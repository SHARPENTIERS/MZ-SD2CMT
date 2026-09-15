#include "edge_record_driver.h"

#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "../drivers/mzio.h"
#include "../drivers/monitor_mute.h"
#include "../drivers/write_edge_monitor.h"
#include "../streams/wav_sample_stream.h"

#if !defined(TIMER5_COMPA_vect)
#error "EDGE record driver requires Timer5 compare-A on ATmega2560"
#endif

#if (F_CPU != 16000000UL)
#error "LEP/L16 Timer5 constants are validated for the 16 MHz ATmega2560 only"
#endif

#define EDGE_FIFO_BYTES WAV_SAMPLE_STREAM_BUFFER_BYTES
#define EDGE_FIFO_CAPACITY (EDGE_FIFO_BYTES - 1U)
#define EDGE_FIFO_MASK (EDGE_FIFO_BYTES - 1U)

/*
   Timer5 CTC timing, all exact at F_CPU = 16 MHz:

     L16: F_CPU/64 = 250 kHz, 4 us/tick, 4 ticks/16 us unit.
     LEP: F_CPU/8  = 2 MHz, 0.5 us/tick, 100 ticks/50 us unit.

   The compare period is 127 units. OCR5A is period-1 because CTC includes
   both zero and OCR5A in one cycle.
*/
#define EDGE_LONG_BLOCK_UNITS 127U
#define EDGE_L16_UNIT_TICKS 4U
#define EDGE_LEP_UNIT_TICKS 100U
#define EDGE_L16_BLOCK_TICKS (EDGE_LONG_BLOCK_UNITS * EDGE_L16_UNIT_TICKS)
#define EDGE_LEP_BLOCK_TICKS (EDGE_LONG_BLOCK_UNITS * EDGE_LEP_UNIT_TICKS)

#if (EDGE_L16_BLOCK_TICKS != 508U)
#error "Invalid L16 Timer5 CTC block"
#endif
#if (EDGE_LEP_BLOCK_TICKS != 12700U)
#error "Invalid LEP Timer5 CTC block"
#endif
#if (EDGE_LEP_BLOCK_TICKS > 65535U)
#error "LEP Timer5 CTC block must fit in OCR5A"
#endif

static volatile uint16_t edge_read_sequence = 0U;
static volatile uint16_t edge_write_sequence = 0U;
static volatile uint8_t edge_state = EDGE_RECORD_DRIVER_STOPPED;

static volatile uint16_t paused_counter = 0U;
static uint16_t long_block_ticks = EDGE_LEP_BLOCK_TICKS;
static uint8_t timer5_clock_bits = _BV(CS51);
/* Selected only while stopped; read in PCINT/Timer5 ISR.  Keeping this as a
   byte lets the quantizer choose an L16 shift path or a LEP subtract path
   without a generic 16-bit division helper. */
static uint8_t edge_quantizer_is_l16 = 0U;

static volatile uint8_t active_level = 0U;
static volatile uint8_t initial_level = 0U;

/*
   Set after the first 127-unit CTC block has elapsed for the current level.
   A long interval is then represented by block tokens plus one signed tail.
*/
static volatile uint8_t interval_has_long_blocks = 0U;

/*
   Error-diffusion residual in Timer5 ticks. LEP can temporarily reach -99..50
   ticks for sub-unit glitches; L16 remains in -3..2 ticks. Both fit int8_t.
*/
static int8_t quantization_residual_ticks = 0;

/* One unpaired 1..15-unit slot is held until the next short slot arrives. */
static volatile uint8_t pending_short_units = 0U;

static inline bool edge_fifo_reserve_from_isr(uint8_t count)
{
    uint16_t used = (uint16_t)(edge_write_sequence - edge_read_sequence);
    return ((uint16_t)(EDGE_FIFO_CAPACITY - used) >= count);
}

static inline bool edge_push_byte_from_isr(uint8_t value)
{
    uint16_t w = edge_write_sequence;

    if (!edge_fifo_reserve_from_isr(1U))
    {
        return false;
    }

    wav_sample_stream_isr_bytes[w & EDGE_FIFO_MASK] = value;
    asm volatile("" ::: "memory");
    edge_write_sequence = (uint16_t)(w + 1U);
    return true;
}

static inline bool edge_flush_pending_short_from_isr(void)
{
    uint8_t pending = pending_short_units;

    if (pending == 0U)
    {
        return true;
    }
    if (!edge_push_byte_from_isr((uint8_t)(pending << 4)))
    {
        return false;
    }
    pending_short_units = 0U;
    return true;
}

static inline bool edge_push_short_units_from_isr(uint8_t units)
{
    uint8_t pending = pending_short_units;

    if ((units == 0U) || (units > 15U))
    {
        return false;
    }
    if (pending == 0U)
    {
        pending_short_units = units;
        return true;
    }
    if (!edge_push_byte_from_isr((uint8_t)((pending << 4) | units)))
    {
        return false;
    }
    pending_short_units = 0U;
    return true;
}

static inline bool edge_push_unit_from_isr(uint8_t units)
{
    uint16_t w;

    if ((units < 16U) || (units > 127U))
    {
        return false;
    }
    if (!edge_flush_pending_short_from_isr() || !edge_fifo_reserve_from_isr(2U))
    {
        return false;
    }

    w = edge_write_sequence;
    wav_sample_stream_isr_bytes[w & EDGE_FIFO_MASK] = EDGE_RECORD_TOKEN_UNIT;
    w++;
    wav_sample_stream_isr_bytes[w & EDGE_FIFO_MASK] = units;
    w++;
    asm volatile("" ::: "memory");
    edge_write_sequence = w;
    return true;
}

/* A CTC compare means exactly one complete 127-unit level block. */
static inline bool edge_push_long_block_from_isr(void)
{
    if (!edge_flush_pending_short_from_isr())
    {
        return false;
    }
    return edge_push_byte_from_isr(EDGE_RECORD_TOKEN_LONG_BLOCK);
}

/* t is signed -1..127. The byte is deliberately transported unchanged. */
static inline bool edge_push_long_tail_from_isr(int8_t tail_units)
{
    uint16_t w;

    if (!edge_fifo_reserve_from_isr(EDGE_RECORD_TOKEN_LONG_TAIL_BYTES))
    {
        return false;
    }

    w = edge_write_sequence;
    wav_sample_stream_isr_bytes[w & EDGE_FIFO_MASK] = EDGE_RECORD_TOKEN_LONG_TAIL;
    w++;
    wav_sample_stream_isr_bytes[w & EDGE_FIFO_MASK] = (uint8_t)tail_units;
    w++;
    asm volatile("" ::: "memory");
    edge_write_sequence = w;
    return true;
}

/*
   The PCINT path must not invoke AVR libgcc 16-bit division.  L16 is an exact
   power-of-two unit (4 Timer5 ticks), while LEP needs only repeated subtraction
   by 100.  At the shortest LEP pulse this loop runs once; its worst 127-loop
   case occurs only just below a 6.350 ms long-block boundary.
*/
static inline bool edge_quantize_l16_short_from_isr(uint16_t ticks,
                                                     uint8_t *units_out)
{
    int16_t adjusted;
    uint16_t units;

    if (units_out == NULL)
    {
        return false;
    }

    adjusted = (int16_t)ticks + (int16_t)quantization_residual_ticks;
    if (adjusted < 1)
    {
        adjusted = 1;
    }

    /* floor((adjusted + 2) / 4), but only a logical shift is generated. */
    units = (uint16_t)(adjusted + 2) >> 2U;
    if (units == 0U)
    {
        units = 1U;
    }
    if (units > 127U)
    {
        return false;
    }

    quantization_residual_ticks =
        (int8_t)(adjusted - (int16_t)((int16_t)units << 2U));
    *units_out = (uint8_t)units;
    return true;
}

static inline bool edge_quantize_lep_short_from_isr(uint16_t ticks,
                                                     uint8_t *units_out)
{
    int16_t adjusted;
    uint16_t rounded;
    uint8_t units = 0U;

    if (units_out == NULL)
    {
        return false;
    }

    adjusted = (int16_t)ticks + (int16_t)quantization_residual_ticks;
    if (adjusted < 1)
    {
        adjusted = 1;
    }

    /* floor((adjusted + 50) / 100) without __divmodhi4. */
    rounded = (uint16_t)(adjusted + 50);
    while (rounded >= EDGE_LEP_UNIT_TICKS)
    {
        rounded = (uint16_t)(rounded - EDGE_LEP_UNIT_TICKS);
        units++;
        if (units > 127U)
        {
            return false;
        }
    }
    if (units == 0U)
    {
        units = 1U;
    }

    quantization_residual_ticks =
        (int8_t)(adjusted - (int16_t)((uint16_t)units * EDGE_LEP_UNIT_TICKS));
    *units_out = units;
    return true;
}

static inline bool edge_quantize_short_from_isr(uint16_t ticks,
                                                uint8_t *units_out)
{
    if (edge_quantizer_is_l16 != 0U)
    {
        return edge_quantize_l16_short_from_isr(ticks, units_out);
    }
    return edge_quantize_lep_short_from_isr(ticks, units_out);
}

static inline bool edge_quantize_l16_long_tail_from_isr(uint16_t ticks,
                                                         int8_t *tail_out)
{
    int16_t adjusted;
    int16_t rounded;
    int16_t tail_units;
    int16_t quantized_ticks;

    if (tail_out == NULL)
    {
        return false;
    }

    adjusted = (int16_t)ticks + (int16_t)quantization_residual_ticks;
    rounded = (int16_t)(adjusted + 2);

    /* floor(rounded / 4), preserving the only possible negative result -1. */
    if (rounded < 0)
    {
        tail_units = -1;
    }
    else
    {
        tail_units = (int16_t)((uint16_t)rounded >> 2U);
    }

    if ((tail_units < -1) || (tail_units > 127))
    {
        return false;
    }

    quantized_ticks = (tail_units < 0) ? -EDGE_L16_UNIT_TICKS :
        (int16_t)(tail_units << 2U);
    quantization_residual_ticks = (int8_t)(adjusted - quantized_ticks);
    *tail_out = (int8_t)tail_units;
    return true;
}

static inline bool edge_quantize_lep_long_tail_from_isr(uint16_t ticks,
                                                         int8_t *tail_out)
{
    int16_t adjusted;
    int16_t rounded;
    int16_t tail_units = 0;
    int16_t quantized_ticks;

    if (tail_out == NULL)
    {
        return false;
    }

    adjusted = (int16_t)ticks + (int16_t)quantization_residual_ticks;
    rounded = (int16_t)(adjusted + 50);

    /* floor(rounded / 100) without __divmodhi4.  The negative case is -1. */
    if (rounded < 0)
    {
        tail_units = -1;
    }
    else
    {
        while (rounded >= (int16_t)EDGE_LEP_UNIT_TICKS)
        {
            rounded = (int16_t)(rounded - (int16_t)EDGE_LEP_UNIT_TICKS);
            tail_units++;
            if (tail_units > 127)
            {
                return false;
            }
        }
    }

    if ((tail_units < -1) || (tail_units > 127))
    {
        return false;
    }

    quantized_ticks = (tail_units < 0) ? -(int16_t)EDGE_LEP_UNIT_TICKS :
        (int16_t)(tail_units * (int16_t)EDGE_LEP_UNIT_TICKS);
    quantization_residual_ticks = (int8_t)(adjusted - quantized_ticks);
    *tail_out = (int8_t)tail_units;
    return true;
}

/* Complete only the tail after one or more exact 127-unit CTC blocks. */
static inline bool edge_quantize_long_tail_from_isr(uint16_t ticks,
                                                     int8_t *tail_out)
{
    if (edge_quantizer_is_l16 != 0U)
    {
        return edge_quantize_l16_long_tail_from_isr(ticks, tail_out);
    }
    return edge_quantize_lep_long_tail_from_isr(ticks, tail_out);
}

static inline bool edge_emit_short_elapsed_from_isr(uint16_t ticks)
{
    uint8_t units;

    if (!edge_quantize_short_from_isr(ticks, &units))
    {
        return false;
    }
    if (units <= 15U)
    {
        return edge_push_short_units_from_isr(units);
    }
    return edge_push_unit_from_isr(units);
}

/* Close the current level at an edge, pause transition or stop. */
static inline bool edge_finish_elapsed_from_isr(uint16_t ticks)
{
    if (interval_has_long_blocks != 0U)
    {
        int8_t tail_units;

        if (!edge_quantize_long_tail_from_isr(ticks, &tail_units) ||
            !edge_push_long_tail_from_isr(tail_units))
        {
            return false;
        }
        interval_has_long_blocks = 0U;
        return true;
    }

    return edge_emit_short_elapsed_from_isr(ticks);
}

/* Called at each exact 127-unit Timer5 CTC compare event. */
static inline bool edge_accept_long_block_from_isr(void)
{
    if (!edge_push_long_block_from_isr())
    {
        return false;
    }
    interval_has_long_blocks = 1U;
    return true;
}

/*
   If OCF5A is pending while PCINT is being serviced, hardware already reset
   TCNT5 in CTC mode but the compare ISR has not run yet. Emit that block first
   so the following tail is measured from the reset counter position.
*/
static inline bool edge_service_pending_compare_from_isr(void)
{
    if ((TIFR5 & _BV(OCF5A)) == 0U)
    {
        return true;
    }

    TIFR5 = _BV(OCF5A);
    return edge_accept_long_block_from_isr();
}

/* Snapshot current CTC sub-block ticks and reset for the next level. */
static inline bool edge_take_elapsed_and_restart_from_isr(uint16_t *ticks)
{
    if ((ticks == NULL) || !edge_service_pending_compare_from_isr())
    {
        return false;
    }

    *ticks = TCNT5;
    TCNT5 = 0U;
    TIFR5 = _BV(OCF5A);
    return true;
}

/* Snapshot current CTC sub-block ticks without disturbing a paused level. */
static inline bool edge_snapshot_elapsed_from_isr(uint16_t *ticks)
{
    if ((ticks == NULL) || !edge_service_pending_compare_from_isr())
    {
        return false;
    }

    *ticks = TCNT5;
    return true;
}

static inline void edge_stop_pcint_from_isr(void)
{
    write_edge_monitor_stop();
}

static inline void edge_stop_timer_from_isr(void)
{
    edge_stop_pcint_from_isr();
    TIMSK5 &= (uint8_t)~_BV(OCIE5A);
    TCCR5B = 0U;
    TCCR5A = 0U;
    TIFR5 = _BV(OCF5A);
    monitor_set_tape_activity_from_isr(false);
}

/* Start or resume Timer5 CTC at a sub-block position below OCR5A+1. */
static inline void edge_start_timer_from_isr(uint16_t ticks)
{
    if (ticks >= long_block_ticks)
    {
        ticks = 0U;
    }

    TCCR5A = 0U;
    TCCR5B = 0U;
    OCR5A = (uint16_t)(long_block_ticks - 1U);
    TCNT5 = ticks;
    TIFR5 = _BV(OCF5A);
    TIMSK5 |= _BV(OCIE5A);
    TCCR5B = (uint8_t)(_BV(WGM52) | timer5_clock_bits);
}

static inline void edge_enable_capture_from_isr(void)
{
    write_edge_monitor_begin_edge_capture();
}

void edge_record_driver_init(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        edge_stop_timer_from_isr();
        edge_read_sequence = 0U;
        edge_write_sequence = 0U;
        paused_counter = 0U;
        long_block_ticks = EDGE_LEP_BLOCK_TICKS;
        timer5_clock_bits = _BV(CS51);
        edge_quantizer_is_l16 = 0U;
        active_level = 0U;
        initial_level = 0U;
        interval_has_long_blocks = 0U;
        pending_short_units = 0U;
        quantization_residual_ticks = 0;
        edge_state = EDGE_RECORD_DRIVER_STOPPED;
    }
}

bool edge_record_driver_prepare(uint8_t unit_us)
{
    edge_record_driver_init();

    if (unit_us == 16U)
    {
        long_block_ticks = EDGE_L16_BLOCK_TICKS;
        timer5_clock_bits = (uint8_t)(_BV(CS51) | _BV(CS50));
        edge_quantizer_is_l16 = 1U;
    }
    else if (unit_us == 50U)
    {
        long_block_ticks = EDGE_LEP_BLOCK_TICKS;
        timer5_clock_bits = _BV(CS51);
        edge_quantizer_is_l16 = 0U;
    }
    else
    {
        edge_state = EDGE_RECORD_DRIVER_BAD_ARGUMENT;
        return false;
    }

    edge_state = EDGE_RECORD_DRIVER_READY;
    return true;
}

bool edge_record_driver_start(void)
{
    if (edge_state != EDGE_RECORD_DRIVER_READY)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        DDRJ &= (uint8_t)~_BV(PJ0);
        PORTJ &= (uint8_t)~_BV(PJ0);

        active_level = mz_write_sample_from_isr();
        initial_level = active_level;
        paused_counter = 0U;
        interval_has_long_blocks = 0U;
        pending_short_units = 0U;
        quantization_residual_ticks = 0;
        edge_start_timer_from_isr(0U);
        edge_state = EDGE_RECORD_DRIVER_RUNNING;
        edge_enable_capture_from_isr();
        monitor_set_tape_activity_from_isr(true);
    }
    return true;
}

bool edge_record_driver_pause(void)
{
    uint16_t ticks;
    bool ok = true;

    if (edge_state != EDGE_RECORD_DRIVER_RUNNING)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        edge_stop_pcint_from_isr();
        if (!edge_snapshot_elapsed_from_isr(&ticks))
        {
            edge_stop_timer_from_isr();
            edge_state = EDGE_RECORD_DRIVER_OVERRUN;
            ok = false;
        }
        else
        {
            TCCR5B = 0U;
            paused_counter = ticks;
            edge_state = EDGE_RECORD_DRIVER_PAUSED;
            monitor_set_tape_activity_from_isr(false);
        }
    }
    return ok;
}

bool edge_record_driver_resume(void)
{
    uint8_t level;
    bool ok = true;

    if (edge_state != EDGE_RECORD_DRIVER_PAUSED)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        level = mz_write_sample_from_isr();

        if (level != active_level)
        {
            /* Close the old level at pause, then begin the changed level. */
            if (!edge_finish_elapsed_from_isr(paused_counter))
            {
                edge_stop_timer_from_isr();
                edge_state = EDGE_RECORD_DRIVER_OVERRUN;
                ok = false;
            }
            else
            {
                active_level = level;
                interval_has_long_blocks = 0U;
                edge_start_timer_from_isr(0U);
            }
        }
        else
        {
            /* Continue the same level; paused wall time is not counted. */
            edge_start_timer_from_isr(paused_counter);
        }

        if (ok)
        {
            edge_state = EDGE_RECORD_DRIVER_RUNNING;
            edge_enable_capture_from_isr();
            monitor_set_tape_activity_from_isr(true);
        }
    }
    return ok;
}

void edge_record_driver_stop(void)
{
    uint16_t ticks = 0U;
    bool valid;
    bool timer_snapshot_ok = true;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        valid = ((edge_state == EDGE_RECORD_DRIVER_RUNNING) ||
                 (edge_state == EDGE_RECORD_DRIVER_PAUSED));
        if (valid)
        {
            edge_stop_pcint_from_isr();
            if (edge_state == EDGE_RECORD_DRIVER_RUNNING)
            {
                timer_snapshot_ok = edge_take_elapsed_and_restart_from_isr(&ticks);
            }
            else
            {
                ticks = paused_counter;
            }

            TCCR5B = 0U;
            TIMSK5 &= (uint8_t)~_BV(OCIE5A);
            TIFR5 = _BV(OCF5A);

            if (!timer_snapshot_ok || !edge_finish_elapsed_from_isr(ticks) ||
                !edge_flush_pending_short_from_isr())
            {
                edge_state = EDGE_RECORD_DRIVER_OVERRUN;
            }
            else
            {
                edge_state = EDGE_RECORD_DRIVER_STOPPED;
            }
            monitor_set_tape_activity_from_isr(false);
        }
    }
}

void edge_record_driver_abort(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        edge_stop_timer_from_isr();
        interval_has_long_blocks = 0U;
        pending_short_units = 0U;
        edge_state = EDGE_RECORD_DRIVER_STOPPED;
    }
}

edge_record_driver_state_t edge_record_driver_get_state(void)
{
    uint8_t state;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        state = edge_state;
    }
    return (edge_record_driver_state_t)state;
}

uint16_t edge_record_driver_available_bytes(void)
{
    uint16_t r;
    uint16_t w;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        r = edge_read_sequence;
        w = edge_write_sequence;
    }
    return (uint16_t)(w - r);
}

bool edge_record_driver_pop_byte(uint8_t *value)
{
    bool result = false;

    if (value == NULL)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        uint16_t r = edge_read_sequence;
        if (r != edge_write_sequence)
        {
            *value = wav_sample_stream_isr_bytes[r & EDGE_FIFO_MASK];
            edge_read_sequence = (uint16_t)(r + 1U);
            result = true;
        }
    }
    return result;
}

uint8_t edge_record_driver_fill_percent(void)
{
    uint32_t percent = ((uint32_t)edge_record_driver_available_bytes() * 100UL) /
                       EDGE_FIFO_CAPACITY;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}

uint8_t edge_record_driver_get_initial_level(void)
{
    uint8_t result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        result = initial_level;
    }
    return result;
}

ISR(TIMER5_COMPA_vect)
{
    if (edge_state == EDGE_RECORD_DRIVER_RUNNING)
    {
        if (!edge_accept_long_block_from_isr())
        {
            edge_stop_timer_from_isr();
            edge_state = EDGE_RECORD_DRIVER_OVERRUN;
        }
    }
}

void edge_record_driver_on_write_edge_from_isr(uint8_t new_level)
{
    uint16_t ticks;

    if (edge_state != EDGE_RECORD_DRIVER_RUNNING)
    {
        return;
    }
    if (new_level == active_level)
    {
        return;
    }

    if (!edge_take_elapsed_and_restart_from_isr(&ticks) ||
        !edge_finish_elapsed_from_isr(ticks))
    {
        edge_stop_timer_from_isr();
        edge_state = EDGE_RECORD_DRIVER_OVERRUN;
        return;
    }

    active_level = new_level;
}
