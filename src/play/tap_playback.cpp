#include "tap_playback.h"

#include <Arduino.h>
#include <avr/io.h>
#include <util/atomic.h>

#include "timer3b_owner.h"
#include "../drivers/flash_text.h"
#include "../drivers/monitor_mute.h"
#include "../drivers/mzio.h"
#include "../drivers/sdcard.h"
#include "../streams/wav_sample_stream.h"
#include "../streams/cmt_error_buffers.h"

#if !defined(TIMER3_COMPB_vect)
#error "TAP playback requires Timer3 compare-B on ATmega2560"
#endif

#define TAP_FIFO_BYTES WAV_SAMPLE_STREAM_BUFFER_BYTES
#define TAP_FIFO_CAPACITY (TAP_FIFO_BYTES - 1U)
#define TAP_FIFO_MASK (TAP_FIFO_BYTES - 1U)
#define TAP_WORK_BYTES WAV_SAMPLE_STREAM_REFILL_BLOCK
#define TAP_REFILL_RESERVE (TAP_FIFO_CAPACITY - TAP_WORK_BYTES)

#define TAP_HEADER_FLAG 0x00U
#define TAP_DATA_FLAG 0xFFU
#define TAP_HEADER_PILOT_PULSES 4032U
#define TAP_DATA_PILOT_PULSES 1610U

#define TAP_GAP_CHUNK_TICKS 60000U
#define TAP_HEADER_GAP_CHUNKS 93U
#define TAP_HEADER_GAP_TAIL_TICKS 20000U
#define TAP_DATA_GAP_CHUNKS 266U
#define TAP_DATA_GAP_TAIL_TICKS 40000U

/* Progress uses small nominal timing units. This keeps the Timer3 ISR cheap
   while allowing the displayed percentage to advance during pilots and gaps. */
#define TAP_PROGRESS_TICK_DIVISOR 256UL

#if ((TAP_FIFO_BYTES & (TAP_FIFO_BYTES - 1U)) != 0)
#error "TAP FIFO size must be a power of two"
#endif

typedef enum
{
    TAP_PHASE_IDLE = 0U,
    TAP_PHASE_PILOT_LOW,
    TAP_PHASE_PILOT_HIGH,
    TAP_PHASE_SYNC_LOW,
    TAP_PHASE_SYNC_HIGH,
    TAP_PHASE_DATA_LOW,
    TAP_PHASE_DATA_HIGH,
    TAP_PHASE_TERMINATOR_LOW,
    TAP_PHASE_GAP_LOW
} tap_phase_t;

static volatile uint16_t tap_read_sequence = 0U;
static volatile uint16_t tap_write_sequence = 0U;
static volatile uint8_t tap_source_finished = 1U;
static volatile uint8_t tap_state = TAP_PLAYBACK_STOPPED;
static volatile uint8_t tap_phase = TAP_PHASE_IDLE;
static volatile uint8_t tap_need_next_block = 0U;
static volatile uint8_t tap_next_block_ready = 0U;
static volatile uint32_t tap_progress_elapsed_units = 0UL;

/* Foreground owns the unread source count for the active or pending block. */
static uint16_t tap_source_remaining = 0U;
static uint32_t tap_total_file_bytes = 0UL;
static uint32_t tap_progress_total_units = 0UL;

/* Current block fields are consumed only by the ISR while RUNNING. */
static uint16_t tap_block_length = 0U;
static uint16_t tap_block_bytes_emitted = 0U;
static uint8_t tap_current_flag = TAP_HEADER_FLAG;
static bool tap_current_is_last = true;
static uint8_t tap_current_byte = 0U;
static uint8_t tap_bit_mask = 0x80U;
static uint16_t tap_current_bit_ticks = 1U;
static uint16_t tap_pilot_pulses_remaining = 0U;

/* The foreground publishes these together by setting tap_next_block_ready. */
static uint16_t tap_pending_length = 0U;
static uint8_t tap_pending_flag = TAP_HEADER_FLAG;
static bool tap_pending_is_last = true;

/* Unscheduled part of a long container gap. */
static volatile uint16_t tap_gap_chunks_remaining = 0U;
static volatile uint16_t tap_gap_tail_ticks = 0U;
static volatile uint16_t tap_gap_current_progress_units = 0U;
static uint16_t tap_paused_current_ticks = 0U;

static tap_timing_t tap_timing;
static uint16_t tap_progress_pilot_pulse_units = 1U;
static uint16_t tap_progress_sync_units = 1U;
static uint16_t tap_progress_bit_units = 1U;
static uint16_t tap_progress_terminator_units = 1U;
static uint16_t tap_progress_gap_chunk_units = 1U;
static uint16_t tap_progress_header_gap_tail_units = 1U;
static uint16_t tap_progress_data_gap_tail_units = 1U;
#define tap_error_text cmt_playback_backend_error

static uint16_t tap_progress_units_for_ticks(uint32_t ticks)
{
    uint32_t units = (ticks + TAP_PROGRESS_TICK_DIVISOR - 1UL) /
                     TAP_PROGRESS_TICK_DIVISOR;
    if (units == 0UL) return 1U;
    return (units > 0xFFFFUL) ? 0xFFFFU : (uint16_t)units;
}

static void tap_progress_total_add(uint32_t units)
{
    if (units > (0xFFFFFFFFUL - tap_progress_total_units))
        tap_progress_total_units = 0xFFFFFFFFUL;
    else
        tap_progress_total_units += units;
}

static inline void tap_progress_add_from_isr(uint16_t units)
{
    uint32_t elapsed = tap_progress_elapsed_units;
    if ((uint32_t)units > (0xFFFFFFFFUL - elapsed))
        tap_progress_elapsed_units = 0xFFFFFFFFUL;
    else
        tap_progress_elapsed_units = elapsed + (uint32_t)units;
}

static uint16_t tap_used_snapshot(void)
{
    uint16_t read_snapshot;
    uint16_t write_snapshot;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        read_snapshot = tap_read_sequence;
        write_snapshot = tap_write_sequence;
    }
    return (uint16_t)(write_snapshot - read_snapshot);
}

static bool tap_pop_from_isr(uint8_t *value)
{
    uint16_t read_local = tap_read_sequence;

    if ((value == NULL) || (read_local == tap_write_sequence))
    {
        return false;
    }
    *value = wav_sample_stream_isr_bytes[read_local & TAP_FIFO_MASK];
    tap_read_sequence = (uint16_t)(read_local + 1U);
    return true;
}

static void tap_stop_timer_from_isr(void)
{
    TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
    TCCR3A = 0U;
    TCCR3B = 0U;
    TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
    if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_TAP)
    {
        timer3b_owner_set_from_isr(TIMER3B_OWNER_NONE);
    }
    monitor_set_tape_activity_from_isr(false);
}

static void tap_stop_timer_from_foreground(bool force_low)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tap_stop_timer_from_isr();
    }
    if (force_low) mz_read_set(false);
    monitor_disable();
}

static void tap_set_error_P(PGM_P text, tap_playback_state_t state)
{
    tap_stop_timer_from_foreground(true);
    sdcard_file_close();
    flash_text_copy(tap_error_text, sizeof(tap_error_text), text);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tap_phase = TAP_PHASE_IDLE;
        tap_state = (uint8_t)state;
    }
}

static inline void tap_schedule_first_from_isr(uint16_t ticks)
{
    if (ticks == 0U) ticks = 1U;
    OCR3B = (uint16_t)(TCNT3 + ticks);
}

static inline void tap_schedule_next_from_isr(uint16_t ticks)
{
    if (ticks == 0U) ticks = 1U;
    OCR3B = (uint16_t)(OCR3B + ticks);
}

static inline uint16_t tap_bit_ticks_from_isr(void)
{
    return ((tap_current_byte & tap_bit_mask) != 0U) ?
        tap_timing.long_half_ticks : tap_timing.short_half_ticks;
}

static void tap_fail_from_isr(tap_playback_state_t state)
{
    tap_stop_timer_from_isr();
    mz_read_set_fast_from_isr(0U);
    tap_phase = TAP_PHASE_IDLE;
    tap_state = (uint8_t)state;
}

static bool tap_refill_once(void)
{
    uint16_t used;
    uint16_t free_bytes;
    uint16_t request;
    int16_t received;
    uint16_t write_local;
    uint8_t *work;

    if (tap_source_remaining == 0U)
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            tap_source_finished = 1U;
        }
        return true;
    }

    used = tap_used_snapshot();
    if (used > TAP_FIFO_CAPACITY)
    {
        tap_set_error_P(PSTR("TAP FIFO"), TAP_PLAYBACK_IO_ERROR);
        return false;
    }

    free_bytes = (uint16_t)(TAP_FIFO_CAPACITY - used);
    if (free_bytes == 0U) return true;

    request = free_bytes;
    if (request > TAP_WORK_BYTES) request = TAP_WORK_BYTES;
    if (request > tap_source_remaining) request = tap_source_remaining;

    work = wav_sample_stream_get_shared_work_buffer();
    received = sdcard_file_read(work, request);
    if (received < 0)
    {
        tap_set_error_P(PSTR("TAP READ"), TAP_PLAYBACK_IO_ERROR);
        return false;
    }
    if ((uint16_t)received != request)
    {
        tap_set_error_P(PSTR("TAP TRUNC"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }

    write_local = tap_write_sequence;
    for (uint16_t i = 0U; i < request; ++i)
    {
        wav_sample_stream_isr_bytes[write_local & TAP_FIFO_MASK] = work[i];
        write_local = (uint16_t)(write_local + 1U);
    }
    tap_source_remaining = (uint16_t)(tap_source_remaining - request);

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        asm volatile("" ::: "memory");
        tap_write_sequence = write_local;
        if (tap_source_remaining == 0U) tap_source_finished = 1U;
    }
    return true;
}

static bool tap_prefill(void)
{
    while (tap_source_remaining != 0U)
    {
        uint16_t before = tap_used_snapshot();
        if (!tap_refill_once()) return false;
        if (tap_used_snapshot() == before) break;
    }
    return tap_used_snapshot() != 0U;
}

static bool tap_read_exact(uint8_t *destination, uint16_t length)
{
    int16_t received = sdcard_file_read(destination, length);

    if (received < 0)
    {
        tap_set_error_P(PSTR("TAP READ"), TAP_PLAYBACK_IO_ERROR);
        return false;
    }
    if ((uint16_t)received != length)
    {
        tap_set_error_P(PSTR("TAP TRUNC"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }
    return true;
}

/* Validate the block framing and calculate nominal playback work before the
   first block is prepared. Only the two-byte length and flag are read; block
   payloads remain streamed during PLAY. */
static bool tap_scan_progress_total(void)
{
    uint8_t length_bytes[2];
    uint8_t flag;
    uint32_t position = 0UL;

    tap_progress_total_units = 0UL;
    while (position < tap_total_file_bytes)
    {
        uint16_t length;
        uint32_t block_end;
        bool is_last;

        if ((tap_total_file_bytes - position) < 2UL)
        {
            tap_set_error_P(PSTR("TAP LENGTH"), TAP_PLAYBACK_BAD_FILE);
            return false;
        }
        if (!sdcard_file_seek(position))
        {
            tap_set_error_P(PSTR("TAP READ"), TAP_PLAYBACK_IO_ERROR);
            return false;
        }
        if (!tap_read_exact(length_bytes, sizeof(length_bytes))) return false;
        length = (uint16_t)length_bytes[0] |
                 (uint16_t)((uint16_t)length_bytes[1] << 8U);
        if (length < 2U)
        {
            tap_set_error_P(PSTR("TAP LENGTH"), TAP_PLAYBACK_BAD_FILE);
            return false;
        }

        block_end = position + 2UL + (uint32_t)length;
        if ((block_end < position) || (block_end > tap_total_file_bytes))
        {
            tap_set_error_P(PSTR("TAP TRUNC"), TAP_PLAYBACK_BAD_FILE);
            return false;
        }
        if (!tap_read_exact(&flag, 1U)) return false;
        if ((flag != TAP_HEADER_FLAG) && (flag != TAP_DATA_FLAG))
        {
            tap_set_error_P(PSTR("TAP FLAG"), TAP_PLAYBACK_BAD_FILE);
            return false;
        }

        is_last = (block_end == tap_total_file_bytes);
        tap_progress_total_add((uint32_t)
            ((flag == TAP_HEADER_FLAG) ? TAP_HEADER_PILOT_PULSES :
                                         TAP_DATA_PILOT_PULSES) *
            (uint32_t)tap_progress_pilot_pulse_units);
        tap_progress_total_add(tap_progress_sync_units);
        tap_progress_total_add((uint32_t)length * 8UL *
                               (uint32_t)tap_progress_bit_units);
        tap_progress_total_add(tap_progress_terminator_units);
        if (!is_last)
        {
            if (flag == TAP_HEADER_FLAG)
            {
                tap_progress_total_add((uint32_t)TAP_HEADER_GAP_CHUNKS *
                                       tap_progress_gap_chunk_units);
                tap_progress_total_add(
                    tap_progress_header_gap_tail_units);
            }
            else
            {
                tap_progress_total_add((uint32_t)TAP_DATA_GAP_CHUNKS *
                                       tap_progress_gap_chunk_units);
                tap_progress_total_add(tap_progress_data_gap_tail_units);
            }
        }
        position = block_end;
    }

    if (!sdcard_file_seek(0UL))
    {
        tap_set_error_P(PSTR("TAP READ"), TAP_PLAYBACK_IO_ERROR);
        return false;
    }
    return tap_progress_total_units != 0UL;
}

static void tap_set_current_block(uint16_t length, uint8_t flag, bool is_last)
{
    tap_block_length = length;
    tap_block_bytes_emitted = 0U;
    tap_current_flag = flag;
    tap_current_is_last = is_last;
    tap_current_byte = flag;
    tap_bit_mask = 0x80U;
    tap_current_bit_ticks = 1U;
    tap_pilot_pulses_remaining = (flag == TAP_HEADER_FLAG) ?
        TAP_HEADER_PILOT_PULSES : TAP_DATA_PILOT_PULSES;
}

/* Parse and prefill exactly one block. No bytes from the following block enter
   the FIFO, and the cached flag remains the first byte emitted by the ISR. */
static bool tap_parse_block(bool first_block)
{
    uint8_t length_bytes[2];
    uint8_t flag;
    uint16_t length;
    uint32_t position;
    uint32_t bytes_left;
    bool is_last;

    if (tap_used_snapshot() != 0U)
    {
        tap_set_error_P(PSTR("TAP FIFO"), TAP_PLAYBACK_IO_ERROR);
        return false;
    }

    position = sdcard_file_position();
    if (position > tap_total_file_bytes)
    {
        tap_set_error_P(PSTR("TAP READ"), TAP_PLAYBACK_IO_ERROR);
        return false;
    }
    bytes_left = tap_total_file_bytes - position;
    if (bytes_left < 2UL)
    {
        tap_set_error_P(PSTR("TAP LENGTH"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }
    if (!tap_read_exact(length_bytes, sizeof(length_bytes))) return false;

    length = (uint16_t)length_bytes[0] |
             (uint16_t)((uint16_t)length_bytes[1] << 8U);
    if (length < 2U)
    {
        tap_set_error_P(PSTR("TAP LENGTH"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }

    position = sdcard_file_position();
    bytes_left = tap_total_file_bytes - position;
    if ((uint32_t)length > bytes_left)
    {
        tap_set_error_P(PSTR("TAP TRUNC"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }
    is_last = ((uint32_t)length == bytes_left);

    if (!tap_read_exact(&flag, 1U)) return false;
    if ((flag != TAP_HEADER_FLAG) && (flag != TAP_DATA_FLAG))
    {
        tap_set_error_P(PSTR("TAP FLAG"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }

    tap_source_remaining = (uint16_t)(length - 1U);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tap_source_finished = 0U;
    }
    if (!tap_prefill())
    {
        if (tap_state != TAP_PLAYBACK_IO_ERROR &&
            tap_state != TAP_PLAYBACK_BAD_FILE)
        {
            tap_set_error_P(PSTR("TAP FIFO"), TAP_PLAYBACK_IO_ERROR);
        }
        return false;
    }

    if (first_block)
    {
        tap_set_current_block(length, flag, is_last);
    }
    else
    {
        tap_pending_length = length;
        tap_pending_flag = flag;
        tap_pending_is_last = is_last;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            asm volatile("" ::: "memory");
            tap_next_block_ready = 1U;
        }
    }
    return true;
}

static bool tap_schedule_gap_piece_from_isr(void)
{
    if (tap_gap_chunks_remaining != 0U)
    {
        tap_gap_chunks_remaining--;
        tap_gap_current_progress_units = tap_progress_gap_chunk_units;
        tap_schedule_next_from_isr(TAP_GAP_CHUNK_TICKS);
        return true;
    }
    if (tap_gap_tail_ticks != 0U)
    {
        uint16_t tail = tap_gap_tail_ticks;
        tap_gap_tail_ticks = 0U;
        tap_gap_current_progress_units =
            (tail == TAP_HEADER_GAP_TAIL_TICKS) ?
            tap_progress_header_gap_tail_units :
            tap_progress_data_gap_tail_units;
        tap_schedule_next_from_isr(tail);
        return true;
    }
    tap_gap_current_progress_units = 0U;
    return false;
}

static void tap_activate_pending_from_isr(void)
{
    tap_set_current_block(tap_pending_length,
                          tap_pending_flag,
                          tap_pending_is_last);
    tap_next_block_ready = 0U;
    tap_need_next_block = 0U;
}

void tap_playback_init(void)
{
    tap_playback_stop();
}

bool tap_playback_prepare(const char *path, tap_speed_t speed)
{
    tap_playback_stop();

    if ((path == NULL) || !tap_speed_get_timing(speed, &tap_timing))
    {
        tap_set_error_P(PSTR("TAP ARG"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }

    tap_error_text[0] = '\0';
    tap_total_file_bytes = 0UL;
    tap_progress_total_units = 0UL;
    tap_source_remaining = 0U;
    tap_progress_pilot_pulse_units = tap_progress_units_for_ticks(
        (uint32_t)tap_timing.pilot_half_ticks * 2UL);
    tap_progress_sync_units = tap_progress_units_for_ticks(
        (uint32_t)tap_timing.sync_low_ticks + tap_timing.sync_high_ticks);
    tap_progress_bit_units = tap_progress_units_for_ticks(
        (uint32_t)tap_timing.long_half_ticks + tap_timing.short_half_ticks);
    tap_progress_terminator_units = tap_progress_units_for_ticks(
        tap_timing.pilot_half_ticks);
    tap_progress_gap_chunk_units = tap_progress_units_for_ticks(
        TAP_GAP_CHUNK_TICKS);
    tap_progress_header_gap_tail_units = tap_progress_units_for_ticks(
        TAP_HEADER_GAP_TAIL_TICKS);
    tap_progress_data_gap_tail_units = tap_progress_units_for_ticks(
        TAP_DATA_GAP_TAIL_TICKS);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tap_read_sequence = 0U;
        tap_write_sequence = 0U;
        tap_source_finished = 1U;
        tap_need_next_block = 0U;
        tap_next_block_ready = 0U;
        tap_phase = TAP_PHASE_IDLE;
        tap_progress_elapsed_units = 0UL;
    }

    if (!sdcard_file_open_read(path))
    {
        tap_set_error_P(PSTR("TAP OPEN"), TAP_PLAYBACK_IO_ERROR);
        return false;
    }
    tap_total_file_bytes = sdcard_file_size();
    if (tap_total_file_bytes == 0UL)
    {
        tap_set_error_P(PSTR("EMPTY TAP"), TAP_PLAYBACK_BAD_FILE);
        return false;
    }
    if (!tap_scan_progress_total()) return false;
    if (!tap_parse_block(true)) return false;

    tap_state = TAP_PLAYBACK_READY;
    return true;
}

bool tap_playback_start(void)
{
    if (tap_state != TAP_PLAYBACK_READY) return false;

    tap_set_current_block(tap_block_length,
                          tap_current_flag,
                          tap_current_is_last);
    mz_read_set_fast(false);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
        TCCR3A = 0U;
        TCCR3B = 0U;
        TCNT3 = 0U;
        TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
        tap_phase = TAP_PHASE_PILOT_LOW;
        tap_progress_elapsed_units = 0UL;
        tap_gap_current_progress_units = 0U;
        tap_state = TAP_PLAYBACK_RUNNING;
        timer3b_owner_set_from_isr(TIMER3B_OWNER_TAP);
        monitor_set_tape_activity_from_isr(true);
        TCCR3B = _BV(CS30);
        tap_schedule_first_from_isr(tap_timing.pilot_half_ticks);
        TIMSK3 |= _BV(OCIE3B);
    }
    mz_sense_set(false);
    return true;
}

bool tap_playback_pause(void)
{
    uint16_t current_remaining;

    if (tap_state != TAP_PLAYBACK_RUNNING) return false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        current_remaining = (uint16_t)(OCR3B - TCNT3);
        if (((TIFR3 & _BV(OCF3B)) != 0U) || (current_remaining == 0U))
        {
            current_remaining = 1U;
        }
        tap_paused_current_ticks = current_remaining;
        tap_stop_timer_from_isr();
        tap_state = TAP_PLAYBACK_PAUSED;
    }
    return true;
}

bool tap_playback_resume(void)
{
    if (tap_state != TAP_PLAYBACK_PAUSED) return false;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
        TCCR3A = 0U;
        TCCR3B = 0U;
        TCNT3 = 0U;
        TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
        tap_state = TAP_PLAYBACK_RUNNING;
        timer3b_owner_set_from_isr(TIMER3B_OWNER_TAP);
        monitor_set_tape_activity_from_isr(tap_phase != TAP_PHASE_GAP_LOW);
        TCCR3B = _BV(CS30);
        tap_schedule_first_from_isr(tap_paused_current_ticks);
        tap_paused_current_ticks = 0U;
        TIMSK3 |= _BV(OCIE3B);
    }
    return true;
}

void tap_playback_stop(void)
{
    tap_stop_timer_from_foreground(true);
    sdcard_file_close();
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tap_read_sequence = 0U;
        tap_write_sequence = 0U;
        tap_source_finished = 1U;
        tap_state = TAP_PLAYBACK_STOPPED;
        tap_phase = TAP_PHASE_IDLE;
        tap_need_next_block = 0U;
        tap_next_block_ready = 0U;
        tap_progress_elapsed_units = 0UL;
        tap_gap_chunks_remaining = 0U;
        tap_gap_tail_ticks = 0U;
        tap_gap_current_progress_units = 0U;
    }
    tap_source_remaining = 0U;
    tap_total_file_bytes = 0UL;
    tap_progress_total_units = 0UL;
    tap_block_length = 0U;
    tap_block_bytes_emitted = 0U;
    tap_current_byte = 0U;
    tap_bit_mask = 0x80U;
    tap_paused_current_ticks = 0U;
    tap_error_text[0] = '\0';
    mz_sense_set(true);
}

void tap_playback_service(void)
{
    tap_playback_state_t state = tap_playback_get_state();

    if (state == TAP_PLAYBACK_RUNNING)
    {
        uint8_t need_next;
        uint8_t next_ready;

        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            need_next = tap_need_next_block;
            next_ready = tap_next_block_ready;
        }

        if ((need_next != 0U) && (next_ready == 0U))
        {
            if ((tap_source_remaining != 0U) || (tap_used_snapshot() != 0U))
            {
                tap_set_error_P(PSTR("TAP FIFO"), TAP_PLAYBACK_IO_ERROR);
            }
            else
            {
                (void)tap_parse_block(false);
            }
        }

        if ((tap_playback_get_state() == TAP_PLAYBACK_RUNNING) &&
            (tap_source_remaining != 0U) &&
            (tap_used_snapshot() <= TAP_REFILL_RESERVE))
        {
            (void)tap_refill_once();
        }
        state = tap_playback_get_state();
    }

    if ((state == TAP_PLAYBACK_FINISHED) ||
        (state == TAP_PLAYBACK_UNDERRUN) ||
        (state == TAP_PLAYBACK_IO_ERROR) ||
        (state == TAP_PLAYBACK_BAD_FILE))
    {
        if (tap_error_text[0] == '\0')
        {
            if (state == TAP_PLAYBACK_UNDERRUN)
                flash_text_copy(tap_error_text, sizeof(tap_error_text), PSTR("UNDERRUN"));
            else if (state == TAP_PLAYBACK_BAD_FILE)
                flash_text_copy(tap_error_text, sizeof(tap_error_text), PSTR("TAP FIFO"));
        }
        sdcard_file_close();
    }
}

tap_playback_state_t tap_playback_get_state(void)
{
    uint8_t state;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        state = tap_state;
    }
    return (tap_playback_state_t)state;
}

const char *tap_playback_get_error_text(void)
{
    return tap_error_text;
}

uint8_t tap_playback_get_progress_percent(void)
{
    uint32_t emitted;
    uint32_t scaled_emitted;
    uint32_t scaled_total;
    uint32_t percent;

    if (tap_playback_get_state() == TAP_PLAYBACK_FINISHED) return 100U;
    if (tap_progress_total_units == 0UL) return 0U;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        emitted = tap_progress_elapsed_units;
    }
    if (emitted >= tap_progress_total_units) return 99U;

    /* Floor the value so rounding cannot show 100 % before the final
       terminator. Scale large totals before x100 to avoid overflow. */
    scaled_emitted = emitted;
    scaled_total = tap_progress_total_units;
    while (scaled_total > (0xFFFFFFFFUL / 100UL))
    {
        scaled_emitted >>= 1U;
        scaled_total = (scaled_total >> 1U) + (scaled_total & 1UL);
    }
    percent = (scaled_emitted * 100UL) / scaled_total;
    return (percent >= 100UL) ? 99U : (uint8_t)percent;
}

uint8_t tap_playback_get_buffer_fill_percent(void)
{
    uint16_t used = tap_used_snapshot();
    uint8_t need_next;
    uint8_t next_ready;
    uint32_t remaining;
    uint32_t target;
    uint32_t percent;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        need_next = tap_need_next_block;
        next_ready = tap_next_block_ready;
    }

    /* During the short inter-block handoff do not report the previous block as
       buffered if the next block has not been prepared yet. */
    if ((need_next != 0U) && (next_ready == 0U) && (used == 0U))
        return 0U;

    if (used > TAP_FIFO_CAPACITY) return 0U;

    /* Readiness is relative to useful future bytes, not the physical FIFO
       size.  For a short block, 100 % means its complete remainder is in RAM. */
    remaining = (uint32_t)used + (uint32_t)tap_source_remaining;
    if (remaining == 0UL) return 100U;

    target = (remaining < (uint32_t)TAP_FIFO_CAPACITY) ?
        remaining : (uint32_t)TAP_FIFO_CAPACITY;
    percent = ((uint32_t)used * 100UL) / target;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}

void tap_playback_timer3_compb_from_isr(void)
{
    if (tap_state != TAP_PLAYBACK_RUNNING)
    {
        tap_stop_timer_from_isr();
        return;
    }

    switch ((tap_phase_t)tap_phase)
    {
        case TAP_PHASE_PILOT_LOW:
            mz_read_set_fast_from_isr(1U);
            tap_phase = TAP_PHASE_PILOT_HIGH;
            tap_schedule_next_from_isr(tap_timing.pilot_half_ticks);
            break;

        case TAP_PHASE_PILOT_HIGH:
            tap_progress_add_from_isr(tap_progress_pilot_pulse_units);
            if (tap_pilot_pulses_remaining != 0U)
                tap_pilot_pulses_remaining--;
            mz_read_set_fast_from_isr(0U);
            if (tap_pilot_pulses_remaining != 0U)
            {
                tap_phase = TAP_PHASE_PILOT_LOW;
                tap_schedule_next_from_isr(tap_timing.pilot_half_ticks);
            }
            else
            {
                tap_phase = TAP_PHASE_SYNC_LOW;
                tap_schedule_next_from_isr(tap_timing.sync_low_ticks);
            }
            break;

        case TAP_PHASE_SYNC_LOW:
            mz_read_set_fast_from_isr(1U);
            tap_phase = TAP_PHASE_SYNC_HIGH;
            tap_schedule_next_from_isr(tap_timing.sync_high_ticks);
            break;

        case TAP_PHASE_SYNC_HIGH:
            tap_progress_add_from_isr(tap_progress_sync_units);
            mz_read_set_fast_from_isr(0U);
            tap_current_bit_ticks = tap_bit_ticks_from_isr();
            tap_phase = TAP_PHASE_DATA_LOW;
            tap_schedule_next_from_isr(tap_current_bit_ticks);
            break;

        case TAP_PHASE_DATA_LOW:
            mz_read_set_fast_from_isr(1U);
            tap_phase = TAP_PHASE_DATA_HIGH;
            tap_schedule_next_from_isr(tap_current_bit_ticks);
            break;

        case TAP_PHASE_DATA_HIGH:
            tap_progress_add_from_isr(tap_progress_bit_units);
            tap_bit_mask >>= 1U;
            if (tap_bit_mask != 0U)
            {
                mz_read_set_fast_from_isr(0U);
                tap_current_bit_ticks = tap_bit_ticks_from_isr();
                tap_phase = TAP_PHASE_DATA_LOW;
                tap_schedule_next_from_isr(tap_current_bit_ticks);
                break;
            }

            tap_block_bytes_emitted++;
            if (tap_block_bytes_emitted >= tap_block_length)
            {
                mz_read_set_fast_from_isr(0U);
                tap_phase = TAP_PHASE_TERMINATOR_LOW;
                tap_schedule_next_from_isr(tap_timing.pilot_half_ticks);
                break;
            }

            if (!tap_pop_from_isr(&tap_current_byte))
            {
                tap_fail_from_isr((tap_source_finished != 0U) ?
                    TAP_PLAYBACK_BAD_FILE : TAP_PLAYBACK_UNDERRUN);
                break;
            }
            tap_bit_mask = 0x80U;
            mz_read_set_fast_from_isr(0U);
            tap_current_bit_ticks = tap_bit_ticks_from_isr();
            tap_phase = TAP_PHASE_DATA_LOW;
            tap_schedule_next_from_isr(tap_current_bit_ticks);
            break;

        case TAP_PHASE_TERMINATOR_LOW:
            tap_progress_add_from_isr(tap_progress_terminator_units);
            if (tap_current_is_last)
            {
                tap_stop_timer_from_isr();
                mz_read_set_fast_from_isr(0U);
                tap_phase = TAP_PHASE_IDLE;
                tap_state = TAP_PLAYBACK_FINISHED;
                break;
            }

            monitor_set_tape_activity_from_isr(false);
            tap_need_next_block = 1U;
            tap_next_block_ready = 0U;
            if (tap_current_flag == TAP_HEADER_FLAG)
            {
                tap_gap_chunks_remaining = TAP_HEADER_GAP_CHUNKS;
                tap_gap_tail_ticks = TAP_HEADER_GAP_TAIL_TICKS;
            }
            else
            {
                tap_gap_chunks_remaining = TAP_DATA_GAP_CHUNKS;
                tap_gap_tail_ticks = TAP_DATA_GAP_TAIL_TICKS;
            }
            tap_phase = TAP_PHASE_GAP_LOW;
            (void)tap_schedule_gap_piece_from_isr();
            break;

        case TAP_PHASE_GAP_LOW:
            tap_progress_add_from_isr(tap_gap_current_progress_units);
            if (tap_schedule_gap_piece_from_isr()) break;
            if (tap_next_block_ready == 0U)
            {
                tap_fail_from_isr(TAP_PLAYBACK_UNDERRUN);
                break;
            }
            tap_activate_pending_from_isr();
            monitor_set_tape_activity_from_isr(true);
            mz_read_set_fast_from_isr(0U);
            tap_phase = TAP_PHASE_PILOT_LOW;
            tap_schedule_next_from_isr(tap_timing.pilot_half_ticks);
            break;

        case TAP_PHASE_IDLE:
        default:
            tap_fail_from_isr(TAP_PLAYBACK_BAD_FILE);
            break;
    }
}
