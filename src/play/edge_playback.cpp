#include "edge_playback.h"
#include "mzf_playback.h"
#include "tap_playback.h"
#include "timer3b_owner.h"

#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <string.h>

#include "../drivers/mzio.h"
#include "../drivers/monitor_mute.h"
#include "../drivers/flash_text.h"
#include "../drivers/sdcard.h"
#include "../streams/wav_sample_stream.h"
#include "../streams/cmt_error_buffers.h"

#if !defined(TIMER3_COMPB_vect)
#error "EDGE playback requires Timer3 compare-B on ATmega2560"
#endif

#define EDGE_FIFO_BYTES WAV_SAMPLE_STREAM_BUFFER_BYTES
#define EDGE_FIFO_CAPACITY (EDGE_FIFO_BYTES - 1U)
#define EDGE_FIFO_MASK (EDGE_FIFO_BYTES - 1U)
#define EDGE_WORK_BYTES WAV_SAMPLE_STREAM_REFILL_BLOCK
#define EDGE_REFILL_RESERVE (EDGE_FIFO_CAPACITY - EDGE_WORK_BYTES)

/*
    LEP: 50 us = 800 Timer3 ticks.  A 127-unit interval is 101600 ticks,
    so it is emitted as 60000 + at most 41600 ticks.  L16: 16 us = 256 ticks,
    therefore every encoded interval fits in one 16-bit compare period.
*/
#define EDGE_LEP_UNIT_TICKS 800U
#define EDGE_L16_UNIT_TICKS 256U
#define EDGE_LEP_FIRST_CHUNK_UNITS 75U
#define EDGE_LEP_FIRST_CHUNK_TICKS 60000U

static volatile uint16_t edge_read_sequence = 0U;
static volatile uint16_t edge_write_sequence = 0U;
static volatile uint8_t edge_source_finished = 1U;
static volatile uint8_t edge_state = EDGE_PLAYBACK_STOPPED;

static bool edge_is_l16 = false;
static bool edge_invert = false;

/* Remaining part of the current source interval after its first compare chunk. */
static uint16_t edge_slot_tail_ticks = 0U;

/* Pause retains at most two 16-bit chunks, never a 32-bit tick count. */
static uint16_t edge_paused_current_ticks = 0U;
static uint16_t edge_paused_tail_ticks = 0U;

static uint32_t edge_total_bytes = 0UL;
/* Foreground-only count of source bytes already copied from SD into the FIFO. */
static uint32_t edge_source_bytes_read = 0UL;
#define edge_error_text cmt_playback_backend_error

static void edge_set_error_P(PGM_P text, edge_playback_state_t state)
{
    flash_text_copy(edge_error_text, sizeof(edge_error_text), text);
    edge_state = (uint8_t)state;
}

static uint16_t edge_used_snapshot(void)
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

static bool edge_pop_from_isr(int8_t *value)
{
    uint16_t r = edge_read_sequence;
    if ((value == NULL) || (r == edge_write_sequence))
    {
        return false;
    }
    *value = (int8_t)wav_sample_stream_isr_bytes[r & EDGE_FIFO_MASK];
    edge_read_sequence = (uint16_t)(r + 1U);
    return true;
}

static void edge_stop_timer_from_isr(void)
{
    TIMSK3 &= (uint8_t)~_BV(OCIE3B);
    TCCR3A = 0U;
    TCCR3B = 0U;
    TIFR3 = _BV(OCF3B);
    if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_EDGE)
    {
        timer3b_owner_set_from_isr(TIMER3B_OWNER_NONE);
    }
    monitor_set_tape_activity_from_isr(false);
}

static void edge_stop_timer_from_foreground(bool force_low)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK3 &= (uint8_t)~_BV(OCIE3B);
        TCCR3A = 0U;
        TCCR3B = 0U;
        TIFR3 = _BV(OCF3B);
        if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_EDGE)
        {
            timer3b_owner_set_from_isr(TIMER3B_OWNER_NONE);
        }
    }
    if (force_low)
    {
        mz_read_set(false);
    }
    monitor_disable();
}

/* First compare is relative to current Timer3 count. */
static inline void edge_schedule_first_chunk_from_isr(uint16_t ticks)
{
    if (ticks == 0U) ticks = 1U;
    OCR3B = (uint16_t)(TCNT3 + ticks);
}

/* Later chunks are anchored to the previous compare point. */
static inline void edge_schedule_next_chunk_from_isr(uint16_t ticks)
{
    if (ticks == 0U) ticks = 1U;
    OCR3B = (uint16_t)(OCR3B + ticks);
}

/*
    Convert one signed-format magnitude into one or two Timer3 chunks.
    This ISR helper uses only 8/16-bit arithmetic.  For L16, x256 is a shift.
*/
static inline uint16_t edge_prepare_interval_from_isr(uint8_t magnitude)
{
    if (edge_is_l16)
    {
        edge_slot_tail_ticks = 0U;
        return (uint16_t)magnitude << 8U;
    }

    if (magnitude > EDGE_LEP_FIRST_CHUNK_UNITS)
    {
        /* On AVR unsigned int is 16 bit. Both factors are bounded so their
           product remains below 65536: (127 - 75) * 800 = 41600. */
        uint16_t tail_units = (uint16_t)(magnitude - EDGE_LEP_FIRST_CHUNK_UNITS);
        edge_slot_tail_ticks = (uint16_t)(tail_units * (uint16_t)EDGE_LEP_UNIT_TICKS);
        return EDGE_LEP_FIRST_CHUNK_TICKS;
    }

    edge_slot_tail_ticks = 0U;
    /* 75 * 800 = 60000, also safe in a 16-bit unsigned product. */
    return (uint16_t)((uint16_t)magnitude * (uint16_t)EDGE_LEP_UNIT_TICKS);
}

static inline void edge_set_slot_level_from_isr(int8_t slot)
{
    uint8_t level = (slot > 0) ? 1U : 0U;

    if (edge_invert)
    {
        level ^= 1U;
    }

    mz_read_set_fast_from_isr(level);
}

static bool edge_refill_once(void)
{
    uint16_t used;
    uint16_t free_bytes;
    uint16_t request;
    int16_t received;
    uint16_t write_local;
    uint8_t *work;

    if (edge_source_finished != 0U)
    {
        return true;
    }

    used = edge_used_snapshot();
    if (used > EDGE_FIFO_CAPACITY)
    {
        edge_set_error_P(PSTR("EDGE FIFO"), EDGE_PLAYBACK_IO_ERROR);
        return false;
    }

    free_bytes = (uint16_t)(EDGE_FIFO_CAPACITY - used);
    if (free_bytes == 0U)
    {
        return true;
    }

    request = free_bytes;
    if (request > EDGE_WORK_BYTES)
    {
        request = EDGE_WORK_BYTES;
    }

    work = wav_sample_stream_get_shared_work_buffer();
    received = sdcard_file_read(work, request);
    if (received < 0)
    {
        edge_set_error_P(PSTR("EDGE READ"), EDGE_PLAYBACK_IO_ERROR);
        return false;
    }

    if (received == 0)
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            edge_source_finished = 1U;
        }
        return true;
    }

    write_local = edge_write_sequence;
    for (int16_t i = 0; i < received; ++i)
    {
        wav_sample_stream_isr_bytes[write_local & EDGE_FIFO_MASK] = work[i];
        write_local = (uint16_t)(write_local + 1U);
    }

    edge_source_bytes_read += (uint32_t)received;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        asm volatile("" ::: "memory");
        edge_write_sequence = write_local;
        if (sdcard_file_position() >= edge_total_bytes)
        {
            edge_source_finished = 1U;
        }
    }

    return true;
}

static bool edge_prefill(void)
{
    while (edge_source_finished == 0U)
    {
        uint16_t before = edge_used_snapshot();
        if (!edge_refill_once())
        {
            return false;
        }
        if (edge_used_snapshot() == before)
        {
            break;
        }
    }
    return edge_used_snapshot() != 0U;
}

/*
   LEP/L16 intentionally do not scan the whole source during prepare. Their
   screen uses byte progress, which is instant and costs no extra SD pass.
*/

void edge_playback_init(void)
{
    edge_stop_timer_from_foreground(true);
    edge_read_sequence = 0U;
    edge_write_sequence = 0U;
    edge_source_finished = 1U;
    edge_state = EDGE_PLAYBACK_STOPPED;
    edge_is_l16 = false;
    edge_invert = false;
    edge_slot_tail_ticks = 0U;
    edge_paused_current_ticks = 0U;
    edge_paused_tail_ticks = 0U;
    edge_total_bytes = 0UL;
    edge_source_bytes_read = 0UL;
    edge_error_text[0] = '\0';
}

bool edge_playback_prepare(const char *path, uint8_t unit_us, bool invert)
{
    edge_playback_stop();

    if ((path == NULL) || ((unit_us != 16U) && (unit_us != 50U)))
    {
        edge_set_error_P(PSTR("EDGE ARG"), EDGE_PLAYBACK_BAD_FILE);
        return false;
    }

    edge_is_l16 = (unit_us == 16U);
    edge_invert = invert;
    edge_error_text[0] = '\0';

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        edge_read_sequence = 0U;
        edge_write_sequence = 0U;
        edge_source_finished = 0U;
    }
    edge_slot_tail_ticks = 0U;
    edge_paused_current_ticks = 0U;
    edge_paused_tail_ticks = 0U;
    edge_source_bytes_read = 0UL;

    if (!sdcard_file_open_read(path))
    {
        edge_set_error_P(PSTR("EDGE OPEN"), EDGE_PLAYBACK_IO_ERROR);
        return false;
    }

    edge_total_bytes = sdcard_file_size();
    if ((edge_total_bytes == 0UL) || !edge_prefill())
    {
        sdcard_file_close();
        edge_set_error_P(PSTR("EMPTY EDGE"), EDGE_PLAYBACK_BAD_FILE);
        return false;
    }

    edge_state = EDGE_PLAYBACK_READY;
    return true;
}

bool edge_playback_start(void)
{
    int8_t first;
    uint8_t magnitude;
    uint16_t first_ticks;

    if (edge_state != EDGE_PLAYBACK_READY)
    {
        return false;
    }

    if (!edge_pop_from_isr(&first) || (first == 0))
    {
        edge_set_error_P(PSTR("EDGE FIRST"), EDGE_PLAYBACK_BAD_FILE);
        return false;
    }

    magnitude = (first < 0) ? (uint8_t)(-first) : (uint8_t)first;
    edge_set_slot_level_from_isr(first);
    first_ticks = edge_prepare_interval_from_isr(magnitude);

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TCNT3 = 0U;
        TCCR3A = 0U;
        TCCR3B = _BV(CS30);
        TIFR3 = _BV(OCF3B);
        edge_state = EDGE_PLAYBACK_RUNNING;
        monitor_set_tape_activity_from_isr(true);
        timer3b_owner_set_from_isr(TIMER3B_OWNER_EDGE);
        edge_schedule_first_chunk_from_isr(first_ticks);
        TIMSK3 |= _BV(OCIE3B);
    }

    mz_sense_set(false);
    return true;
}

bool edge_playback_pause(void)
{
    uint16_t current_chunk_remaining;

    if (edge_state != EDGE_PLAYBACK_RUNNING)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        current_chunk_remaining = (uint16_t)(OCR3B - TCNT3);
        if ((TIFR3 & _BV(OCF3B)) != 0U || current_chunk_remaining == 0U)
        {
            current_chunk_remaining = 1U;
        }
        edge_paused_current_ticks = current_chunk_remaining;
        edge_paused_tail_ticks = edge_slot_tail_ticks;
        edge_slot_tail_ticks = 0U;
        edge_stop_timer_from_isr();
        edge_state = EDGE_PLAYBACK_PAUSED;
    }
    return true;
}

bool edge_playback_resume(void)
{
    if (edge_state != EDGE_PLAYBACK_PAUSED)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TCNT3 = 0U;
        TCCR3A = 0U;
        TCCR3B = _BV(CS30);
        TIFR3 = _BV(OCF3B);
        edge_state = EDGE_PLAYBACK_RUNNING;
        monitor_set_tape_activity_from_isr(true);
        edge_slot_tail_ticks = edge_paused_tail_ticks;
        edge_paused_tail_ticks = 0U;
        timer3b_owner_set_from_isr(TIMER3B_OWNER_EDGE);
        edge_schedule_first_chunk_from_isr(edge_paused_current_ticks);
        edge_paused_current_ticks = 0U;
        TIMSK3 |= _BV(OCIE3B);
    }
    return true;
}

void edge_playback_stop(void)
{
    edge_stop_timer_from_foreground(true);
    sdcard_file_close();
    edge_source_finished = 1U;
    edge_state = EDGE_PLAYBACK_STOPPED;
    edge_read_sequence = 0U;
    edge_write_sequence = 0U;
    edge_slot_tail_ticks = 0U;
    edge_paused_current_ticks = 0U;
    edge_paused_tail_ticks = 0U;
    edge_total_bytes = 0UL;
    edge_source_bytes_read = 0UL;
    mz_sense_set(true);
}

void edge_playback_service(void)
{
    if (edge_state == EDGE_PLAYBACK_RUNNING)
    {
        /* Refill one bounded SD block as soon as it fits.  This keeps the
           playback safety reserve near 75--100% without lengthening one
           foreground service pass. */
        if ((edge_source_finished == 0U) &&
            (edge_used_snapshot() <= EDGE_REFILL_RESERVE))
        {
            (void)edge_refill_once();
        }
    }

    if ((edge_state == EDGE_PLAYBACK_FINISHED) ||
        (edge_state == EDGE_PLAYBACK_UNDERRUN) ||
        (edge_state == EDGE_PLAYBACK_IO_ERROR) ||
        (edge_state == EDGE_PLAYBACK_BAD_FILE))
    {
        sdcard_file_close();
    }
}

edge_playback_state_t edge_playback_get_state(void)
{
    uint8_t state;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        state = edge_state;
    }
    return (edge_playback_state_t)state;
}

const char *edge_playback_get_error_text(void) { return edge_error_text; }

uint8_t edge_playback_get_buffer_fill_percent(void)
{
    uint16_t used = edge_used_snapshot();
    uint32_t consumed;
    uint32_t remaining;
    uint32_t target;
    uint32_t percent;

    if (used > EDGE_FIFO_CAPACITY) return 0U;
    if (edge_source_bytes_read < (uint32_t)used) return 0U;

    consumed = edge_source_bytes_read - (uint32_t)used;
    if (consumed >= edge_total_bytes) return 100U;

    remaining = edge_total_bytes - consumed;
    target = (remaining < (uint32_t)EDGE_FIFO_CAPACITY) ?
        remaining : (uint32_t)EDGE_FIFO_CAPACITY;
    if (target == 0UL) return 100U;

    percent = ((uint32_t)used * 100UL) / target;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}

uint32_t edge_playback_get_consumed_bytes(void)
{
    uint16_t buffered = edge_used_snapshot();
    uint32_t read = edge_source_bytes_read;

    return ((uint32_t)buffered > read) ? 0UL : (read - (uint32_t)buffered);
}

uint32_t edge_playback_get_total_bytes(void)
{
    return edge_total_bytes;
}

uint8_t edge_playback_get_progress_percent(void)
{
    uint32_t total = edge_total_bytes;
    uint32_t consumed = edge_playback_get_consumed_bytes();
    uint32_t percent;

    if (total == 0UL) return 0U;
    if (consumed >= total) return 100U;

    /* Edge files used here are far below the 42 MiB 32-bit x100 limit. */
    percent = ((consumed * 100UL) + (total / 2UL)) / total;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}

static void edge_playback_timer3_compb_from_isr(void)
{
    int8_t next;
    uint8_t magnitude;

    if (edge_state != EDGE_PLAYBACK_RUNNING)
    {
        edge_stop_timer_from_isr();
        return;
    }

    if (edge_slot_tail_ticks != 0U)
    {
        uint16_t tail = edge_slot_tail_ticks;
        edge_slot_tail_ticks = 0U;
        edge_schedule_next_chunk_from_isr(tail);
        return;
    }

    if (!edge_pop_from_isr(&next))
    {
        edge_stop_timer_from_isr();
        mz_read_set_fast_from_isr(0U);
        edge_state = (edge_source_finished != 0U) ?
            EDGE_PLAYBACK_FINISHED : EDGE_PLAYBACK_UNDERRUN;
        return;
    }

    if (next == 0)
    {
        /* 0 extends the current polarity by exactly 127 format units. */
        edge_schedule_next_chunk_from_isr(edge_prepare_interval_from_isr(127U));
        return;
    }

    magnitude = (next < 0) ? (uint8_t)(-next) : (uint8_t)next;
    edge_set_slot_level_from_isr(next);
    edge_schedule_next_chunk_from_isr(edge_prepare_interval_from_isr(magnitude));
}

ISR(TIMER3_COMPB_vect)
{
    switch (timer3b_owner_get_from_isr())
    {
        case TIMER3B_OWNER_EDGE:
            edge_playback_timer3_compb_from_isr();
            break;

        case TIMER3B_OWNER_MZF:
            (void)mzf_playback_timer3_compb_from_isr();
            break;

        case TIMER3B_OWNER_TAP:
            tap_playback_timer3_compb_from_isr();
            break;

        case TIMER3B_OWNER_NONE:
        default:
            TIMSK3 &= (uint8_t)~_BV(OCIE3B);
            TIFR3 = _BV(OCF3B);
            break;
    }
}
