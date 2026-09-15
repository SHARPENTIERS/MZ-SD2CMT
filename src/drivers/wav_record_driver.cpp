#include "wav_record_driver.h"

#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "mzio.h"
#include "monitor_mute.h"
#include "../streams/record_sample_stream.h"

#if !defined(TIMER1_COMPA_vect)
#error "WAV recording driver requires Timer1 compare A on Arduino Mega 2560"
#endif

static volatile uint8_t driver_state = WAV_RECORD_DRIVER_STOPPED;

static uint16_t timer_sample_rate = 0U;
static uint16_t timer_base_ticks = 0U;
static uint16_t timer_remainder_ticks = 0U;
static uint16_t timer_phase_ticks = 0U;

static volatile uint8_t pack_byte = 0U;
static volatile uint8_t pack_bits = 0U;

static volatile uint8_t tail_byte = 0U;
static volatile uint8_t tail_bits = 0U;
static volatile uint8_t tail_available = 0U;

static inline __attribute__((always_inline))
uint16_t wav_record_driver_next_period_from_isr(void)
{
    uint16_t ticks = timer_base_ticks;
    uint16_t phase = (uint16_t)(timer_phase_ticks + timer_remainder_ticks);

    if (phase >= timer_sample_rate)
    {
        phase = (uint16_t)(phase - timer_sample_rate);
        ticks++;
    }

    timer_phase_ticks = phase;
    return ticks;
}

static void wav_record_driver_disable_timer_from_foreground(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK1 &= (uint8_t)~_BV(OCIE1A);
        TCCR1B = 0U;
        TCCR1A = 0U;
        TIFR1 = _BV(OCF1A);
    }
    monitor_disable();
}

void wav_record_driver_init(void)
{
    wav_record_driver_disable_timer_from_foreground();

    /*
        WRITE = Arduino D15 = PJ0. Explicitly select high-impedance input with
        no internal pull-up; the Sharp interface drives this signal.
    */
    DDRJ &= (uint8_t)~_BV(PJ0);
    PORTJ &= (uint8_t)~_BV(PJ0);

    timer_sample_rate = 0U;
    timer_base_ticks = 0U;
    timer_remainder_ticks = 0U;
    timer_phase_ticks = 0U;
    pack_byte = 0U;
    pack_bits = 0U;
    tail_byte = 0U;
    tail_bits = 0U;
    tail_available = 0U;
    driver_state = WAV_RECORD_DRIVER_STOPPED;
}

bool wav_record_driver_prepare(uint32_t sample_rate)
{
    uint32_t base_ticks;
    uint32_t remainder_ticks;

    if ((sample_rate != 22050UL) && (sample_rate != 44100UL))
    {
        driver_state = WAV_RECORD_DRIVER_BAD_RATE;
        return false;
    }

    base_ticks = (uint32_t)F_CPU / sample_rate;
    remainder_ticks = (uint32_t)F_CPU % sample_rate;

    if ((base_ticks < 32UL) || (base_ticks > 65535UL))
    {
        driver_state = WAV_RECORD_DRIVER_BAD_RATE;
        return false;
    }

    wav_record_driver_disable_timer_from_foreground();

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        timer_sample_rate = (uint16_t)sample_rate;
        timer_base_ticks = (uint16_t)base_ticks;
        timer_remainder_ticks = (uint16_t)remainder_ticks;
        timer_phase_ticks = 0U;
        pack_byte = 0U;
        pack_bits = 0U;
        tail_byte = 0U;
        tail_bits = 0U;
        tail_available = 0U;
        driver_state = WAV_RECORD_DRIVER_READY;
    }

    record_sample_stream_reset();
    return true;
}

bool wav_record_driver_start(void)
{
    if (driver_state != WAV_RECORD_DRIVER_READY)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TCNT1 = 0U;
        OCR1A = (uint16_t)(timer_base_ticks - 1U);

        TCCR1A = 0U;
        TCCR1B = _BV(WGM12);
        TIFR1 = _BV(OCF1A);

        driver_state = WAV_RECORD_DRIVER_RUNNING;
        monitor_set_tape_activity_from_isr(true);
        TIMSK1 |= _BV(OCIE1A);
        TCCR1B = (uint8_t)(_BV(WGM12) | _BV(CS10));
    }

    return true;
}

bool wav_record_driver_pause(void)
{
    if (driver_state != WAV_RECORD_DRIVER_RUNNING)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK1 &= (uint8_t)~_BV(OCIE1A);
        TCCR1B = 0U;
        TIFR1 = _BV(OCF1A);
        driver_state = WAV_RECORD_DRIVER_PAUSED;
        monitor_set_tape_activity_from_isr(false);
    }
    return true;
}

bool wav_record_driver_resume(void)
{
    if (driver_state != WAV_RECORD_DRIVER_PAUSED)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TCNT1 = 0U;
        OCR1A = (uint16_t)(timer_base_ticks - 1U);
        TCCR1A = 0U;
        TCCR1B = _BV(WGM12);
        TIFR1 = _BV(OCF1A);
        driver_state = WAV_RECORD_DRIVER_RUNNING;
        monitor_set_tape_activity_from_isr(true);
        TIMSK1 |= _BV(OCIE1A);
        TCCR1B = (uint8_t)(_BV(WGM12) | _BV(CS10));
    }
    return true;
}

void wav_record_driver_stop(void)
{
    uint8_t local_pack_byte;
    uint8_t local_pack_bits;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK1 &= (uint8_t)~_BV(OCIE1A);
        TCCR1B = 0U;
        TCCR1A = 0U;
        TIFR1 = _BV(OCF1A);

        local_pack_byte = pack_byte;
        local_pack_bits = pack_bits;

        pack_byte = 0U;
        pack_bits = 0U;

        if (local_pack_bits != 0U)
        {
            tail_byte = local_pack_byte;
            tail_bits = local_pack_bits;
            tail_available = 1U;
        }

        if (driver_state != WAV_RECORD_DRIVER_OVERRUN)
        {
            driver_state = WAV_RECORD_DRIVER_STOPPED;
        }
        monitor_set_tape_activity_from_isr(false);
    }
}

wav_record_driver_state_t wav_record_driver_get_state(void)
{
    uint8_t state;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        state = driver_state;
    }

    return (wav_record_driver_state_t)state;
}

uint32_t wav_record_driver_get_sample_rate(void)
{
    return (uint32_t)timer_sample_rate;
}

uint8_t wav_record_driver_get_write_pin(void)
{
    return mzio_write_pin();
}

uint8_t wav_record_driver_get_pending_sample_count(void)
{
    uint8_t bits;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        bits = (tail_available != 0U) ? tail_bits : pack_bits;
    }

    return bits;
}

bool wav_record_driver_take_tail(uint8_t *packed_byte,
                                 uint8_t *valid_bits)
{
    bool result = false;

    if ((packed_byte == NULL) || (valid_bits == NULL))
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (tail_available != 0U)
        {
            *packed_byte = tail_byte;
            *valid_bits = tail_bits;
            tail_byte = 0U;
            tail_bits = 0U;
            tail_available = 0U;
            result = true;
        }
    }

    return result;
}

ISR(TIMER1_COMPA_vect)
{
    uint8_t level;
    uint16_t ticks;

    if (driver_state != WAV_RECORD_DRIVER_RUNNING)
    {
        TIMSK1 &= (uint8_t)~_BV(OCIE1A);
        TCCR1B = 0U;
        driver_state = WAV_RECORD_DRIVER_BAD_ARGUMENT;
        monitor_set_tape_activity_from_isr(false);
        return;
    }

    /*
        Exactly one digital WRITE state per sample interval. D15/PJ0 is read
        directly; no digitalRead(), no ADC and no function callback.
    */
    level = mz_write_sample_from_isr();

    if (level != 0U)
    {
        pack_byte |= (uint8_t)(1U << pack_bits);
    }

    pack_bits++;

    if (pack_bits == 8U)
    {
        if (!record_sample_stream_push_byte_from_isr(pack_byte))
        {
            TIMSK1 &= (uint8_t)~_BV(OCIE1A);
            TCCR1B = 0U;
            driver_state = WAV_RECORD_DRIVER_OVERRUN;
            monitor_set_tape_activity_from_isr(false);
            return;
        }

        pack_byte = 0U;
        pack_bits = 0U;
    }

    ticks = wav_record_driver_next_period_from_isr();

    /*
        CTC reset is hardware timed. OCR1A is updated for the following
        362/363 (44.1 kHz) or 725/726 (22.05 kHz) clock interval.
    */
    OCR1A = (uint16_t)(ticks - 1U);
}
