#include "mzf_playback.h"
#include "timer3b_owner.h"
#include "mzf_loader.h"

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
#include "../streams/cmt_mode_scratch.h"
#include "../streams/cmt_error_buffers.h"
#include "../formats/mz_tape_profiles.h"
#include "../formats/mzi_sidecar.h"
#include "../formats/mz_title.h"

#if !defined(TIMER3_COMPB_vect) || !defined(TIMER3_OVF_vect)
#error "MZF playback requires Timer3 compare-B and overflow on ATmega2560"
#endif

/*
    The binary MZF/M12 header is 128 bytes. MZT is handled as a sequence of
    header + declared data-block records. Record boundaries use the MZ
    MOTOR/SENSE transport handshake when the selected loader requires it.
*/
#define MZF_HEADER_BYTES 128U
#define MZF_HEADER_DATA_LENGTH_OFFSET 0x12U

/*
    MZ-800 monitor waveform:

      short / logical 0: HIGH 250 us, LOW 250 us  (500 us total)
      long  / logical 1: HIGH 500 us, LOW 500 us (1000 us total)

    The MZ-800 tape framing sends a 6,400-short-pulse leader before both the
    header and data sections.
*/
#define MZF_TICKS_PER_US ((uint16_t)(F_CPU / 1000000UL))
#define MZF_US_TO_TICKS(us) ((uint16_t)((uint32_t)(us) * MZF_TICKS_PER_US))
#define MZF_SHORT_HIGH_TICKS MZF_US_TO_TICKS(250U)
#define MZF_SHORT_LOW_TICKS  MZF_US_TO_TICKS(250U)
#define MZF_LONG_HIGH_TICKS  MZF_US_TO_TICKS(500U)
#define MZF_LONG_LOW_TICKS   MZF_US_TO_TICKS(500U)
/* NORMAL without a loader keeps the ROM framing and scales both symmetric
   pulse halves. Division in timer ticks preserves the exact 1:2 / 1:3
   ratios without adding software-edge correction loops. */
/* Fractional-tick NORMAL profiles derived from 44.1 kHz pulse periods. */
#define MZF_NORMAL_1_2_SHORT_TICKS ((uint16_t)2178U) /* 136.125 us */
#define MZF_NORMAL_1_2_LONG_TICKS  ((uint16_t)4425U) /* 276.563 us */
#define MZF_NORMAL_1_3_SHORT_TICKS ((uint16_t)1811U) /* 113.188 us */
#define MZF_NORMAL_1_3_LONG_TICKS  ((uint16_t)3265U) /* 204.063 us */
#define MZF_IC_1_4_SHORT_HIGH_TICKS MZF_US_TO_TICKS(112U)
#define MZF_IC_1_4_SHORT_LOW_TICKS  MZF_US_TO_TICKS(80U)
#define MZF_IC_1_4_LONG_HIGH_TICKS  MZF_US_TO_TICKS(176U)
#define MZF_IC_1_4_LONG_LOW_TICKS   MZF_US_TO_TICKS(160U)
#define MZF_IC_1_3_SHORT_HIGH_TICKS MZF_US_TO_TICKS(112U)
#define MZF_IC_1_3_SHORT_LOW_TICKS  MZF_US_TO_TICKS(96U)
#define MZF_IC_1_3_LONG_HIGH_TICKS  MZF_US_TO_TICKS(224U)
#define MZF_IC_1_3_LONG_LOW_TICKS   MZF_US_TO_TICKS(192U)
/* MZ-700 1Z-009A FAST3 runs DLY3 from RAM and samples input about 97 us
   after the edge. Keep both halves symmetric: short remains below the sample
   point and long is exactly twice the short interval. */
#define MZF_MZ700_3X_SHORT_TICKS MZF_US_TO_TICKS(80U)
#define MZF_MZ700_3X_LONG_TICKS  MZF_US_TO_TICKS(160U)
#define MZF_IC_1_2_SHORT_HIGH_TICKS MZF_US_TO_TICKS(144U)
#define MZF_IC_1_2_SHORT_LOW_TICKS  MZF_US_TO_TICKS(112U)
#define MZF_IC_1_2_LONG_HIGH_TICKS  MZF_US_TO_TICKS(256U)
#define MZF_IC_1_2_LONG_LOW_TICKS   MZF_US_TO_TICKS(224U)
#define MZF_TC_1_3_SHORT_HIGH_TICKS MZF_US_TO_TICKS(112U)
#define MZF_TC_1_3_SHORT_LOW_TICKS  MZF_US_TO_TICKS(112U)
#define MZF_TC_1_3_LONG_HIGH_TICKS  MZF_US_TO_TICKS(204U)
#define MZF_TC_1_3_LONG_LOW_TICKS   MZF_US_TO_TICKS(204U)
#define MZF_TC_1_4_SHORT_HIGH_TICKS MZF_IC_1_4_SHORT_HIGH_TICKS
#define MZF_TC_1_4_SHORT_LOW_TICKS  MZF_IC_1_4_SHORT_LOW_TICKS
#define MZF_TC_1_4_LONG_HIGH_TICKS  MZF_IC_1_4_LONG_HIGH_TICKS
#define MZF_TC_1_4_LONG_LOW_TICKS   MZF_IC_1_4_LONG_LOW_TICKS
#define MZF_TC_1_2_SHORT_HIGH_TICKS MZF_US_TO_TICKS(144U)
#define MZF_TC_1_2_SHORT_LOW_TICKS  MZF_US_TO_TICKS(144U)
#define MZF_TC_1_2_LONG_HIGH_TICKS  MZF_US_TO_TICKS(288U)
#define MZF_TC_1_2_LONG_LOW_TICKS   MZF_US_TO_TICKS(288U)

#define MZF_MZ800_LONG_GAP_SHORT_PULSES 6344U
#define MZF_MZ800_SHORT_GAP_SHORT_PULSES 6344U
#define MZF_MZ800_LONG_MARK_LONG_PULSES 40U
#define MZF_MZ800_LONG_MARK_SHORT_PULSES 40U
#define MZF_MZ800_SHORT_MARK_LONG_PULSES 20U
#define MZF_MZ800_SHORT_MARK_SHORT_PULSES 20U
#define MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES 2U
#define MZF_MZ800_TRAILING_LONG_PULSES 2U
#define MZF_TC_LOADER_TRAILING_SHORT_PULSES 98U

#define MZF_FIFO_BYTES WAV_SAMPLE_STREAM_BUFFER_BYTES
#define MZF_FIFO_CAPACITY (MZF_FIFO_BYTES - 1U)
#define MZF_FIFO_MASK (MZF_FIFO_BYTES - 1U)
#define MZF_REFILL_BLOCK WAV_SAMPLE_STREAM_REFILL_BLOCK
#define MZF_REFILL_RESERVE (MZF_FIFO_CAPACITY - MZF_REFILL_BLOCK)
/*
    Header/data boundaries normally use an MZ MOTOR off/on cycle.  Some
    transports, and PLAY CTRL / MANUAL, leave MOTOR high; do not strand a
    binary image after its 128-byte header in that case.
*/
#define MZF_BOUNDARY_AUTO_CONTINUE_MS 120U
#define MZF_IC_TURBO_START_DELAY_MS 345U
#define MZF_MZ700_FAST3_START_DELAY_MS 400U
#define MZF_TC_TURBO_START_DELAY_MS 110U
#define MZF_IC_TURBO_GAP_SHORT_PULSES 5500U
#define MZF_TC_1_4_TURBO_GAP_SHORT_PULSES 5500U
#define MZF_TC_1_3_TURBO_GAP_SHORT_PULSES 15130U
#define MZF_TC_1_2_TURBO_GAP_SHORT_PULSES 11239U
#define MZF_IC_TURBO_MARK_LONG_PULSES 20U
#define MZF_IC_TURBO_MARK_SHORT_PULSES 20U
#define MZF_IC_TURBO_MARK_FINAL_LONG_PULSES 2U

#if ((MZF_FIFO_BYTES & (MZF_FIFO_BYTES - 1U)) != 0U)
#error "MZF FIFO needs a power-of-two size"
#endif

typedef enum
{
    MZF_STAGE_NONE = 0,
    MZF_STAGE_HEADER,
    MZF_STAGE_DATA,
    MZF_STAGE_TAPE_TURBO_DATA,
    MZF_STAGE_ULTRAFAST
} mzf_stage_t;

typedef enum
{
    MZF_STEP_BEGIN = 0,
    MZF_STEP_GAP,
    MZF_STEP_TAPE_MARK_LONG,
    MZF_STEP_TAPE_MARK_SHORT,
    MZF_STEP_TAPE_MARK_FINAL,
    MZF_STEP_BYTE_LOAD,
    MZF_STEP_BYTE_BITS,
    MZF_STEP_BYTE_STOP,
    MZF_STEP_CHECKSUM_LOAD,
    MZF_STEP_CHECKSUM_BITS,
    MZF_STEP_CHECKSUM_STOP,
    MZF_STEP_TRAILING_LONGS,
    MZF_STEP_DUPLICATE_GAP,
    MZF_STEP_BOUNDARY
} mzf_normal_step_t;

typedef enum
{
    MZF_PWM_TERMINAL_NONE = 0,
    MZF_PWM_TERMINAL_BOUNDARY,
    MZF_PWM_TERMINAL_FINISHED
} mzf_pwm_terminal_t;

static volatile uint8_t mzf_state = MZF_PLAYBACK_STOPPED;
#define mzf_error_text cmt_playback_backend_error

static file_format_t mzf_format = FILE_FORMAT_UNKNOWN;
/* The PLAY controller owns this path for the complete prepared session. */
static const char *mzf_source_path = NULL;
/* Manual selection applies to every MZT record. AUTO resolves RECORD=n from
   the same-basename .MTI, falling back to NORMAL 1:1 per missing record. */
static loader_mode_t mzf_requested_loader_mode = LOADER_MODE_NORMAL_1_1;
/* PLAY CTRL policy belongs to the controller, but the exact header/data
   boundary is owned here. This flag prevents MANUAL mode from being stranded
   by a physical MOTOR LOW which the controller intentionally ignores. */
static bool mzf_motor_control_enabled = true;
static uint16_t mzf_mzt_record_index = 0U;
static uint16_t mzf_mzt_record_count = 0U;
static loader_mode_t mzf_mzt_record_loader_mode = LOADER_MODE_NORMAL_1_1;
static bool mzf_mzt_record_loader_from_sidecar = false;
/* Human-readable title of the current logical MZT record.  The MZF name field
   is 17 bytes; common native titles are printable in the ASCII-compatible
   range used here. */
static char mzf_mzt_record_title[18];
static uint8_t mzf_normal_speed_divisor = 1U;
static bool mzf_native_mz700 = false;
static uint16_t mzf_profile_short_high_ticks = MZF_SHORT_HIGH_TICKS;
static uint16_t mzf_profile_short_low_ticks = MZF_SHORT_LOW_TICKS;
static uint16_t mzf_profile_long_high_ticks = MZF_LONG_HIGH_TICKS;
static uint16_t mzf_profile_long_low_ticks = MZF_LONG_LOW_TICKS;
static uint16_t mzf_profile_header_leader = MZF_MZ800_LONG_GAP_SHORT_PULSES;
static uint16_t mzf_profile_data_leader = MZF_MZ800_SHORT_GAP_SHORT_PULSES;
static uint8_t mzf_profile_header_mark_long = MZF_MZ800_LONG_MARK_LONG_PULSES;
static uint8_t mzf_profile_header_mark_short = MZF_MZ800_LONG_MARK_SHORT_PULSES;
static uint8_t mzf_profile_data_mark_long = MZF_MZ800_SHORT_MARK_LONG_PULSES;
static uint8_t mzf_profile_data_mark_short = MZF_MZ800_SHORT_MARK_SHORT_PULSES;
static uint8_t mzf_profile_final_mark_long = MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES;
static uint16_t mzf_profile_duplicate_gap = 0U;

/* MZF ignores the UI WAV invert setting. TC turbo uses the opposite READ
   phase; IC and native/UL modes use direct polarity. */
static bool mzf_wave_invert_signal = false;

/* PLAY and RECORD never overlap. During MZF playback the second RECORD
   staging sector is idle, so its first 128 bytes hold the active header. */
#define mzf_header cmt_mode_scratch.edge_record_stage_bytes
static volatile uint8_t mzf_header_offset = 0U;

static volatile uint16_t mzf_fifo_read_sequence = 0U;
static volatile uint16_t mzf_fifo_write_sequence = 0U;
static volatile uint8_t mzf_fifo_source_finished = 1U;

static uint32_t mzf_file_size = 0UL;
static uint32_t mzf_record_data_length = 0UL;
/* Absolute end position of the current declared data record. */
static uint32_t mzf_record_data_file_end = 0UL;
static uint32_t mzf_record_data_file_start = 0UL;
static uint32_t mzf_record_data_read = 0UL;
static uint32_t mzf_original_data_offset = 0UL;
static uint32_t mzf_original_data_length = 0UL;
/* True only when the original tape-turbo payload has already been prefetched
   into the shared FIFO and sdcard_file_position() points immediately after
   the bytes currently buffered.  This lets the header time build real
   read-ahead instead of discarding it at the header/data boundary. */
static bool mzf_tape_turbo_payload_prepared = false;
/* Calculated in foreground before playback; no progress counters are kept in ISR. */
static uint32_t mzf_total_duration_ms = 0UL;
/* Exact generated duration in 0.5 ms units. Keep it separate so UI speed
   scaling cannot divide an already-profiled 1:2/1:3 result again. */
static uint32_t mzf_exact_duration_half_ms = 0UL;

static mzf_stage_t mzf_stage = MZF_STAGE_NONE;
static mzf_normal_step_t mzf_normal_step = MZF_STEP_BEGIN;
static uint16_t mzf_normal_loop = 0U;
static uint32_t mzf_normal_bytes_remaining = 0UL;
static uint16_t mzf_normal_checksum = 0U;
static uint8_t mzf_normal_data = 0U;
static uint8_t mzf_normal_bits_remaining = 0U;
static uint8_t mzf_normal_checksum_byte_index = 0U;
static bool mzf_normal_header_preamble = true;
static volatile uint8_t mzf_native_copy_index = 0U;
static volatile bool mzf_native_repeat_refill_requested = false;
static volatile bool mzf_native_repeat_refill_ready = false;

/* Written by Timer3 ISR and read in foreground. */
static volatile bool mzf_boundary_waiting = false;
static volatile uint8_t mzf_motor_low_seen = 0U;
static bool mzf_paused_mid_pulse = false;
static bool mzf_pwm_low_first = false;
static bool mzf_pwm_bootstrap_pending = false;
static bool mzf_pwm_stop_pending = false;
static bool mzf_pwm_next_valid = false;
static bool mzf_pwm_paused_com_connected = false;
static uint8_t mzf_pwm_paused_resume_level = 0U;
static uint16_t mzf_pwm_current_compare_ticks = 1U;
static uint16_t mzf_pwm_next_compare_ticks = 1U;
static volatile uint8_t mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;

/* Foreground-only fallback timer for a missing/short MOTOR boundary. */
static bool mzf_boundary_auto_timer_armed = false;
static uint16_t mzf_boundary_auto_start_ms = 0U;

/* MZ700 FAST3 requires a 400 ms READ-low gap before the accelerated data
   leader. The gap is service-driven so foreground transport, keypad and SD
   handling remain responsive. */
static bool mzf_fast3_start_delay_armed = false;
static uint16_t mzf_fast3_start_delay_started_ms = 0U;

static void mzf_set_error_P(PGM_P text, mzf_playback_state_t state)
{
    flash_text_copy(mzf_error_text, sizeof(mzf_error_text), text);
    mzf_state = (uint8_t)state;
}

static void mzf_set_error(const char *text, mzf_playback_state_t state)
{
    if (text == NULL)
    {
        flash_text_copy(mzf_error_text, sizeof(mzf_error_text), PSTR("MZF ERROR"));
    }
    else
    {
        strncpy(mzf_error_text, text, sizeof(mzf_error_text) - 1U);
        mzf_error_text[sizeof(mzf_error_text) - 1U] = '\0';
    }
    mzf_state = (uint8_t)state;
}

static void mzf_set_error_from_isr_P(PGM_P text, mzf_playback_state_t state)
{
    flash_text_copy(mzf_error_text, sizeof(mzf_error_text), text);
    mzf_state = (uint8_t)state;
    mz_read_set_fast_from_isr(0U);
}

static void mzf_stop_timer_from_isr_with_read(uint8_t read_level)
{
    TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
    TCCR3A = 0U;
    TCCR3B = 0U;
    TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
    if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_MZF)
    {
        timer3b_owner_set_from_isr(TIMER3B_OWNER_NONE);
    }
    mzf_pwm_bootstrap_pending = false;
    mzf_pwm_stop_pending = false;
    mzf_pwm_next_valid = false;
    mz_read_set_fast_from_isr(read_level);
    monitor_set_tape_activity_from_isr(false);
}

static void mzf_stop_timer_from_isr(void)
{
    mzf_stop_timer_from_isr_with_read(0U);
}

static void mzf_stop_timer_from_foreground(bool force_low)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
        TCCR3A = 0U;
        TCCR3B = 0U;
        TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
        if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_MZF)
        {
            timer3b_owner_set_from_isr(TIMER3B_OWNER_NONE);
        }
        mzf_pwm_bootstrap_pending = false;
        mzf_pwm_stop_pending = false;
        mzf_pwm_next_valid = false;
    }
    if (force_low)
    {
        mz_read_set(false);
    }
    monitor_disable();
}

static uint16_t mzf_pwm_compare_ticks(uint16_t high_ticks,
                                      uint16_t low_ticks)
{
    uint16_t compare = mzf_pwm_low_first ? low_ticks : high_ticks;
    return (compare == 0U) ? 1U : compare;
}

static void mzf_pwm_write_next_from_isr(uint16_t high_ticks,
                                        uint16_t low_ticks)
{
    uint16_t total_ticks = (uint16_t)(high_ticks + low_ticks);
    uint16_t compare_ticks = mzf_pwm_compare_ticks(high_ticks, low_ticks);

    if (total_ticks < 2U) total_ticks = 2U;
    OCR3A = (uint16_t)(total_ticks - 1U);
    OCR3B = compare_ticks;
    mzf_pwm_next_compare_ticks = compare_ticks;
    mzf_pwm_next_valid = true;
}

/*
    READ is Arduino pin 2 = PE4/OC3B on the ATmega2560.  Fast-PWM mode 15 uses
    OCR3A as the double-buffered period and OCR3B as the double-buffered phase
    boundary.  Both physical READ edges are therefore produced by Timer3;
    software only prepares a later complete pulse.
*/
static void mzf_pwm_start_first_from_isr(uint16_t high_ticks,
                                         uint16_t low_ticks,
                                         bool physical_low_first)
{
    uint16_t total_ticks = (uint16_t)(high_ticks + low_ticks);
    uint8_t com_bits;

    if (total_ticks < 2U) total_ticks = 2U;
    mzf_pwm_low_first = physical_low_first;
    mzf_pwm_current_compare_ticks = mzf_pwm_compare_ticks(high_ticks, low_ticks);
    mzf_pwm_next_compare_ticks = 1U;
    mzf_pwm_next_valid = false;
    mzf_pwm_bootstrap_pending = true;
    mzf_pwm_stop_pending = false;
    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;

    TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
    TCCR3A = 0U;
    TCCR3B = 0U;
    OCR3A = (uint16_t)(total_ticks - 1U);
    OCR3B = mzf_pwm_current_compare_ticks;
    /* Start at TOP so the first timer clock performs a real BOTTOM action
       and establishes the OC3B start level before the measured phase. */
    TCNT3 = OCR3A;
    TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));

    mz_read_set_fast_from_isr(physical_low_first ? 0U : 1U);
    com_bits = (uint8_t)(_BV(COM3B1) |
                         (physical_low_first ? _BV(COM3B0) : 0U));
    timer3b_owner_set_from_isr(TIMER3B_OWNER_MZF);
    monitor_set_tape_activity_from_isr(true);
    TCCR3A = (uint8_t)(com_bits | _BV(WGM31) | _BV(WGM30));
    TIMSK3 |= _BV(OCIE3B);
    TCCR3B = (uint8_t)(_BV(WGM33) | _BV(WGM32) | _BV(CS30));
}

static void mzf_pwm_disconnect_output_from_isr(void)
{
    TCCR3A &= (uint8_t)~(_BV(COM3B1) | _BV(COM3B0));
    TIMSK3 &= (uint8_t)~_BV(OCIE3B);
}

static void mzf_pwm_arm_terminal_from_isr(void)
{
    mzf_pwm_stop_pending = true;
    TIMSK3 |= (uint8_t)(_BV(OCIE3B) | _BV(TOIE3));
}

static void mzf_pwm_finish_terminal_from_isr(void)
{
    uint8_t terminal = mzf_pwm_terminal_pending;
    bool hold_ul700_start =
        (terminal == MZF_PWM_TERMINAL_BOUNDARY) &&
        (mzf_stage == MZF_STAGE_HEADER) &&
        mzf_loader_is_mz700_ul();

    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;

    /*
       UL700 keeps READ high at this boundary. Classic UL and UL800 finish with
       READ low before control passes to their loader handoff.
    */
    mzf_stop_timer_from_isr_with_read(hold_ul700_start ? 1U : 0U);
    if (terminal == MZF_PWM_TERMINAL_BOUNDARY)
    {
        mzf_boundary_waiting = true;
    }
    else if (terminal == MZF_PWM_TERMINAL_FINISHED)
    {
        mzf_state = MZF_PLAYBACK_FINISHED;
    }
}

static void mzf_pwm_pause_from_foreground(void)
{
    uint16_t counter;
    bool first_phase;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        counter = TCNT3;
        first_phase = counter < mzf_pwm_current_compare_ticks;
        mzf_pwm_paused_resume_level = mzf_pwm_low_first ?
            (first_phase ? 0U : 1U) : (first_phase ? 1U : 0U);
        mzf_pwm_paused_com_connected =
            (TCCR3A & _BV(COM3B1)) != 0U;

        TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
        TCCR3B &= (uint8_t)~(_BV(CS32) | _BV(CS31) | _BV(CS30));
        TCCR3A &= (uint8_t)~(_BV(COM3B1) | _BV(COM3B0));
        TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
        mzf_paused_mid_pulse = true;
        mzf_state = MZF_PLAYBACK_PAUSED;
    }
    mz_read_set_fast(false);
    monitor_disable();
}

static void mzf_pwm_resume_from_foreground(void)
{
    uint8_t com_bits = 0U;

    mz_read_set_fast(mzf_pwm_paused_resume_level != 0U);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (mzf_pwm_paused_com_connected)
        {
            com_bits = (uint8_t)(_BV(COM3B1) |
                       (mzf_pwm_low_first ? _BV(COM3B0) : 0U));
        }

        timer3b_owner_set_from_isr(TIMER3B_OWNER_MZF);
        monitor_set_tape_activity_from_isr(true);
        TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
        TCCR3A = (uint8_t)(com_bits | _BV(WGM31) | _BV(WGM30));
        TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
        if (mzf_pwm_paused_com_connected)
        {
            TIMSK3 |= _BV(OCIE3B);
        }
        if (!mzf_pwm_bootstrap_pending || mzf_pwm_stop_pending)
        {
            TIMSK3 |= _BV(TOIE3);
        }
        TCCR3B = (uint8_t)(_BV(WGM33) | _BV(WGM32) | _BV(CS30));
        mzf_paused_mid_pulse = false;
    }
}

static uint16_t mzf_fifo_used_snapshot(void)
{
    uint16_t read_sequence;
    uint16_t write_sequence;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        read_sequence = mzf_fifo_read_sequence;
        write_sequence = mzf_fifo_write_sequence;
    }
    return (uint16_t)(write_sequence - read_sequence);
}

static void mzf_fifo_reset(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        mzf_fifo_read_sequence = 0U;
        mzf_fifo_write_sequence = 0U;
        mzf_fifo_source_finished = 0U;
    }
}

static bool mzf_fifo_pop_from_isr(uint8_t *value)
{
    uint16_t read_sequence = mzf_fifo_read_sequence;

    if ((value == NULL) || (read_sequence == mzf_fifo_write_sequence))
    {
        return false;
    }

    *value = wav_sample_stream_isr_bytes[read_sequence & MZF_FIFO_MASK];
    mzf_fifo_read_sequence = (uint16_t)(read_sequence + 1U);
    return true;
}

static bool mzf_refill_data_once(void)
{
    uint16_t used;
    uint16_t free_bytes;
    uint16_t request;
    int16_t received;
    uint16_t write_sequence;
    uint8_t *work;

    if (mzf_record_data_read >= mzf_record_data_length)
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            mzf_fifo_source_finished = 1U;
        }
        return true;
    }

    used = mzf_fifo_used_snapshot();
    if (used > MZF_FIFO_CAPACITY)
    {
        mzf_set_error_P(PSTR("MZF FIFO"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    free_bytes = (uint16_t)(MZF_FIFO_CAPACITY - used);
    if (free_bytes == 0U)
    {
        return true;
    }

    request = free_bytes;
    if (request > MZF_REFILL_BLOCK)
    {
        request = MZF_REFILL_BLOCK;
    }
    if ((uint32_t)request > (mzf_record_data_length - mzf_record_data_read))
    {
        request = (uint16_t)(mzf_record_data_length - mzf_record_data_read);
    }

    work = wav_sample_stream_get_shared_work_buffer();
    received = sdcard_file_read(work, request);
    if (received < 0)
    {
        mzf_set_error_P(PSTR("MZF READ"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }
    if (received == 0)
    {
        mzf_set_error_P(PSTR("MZF SHORT"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    write_sequence = mzf_fifo_write_sequence;
    for (int16_t i = 0; i < received; ++i)
    {
        wav_sample_stream_isr_bytes[write_sequence & MZF_FIFO_MASK] = work[i];
        write_sequence = (uint16_t)(write_sequence + 1U);
    }

    mzf_record_data_read += (uint32_t)received;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        asm volatile("" ::: "memory");
        mzf_fifo_write_sequence = write_sequence;
        if (mzf_record_data_read >= mzf_record_data_length)
        {
            mzf_fifo_source_finished = 1U;
        }
    }
    return true;
}

static bool mzf_prefill_data(void)
{
    while (mzf_record_data_read < mzf_record_data_length)
    {
        uint16_t before = mzf_fifo_used_snapshot();
        if (!mzf_refill_data_once())
        {
            return false;
        }
        if (mzf_fifo_used_snapshot() == before)
        {
            break;
        }
    }
    return (mzf_record_data_length == 0UL) || (mzf_fifo_used_snapshot() != 0U);
}

static bool mzf_prepare_loader_block_data(void)
{
    uint8_t *work = wav_sample_stream_get_shared_work_buffer();
    uint16_t length = mzf_loader_build_loader(work, MZF_REFILL_BLOCK);
    uint16_t write_sequence = 0U;

    if ((length == 0U) || (length > MZF_FIFO_CAPACITY))
    {
        mzf_set_error_P(PSTR("LDR BLOCK"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    for (uint16_t i = 0U; i < length; ++i)
    {
        wav_sample_stream_isr_bytes[write_sequence & MZF_FIFO_MASK] = work[i];
        write_sequence = (uint16_t)(write_sequence + 1U);
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        mzf_fifo_read_sequence = 0U;
        mzf_fifo_write_sequence = write_sequence;
        mzf_fifo_source_finished = 1U;
    }

    mzf_record_data_length = length;
    mzf_record_data_read = length;
    mzf_record_data_file_end = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    return true;
}

static uint32_t mzf_header_data_length(void)
{
    return (uint32_t)mzf_header[MZF_HEADER_DATA_LENGTH_OFFSET] |
           ((uint32_t)mzf_header[MZF_HEADER_DATA_LENGTH_OFFSET + 1U] << 8U);
}

static void mzf_capture_mzt_record_title(void)
{
    if (mzf_format != FILE_FORMAT_MZT)
    {
        mzf_mzt_record_title[0] = '\0';
        return;
    }

    if (!mz_title_decode_display(&mzf_header[1U], 17U,
                                 mzf_mzt_record_title,
                                 sizeof(mzf_mzt_record_title)))
    {
        flash_text_copy(mzf_mzt_record_title,
                        sizeof(mzf_mzt_record_title), PSTR("RECORD"));
    }
}

static bool mzf_read_header_record(void)
{
    int16_t received;
    uint32_t remaining;

    if ((mzf_file_size - sdcard_file_position()) < MZF_HEADER_BYTES)
    {
        mzf_set_error_P(PSTR("MZT HEADER"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    received = sdcard_file_read(mzf_header, MZF_HEADER_BYTES);
    if (received != (int16_t)MZF_HEADER_BYTES)
    {
        if (received < 0)
        {
            mzf_set_error_P(PSTR("MZT READ"), MZF_PLAYBACK_IO_ERROR);
        }
        else
        {
            mzf_set_error_P(PSTR("MZT SHORT"), MZF_PLAYBACK_BAD_FILE);
        }
        return false;
    }

    mzf_record_data_length = mzf_header_data_length();
    remaining = mzf_file_size - sdcard_file_position();
    if (mzf_record_data_length > remaining)
    {
        mzf_set_error_P(PSTR("MZF LENGTH"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    mzf_record_data_file_start = sdcard_file_position();
    mzf_record_data_file_end = mzf_record_data_file_start +
                               mzf_record_data_length;
    mzf_header_offset = 0U;
    mzf_record_data_read = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_fifo_reset();
    return true;
}

static uint8_t mzf_popcount8(uint8_t value)
{
    uint8_t count = 0U;
    while (value != 0U)
    {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static uint8_t mzf_popcount16(uint16_t value)
{
    return (uint8_t)(mzf_popcount8((uint8_t)value) +
                     mzf_popcount8((uint8_t)(value >> 8U)));
}

static bool mzf_add_half_milliseconds(uint32_t *total, uint32_t amount)
{
    if (total == NULL) return false;
    if ((0xFFFFFFFFUL - *total) < amount)
    {
        *total = 0xFFFFFFFFUL;
        return false;
    }
    *total += amount;
    return true;
}

/* Compute one header/data frame from the exact pulse profile selected by
   the generator.  The checksum is the same wrapping one-bit count used by
   the ISR. */
static void mzf_set_short_pulse(uint16_t *high_ticks, uint16_t *low_ticks);
static void mzf_set_long_pulse(uint16_t *high_ticks, uint16_t *low_ticks);

static bool mzf_add_stage_duration(bool header_stage,
                                   uint32_t byte_count,
                                   uint32_t one_count,
                                   uint16_t checksum,
                                   uint32_t *half_milliseconds)
{
    {
        uint16_t short_high;
        uint16_t short_low;
        uint16_t long_high;
        uint16_t long_low;
        uint32_t gap_pulses;
        uint8_t copies;
        const uint32_t checksum_ones = mzf_popcount16(checksum);
        uint64_t encoded_short_pulses;
        uint64_t encoded_long_pulses;
        uint64_t short_pulses;
        uint64_t long_pulses;
        uint64_t stage_ticks;
        uint64_t stage_half_milliseconds;
        const uint32_t half_millisecond_ticks = F_CPU / 2000UL;

        mzf_set_short_pulse(&short_high, &short_low);
        mzf_set_long_pulse(&long_high, &long_low);

        const uint32_t short_period =
            (uint32_t)short_high + (uint32_t)short_low;
        gap_pulses = header_stage ? mzf_profile_header_leader :
                                    mzf_profile_data_leader;
        /* Direct MZ700 playback uses one checksummed copy. A non-zero profile
           separator enables a second copy for profiles that require it. */
        copies = (mzf_native_mz700 &&
                  (mzf_profile_duplicate_gap != 0U)) ? 2U : 1U;

        /* Data bits: zero=short, one=long.  Every byte has one long stop;
           the two checksum bytes have the same framing. */
        encoded_short_pulses =
            ((uint64_t)byte_count * 8ULL - one_count) +
            (16ULL - checksum_ones);
        encoded_long_pulses = one_count + byte_count + checksum_ones + 2ULL;
        short_pulses = (uint64_t)gap_pulses +
                       (header_stage ?
                            mzf_profile_header_mark_short :
                            mzf_profile_data_mark_short) +
                       ((uint64_t)copies * encoded_short_pulses) +
                       ((copies == 2U) ? mzf_profile_duplicate_gap : 0U);
        long_pulses = (header_stage ?
                           mzf_profile_header_mark_long :
                           mzf_profile_data_mark_long) +
                      mzf_profile_final_mark_long +
                      ((uint64_t)copies *
                       (encoded_long_pulses +
                        MZF_MZ800_TRAILING_LONG_PULSES));

        stage_ticks = short_pulses * short_period +
                      long_pulses *
                          ((uint32_t)long_high + (uint32_t)long_low);
        stage_half_milliseconds =
            (stage_ticks + (half_millisecond_ticks / 2UL)) /
            half_millisecond_ticks;
        if (stage_half_milliseconds > 0xFFFFFFFFULL)
        {
            *half_milliseconds = 0xFFFFFFFFUL;
            return false;
        }
        if (!mzf_add_half_milliseconds(
                &mzf_exact_duration_half_ms,
                (uint32_t)stage_half_milliseconds))
        {
            *half_milliseconds = 0xFFFFFFFFUL;
            return false;
        }
        return mzf_add_half_milliseconds(
            half_milliseconds, (uint32_t)stage_half_milliseconds);
    }

}

static bool mzf_add_profiled_stage_duration(
    uint32_t byte_count,
    uint32_t one_count,
    uint16_t checksum,
    uint32_t gap_short_pulses,
    uint16_t mark_long_pulses,
    uint16_t mark_short_pulses,
    uint16_t mark_final_long_pulses,
    uint16_t trailing_short_pulses,
    uint16_t trailing_long_pulses,
    uint32_t short_period_ticks,
    uint32_t long_period_ticks,
    uint32_t *half_milliseconds)
{
    const uint32_t checksum_ones = mzf_popcount16(checksum);
    const uint32_t half_millisecond_ticks = F_CPU / 2000UL;
    uint64_t short_pulses;
    uint64_t long_pulses;
    uint64_t stage_ticks;
    uint64_t stage_half_milliseconds;

    if ((half_milliseconds == NULL) ||
        (one_count > (byte_count * 8UL)))
    {
        return false;
    }

    short_pulses = (uint64_t)gap_short_pulses + mark_short_pulses +
                   ((uint64_t)byte_count * 8ULL - one_count) +
                   (16ULL - checksum_ones) + trailing_short_pulses;
    long_pulses = (uint64_t)mark_long_pulses +
                  mark_final_long_pulses + one_count + byte_count +
                  checksum_ones + 2ULL + trailing_long_pulses;
    stage_ticks = short_pulses * short_period_ticks +
                  long_pulses * long_period_ticks;
    stage_half_milliseconds =
        (stage_ticks + (half_millisecond_ticks / 2UL)) /
        half_millisecond_ticks;

    if (stage_half_milliseconds > 0xFFFFFFFFULL)
    {
        *half_milliseconds = 0xFFFFFFFFUL;
        return false;
    }
    return mzf_add_half_milliseconds(
        half_milliseconds, (uint32_t)stage_half_milliseconds);
}

static bool mzf_scan_payload_ones(uint32_t length,
                                  uint32_t *one_count,
                                  uint16_t *checksum)
{
    uint8_t *work = wav_sample_stream_get_shared_work_buffer();

    if ((one_count == NULL) || (checksum == NULL)) return false;
    *one_count = 0UL;
    *checksum = 0U;

    while (length != 0UL)
    {
        uint16_t request = (length > MZF_REFILL_BLOCK) ?
            MZF_REFILL_BLOCK : (uint16_t)length;
        int16_t received = sdcard_file_read(work, request);

        if (received != (int16_t)request)
        {
            mzf_set_error_P((received < 0) ? PSTR("MZF READ") : PSTR("MZF SHORT"),
                            (received < 0) ? MZF_PLAYBACK_IO_ERROR : MZF_PLAYBACK_BAD_FILE);
            return false;
        }

        for (uint16_t index = 0U; index < request; ++index)
        {
            uint8_t ones = mzf_popcount8(work[index]);
            if ((0xFFFFFFFFUL - *one_count) < (uint32_t)ones)
            {
                *one_count = 0xFFFFFFFFUL;
            }
            else
            {
                *one_count += (uint32_t)ones;
            }
            *checksum = (uint16_t)(*checksum + (uint16_t)ones);
        }
        length -= (uint32_t)request;
    }
    return true;
}

static bool mzf_scan_header_record_duration(uint32_t *half_milliseconds)
{
    uint32_t remaining;
    uint32_t header_ones = 0UL;
    uint16_t header_checksum = 0U;
    uint32_t data_ones;
    uint16_t data_checksum;
    int16_t received;

    if ((mzf_file_size - sdcard_file_position()) < MZF_HEADER_BYTES)
    {
        mzf_set_error_P(PSTR("MZT HEADER"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    received = sdcard_file_read(mzf_header, MZF_HEADER_BYTES);
    if (received != (int16_t)MZF_HEADER_BYTES)
    {
        mzf_set_error_P((received < 0) ? PSTR("MZT READ") : PSTR("MZT SHORT"),
                        (received < 0) ? MZF_PLAYBACK_IO_ERROR : MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    for (uint8_t index = 0U; index < MZF_HEADER_BYTES; ++index)
    {
        uint8_t ones = mzf_popcount8(mzf_header[index]);
        header_ones += (uint32_t)ones;
        header_checksum = (uint16_t)(header_checksum + (uint16_t)ones);
    }

    mzf_record_data_length = mzf_header_data_length();
    remaining = mzf_file_size - sdcard_file_position();
    if (mzf_record_data_length > remaining)
    {
        mzf_set_error_P(PSTR("MZF LENGTH"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    if (!mzf_add_stage_duration(true, MZF_HEADER_BYTES, header_ones,
                                header_checksum, half_milliseconds) ||
        !mzf_scan_payload_ones(mzf_record_data_length, &data_ones, &data_checksum) ||
        !mzf_add_stage_duration(false, mzf_record_data_length, data_ones,
                                data_checksum, half_milliseconds))
    {
        return false;
    }
    return true;
}

static bool mzf_calculate_total_duration(void)
{
    mzf_exact_duration_half_ms = 0UL;

    uint32_t half_milliseconds = 0UL;

    mzf_total_duration_ms = 0UL;
    if (!sdcard_file_seek(0UL))
    {
        mzf_set_error_P(PSTR("MZF SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    if (mzf_format == FILE_FORMAT_MZT)
    {
        do
        {
            if (!mzf_scan_header_record_duration(&half_milliseconds)) return false;
        }
        while (sdcard_file_position() < mzf_file_size);
    }
    else
    {
        uint32_t trailing_length;
        uint32_t trailing_ones;
        uint16_t trailing_checksum;

        if (!mzf_scan_header_record_duration(&half_milliseconds)) return false;
        trailing_length = mzf_file_size - sdcard_file_position();
        if (trailing_length != 0UL)
        {
            if (!mzf_scan_payload_ones(trailing_length, &trailing_ones,
                                       &trailing_checksum) ||
                !mzf_add_stage_duration(false, trailing_length, trailing_ones,
                                        trailing_checksum, &half_milliseconds))
            {
                return false;
            }
        }
    }

    if (half_milliseconds == 0xFFFFFFFFUL)
    {
        mzf_total_duration_ms = 0xFFFFFFFFUL;
    }
    else
    {
        const uint8_t units_per_ms =
            (uint8_t)(2U * mzf_normal_speed_divisor);
        mzf_total_duration_ms = half_milliseconds / units_per_ms;
        if ((half_milliseconds % units_per_ms) != 0UL)
        {
            mzf_total_duration_ms++;
        }
    }

    if (!sdcard_file_seek(0UL))
    {
        mzf_set_error_P(PSTR("MZF SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }
    return true;
}


static void mzf_count_buffer_ones(const uint8_t *bytes, uint16_t length,
                                  uint32_t *one_count, uint16_t *checksum)
{
    *one_count = 0UL;
    *checksum = 0U;
    for (uint16_t i = 0U; i < length; ++i)
    {
        const uint8_t ones = mzf_popcount8(bytes[i]);
        *one_count += ones;
        *checksum = (uint16_t)(*checksum + ones);
    }
}

static bool mzf_add_tape_turbo_payload_duration(uint32_t byte_count,
                                                uint32_t one_count,
                                                uint16_t checksum,
                                                uint32_t *half_milliseconds)
{
    uint32_t short_period;
    uint32_t long_period;
    uint32_t gap_pulses;
    uint16_t trailing_short = 0U;
    uint16_t trailing_long = MZF_MZ800_TRAILING_LONG_PULSES;

    switch (mzf_loader_get_variant())
    {
        case MZF_LOADER_VARIANT_IC_1_2:
            short_period = (uint32_t)MZF_IC_1_2_SHORT_HIGH_TICKS +
                           MZF_IC_1_2_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_IC_1_2_LONG_HIGH_TICKS +
                          MZF_IC_1_2_LONG_LOW_TICKS;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_IC_1_3:
            short_period = (uint32_t)MZF_IC_1_3_SHORT_HIGH_TICKS +
                           MZF_IC_1_3_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_IC_1_3_LONG_HIGH_TICKS +
                          MZF_IC_1_3_LONG_LOW_TICKS;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_IC_1_4:
            short_period = (uint32_t)MZF_IC_1_4_SHORT_HIGH_TICKS +
                           MZF_IC_1_4_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_IC_1_4_LONG_HIGH_TICKS +
                          MZF_IC_1_4_LONG_LOW_TICKS;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
        case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH:
            short_period = (uint32_t)MZF_MZ700_3X_SHORT_TICKS * 2UL;
            long_period = (uint32_t)MZF_MZ700_3X_LONG_TICKS * 2UL;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_TC_1_2:
            short_period = (uint32_t)MZF_TC_1_2_SHORT_HIGH_TICKS +
                           MZF_TC_1_2_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_TC_1_2_LONG_HIGH_TICKS +
                          MZF_TC_1_2_LONG_LOW_TICKS;
            gap_pulses = MZF_TC_1_2_TURBO_GAP_SHORT_PULSES;
            trailing_short = MZF_TC_LOADER_TRAILING_SHORT_PULSES;
            trailing_long = 0U;
            break;
        case MZF_LOADER_VARIANT_TC_1_3:
            short_period = (uint32_t)MZF_TC_1_3_SHORT_HIGH_TICKS +
                           MZF_TC_1_3_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_TC_1_3_LONG_HIGH_TICKS +
                          MZF_TC_1_3_LONG_LOW_TICKS;
            gap_pulses = MZF_TC_1_3_TURBO_GAP_SHORT_PULSES;
            trailing_short = MZF_TC_LOADER_TRAILING_SHORT_PULSES;
            trailing_long = 0U;
            break;
        case MZF_LOADER_VARIANT_TC_1_4:
            short_period = (uint32_t)MZF_TC_1_4_SHORT_HIGH_TICKS +
                           MZF_TC_1_4_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_TC_1_4_LONG_HIGH_TICKS +
                          MZF_TC_1_4_LONG_LOW_TICKS;
            gap_pulses = MZF_TC_1_4_TURBO_GAP_SHORT_PULSES;
            trailing_short = MZF_TC_LOADER_TRAILING_SHORT_PULSES;
            trailing_long = 0U;
            break;
        default:
            return false;
    }

    return mzf_add_profiled_stage_duration(
        byte_count, one_count, checksum, gap_pulses,
        MZF_IC_TURBO_MARK_LONG_PULSES,
        MZF_IC_TURBO_MARK_SHORT_PULSES,
        MZF_IC_TURBO_MARK_FINAL_LONG_PULSES,
        trailing_short, trailing_long, short_period, long_period,
        half_milliseconds);
}

static bool mzf_add_current_tape_turbo_duration(
    uint32_t *half_milliseconds)
{
    uint32_t one_count;
    uint16_t checksum;
    uint8_t *work;
    uint16_t loader_length;
    bool ok;

    if (half_milliseconds == NULL) return false;

    mzf_count_buffer_ones(mzf_header, MZF_HEADER_BYTES,
                          &one_count, &checksum);
    if (!mzf_add_profiled_stage_duration(
            MZF_HEADER_BYTES, one_count, checksum,
            MZF_MZ800_LONG_GAP_SHORT_PULSES,
            MZF_MZ800_LONG_MARK_LONG_PULSES,
            MZF_MZ800_LONG_MARK_SHORT_PULSES,
            MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES,
            0U, MZF_MZ800_TRAILING_LONG_PULSES,
            (uint32_t)MZF_SHORT_HIGH_TICKS + MZF_SHORT_LOW_TICKS,
            (uint32_t)MZF_LONG_HIGH_TICKS + MZF_LONG_LOW_TICKS,
            half_milliseconds))
    {
        return false;
    }

    if (mzf_loader_is_tc_turbo())
    {
        work = wav_sample_stream_get_shared_work_buffer();
        loader_length = mzf_loader_build_loader(work, MZF_REFILL_BLOCK);
        if ((loader_length == 0U) ||
            (loader_length != mzf_loader_get_loader_size()))
        {
            return false;
        }
        mzf_count_buffer_ones(work, loader_length, &one_count, &checksum);
        if (!mzf_add_profiled_stage_duration(
                loader_length, one_count, checksum,
                MZF_MZ800_SHORT_GAP_SHORT_PULSES,
                MZF_MZ800_SHORT_MARK_LONG_PULSES,
                MZF_MZ800_SHORT_MARK_SHORT_PULSES,
                MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES,
                MZF_TC_LOADER_TRAILING_SHORT_PULSES, 0U,
                (uint32_t)MZF_SHORT_HIGH_TICKS + MZF_SHORT_LOW_TICKS,
                (uint32_t)MZF_LONG_HIGH_TICKS + MZF_LONG_LOW_TICKS,
                half_milliseconds))
        {
            return false;
        }
    }

    if (!sdcard_file_seek(mzf_original_data_offset))
    {
        return false;
    }
    ok = mzf_scan_payload_ones(mzf_original_data_length,
                               &one_count, &checksum) &&
         mzf_add_tape_turbo_payload_duration(mzf_original_data_length,
                                             one_count, checksum,
                                             half_milliseconds);
    if (!sdcard_file_seek(mzf_original_data_offset))
    {
        return false;
    }
    if (!ok)
    {
        return false;
    }

    /* FAST3 always holds READ low for this deterministic interval before
       starting its payload. It is part of active playback, unlike a MOTOR
       pause, so include it in the displayed nominal time. */
    if (mzf_loader_is_mz700_fast3() &&
        !mzf_add_half_milliseconds(
            half_milliseconds,
            (uint32_t)MZF_MZ700_FAST3_START_DELAY_MS * 2UL))
    {
        return false;
    }

    return true;
}

static bool mzf_calculate_tape_turbo_duration(void)
{
    uint32_t half_milliseconds = 0UL;

    if (!mzf_add_current_tape_turbo_duration(&half_milliseconds))
    {
        return false;
    }

    mzf_total_duration_ms = (half_milliseconds == 0xFFFFFFFFUL) ?
        0xFFFFFFFFUL : (half_milliseconds + 1UL) / 2UL;
    return true;
}

static bool mzf_stage_uses_tape_turbo_timing(void)
{
    return mzf_stage == MZF_STAGE_TAPE_TURBO_DATA;
}

static bool mzf_stage_uses_low_first_timing(void)
{
    /* MZ-800 IC loaders use LOW-first timing; MZ-700 FAST3 uses HIGH-first
       timing for each generated pulse. */
    return mzf_stage_uses_tape_turbo_timing() && mzf_loader_is_ic_turbo();
}

static uint16_t mzf_boundary_auto_continue_ms(void)
{
    if (((mzf_stage == MZF_STAGE_HEADER) &&
         (mzf_loader_is_ic_turbo() || mzf_loader_is_mz700_fast3())) ||
        ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_tape_turbo()))
    {
        if ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_tc_turbo())
        {
            return MZF_TC_TURBO_START_DELAY_MS;
        }
        return MZF_IC_TURBO_START_DELAY_MS;
    }
    return MZF_BOUNDARY_AUTO_CONTINUE_MS;
}

static uint16_t mzf_short_high_ticks(void)
{
    if (mzf_stage_uses_tape_turbo_timing())
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH:
                return MZF_MZ700_3X_SHORT_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_4: return MZF_TC_1_4_SHORT_HIGH_TICKS;
            default: return MZF_IC_1_4_SHORT_HIGH_TICKS;
        }
    }
    return mzf_profile_short_high_ticks;
}

static uint16_t mzf_short_low_ticks(void)
{
    if (mzf_stage_uses_tape_turbo_timing())
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH:
                return MZF_MZ700_3X_SHORT_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_4: return MZF_TC_1_4_SHORT_LOW_TICKS;
            default: return MZF_IC_1_4_SHORT_LOW_TICKS;
        }
    }
    return mzf_profile_short_low_ticks;
}

static uint16_t mzf_long_high_ticks(void)
{
    if (mzf_stage_uses_tape_turbo_timing())
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH:
                return MZF_MZ700_3X_LONG_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_4: return MZF_TC_1_4_LONG_HIGH_TICKS;
            default: return MZF_IC_1_4_LONG_HIGH_TICKS;
        }
    }
    return mzf_profile_long_high_ticks;
}

static uint16_t mzf_long_low_ticks(void)
{
    if (mzf_stage_uses_tape_turbo_timing())
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH:
                return MZF_MZ700_3X_LONG_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_4: return MZF_TC_1_4_LONG_LOW_TICKS;
            default: return MZF_IC_1_4_LONG_LOW_TICKS;
        }
    }
    return mzf_profile_long_low_ticks;
}

static void mzf_configure_normal_speed(loader_mode_t loader_mode)
{
    mz_tape_profile_id_t profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_1X;
    mz_tape_profile_t profile;

    if (loader_mode == LOADER_MODE_NORMAL_1_2)
    {
        mzf_normal_speed_divisor = 2U;
        profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_2X;
    }
    else if (loader_mode == LOADER_MODE_NORMAL_1_3)
    {
        mzf_normal_speed_divisor = 3U;
        profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_3X;
    }
    else if (loader_mode == LOADER_MODE_NORMAL_1_4)
    {
        mzf_normal_speed_divisor = 4U;
        profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_4X;
    }
    else
    {
        mzf_normal_speed_divisor = 1U;
    }

    mzf_native_mz700 = (loader_mode == LOADER_MODE_MZ700_1X);
    if (mzf_native_mz700)
    {
        profile_id = MZ_TAPE_PROFILE_MZ700_NORMAL_1X;
    }

    if (!mz_tape_profile_read(profile_id, &profile))
    {
        return;
    }

    mzf_profile_short_high_ticks = MZF_US_TO_TICKS(profile.short_high_us);
    mzf_profile_short_low_ticks = MZF_US_TO_TICKS(profile.short_low_us);
    mzf_profile_long_high_ticks = MZF_US_TO_TICKS(profile.long_high_us);
    mzf_profile_long_low_ticks = MZF_US_TO_TICKS(profile.long_low_us);

    /* Use explicit fractional-tick NORMAL profiles instead of rounded
       microsecond conversion. */
    if (loader_mode == LOADER_MODE_NORMAL_1_2)
    {
        mzf_profile_short_high_ticks = MZF_NORMAL_1_2_SHORT_TICKS;
        mzf_profile_short_low_ticks = MZF_NORMAL_1_2_SHORT_TICKS;
        mzf_profile_long_high_ticks = MZF_NORMAL_1_2_LONG_TICKS;
        mzf_profile_long_low_ticks = MZF_NORMAL_1_2_LONG_TICKS;
    }
    else if (loader_mode == LOADER_MODE_NORMAL_1_3)
    {
        mzf_profile_short_high_ticks = MZF_NORMAL_1_3_SHORT_TICKS;
        mzf_profile_short_low_ticks = MZF_NORMAL_1_3_SHORT_TICKS;
        mzf_profile_long_high_ticks = MZF_NORMAL_1_3_LONG_TICKS;
        mzf_profile_long_low_ticks = MZF_NORMAL_1_3_LONG_TICKS;
    }

    mzf_profile_header_leader =
        (uint16_t)profile.header_leader_short_pulses;
    mzf_profile_data_leader =
        (uint16_t)profile.data_leader_short_pulses;
    mzf_profile_header_mark_long = profile.header_mark_long_pulses;
    mzf_profile_header_mark_short = profile.header_mark_short_pulses;
    mzf_profile_data_mark_long = profile.data_mark_long_pulses;
    mzf_profile_data_mark_short = profile.data_mark_short_pulses;
    mzf_profile_final_mark_long = profile.final_mark_long_pulses;
    mzf_profile_duplicate_gap = profile.duplicate_gap_short_pulses;
}

static void mzf_set_short_pulse(uint16_t *high_ticks, uint16_t *low_ticks)
{
    *high_ticks = mzf_short_high_ticks();
    *low_ticks = mzf_short_low_ticks();
}

static void mzf_set_long_pulse(uint16_t *high_ticks, uint16_t *low_ticks)
{
    *high_ticks = mzf_long_high_ticks();
    *low_ticks = mzf_long_low_ticks();
}

static uint16_t mzf_gap_short_pulses(void)
{
    if (mzf_stage == MZF_STAGE_HEADER)
    {
        return mzf_profile_header_leader;
    }
    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_TC_1_2:
                return MZF_TC_1_2_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_3:
                return MZF_TC_1_3_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_4:
                return MZF_TC_1_4_TURBO_GAP_SHORT_PULSES;
            default:
                return MZF_IC_TURBO_GAP_SHORT_PULSES;
        }
    }
    return mzf_profile_data_leader;
}

static uint16_t mzf_mark_long_pulses(void)
{
    if (mzf_stage == MZF_STAGE_HEADER)
    {
        return mzf_profile_header_mark_long;
    }
    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        return MZF_IC_TURBO_MARK_LONG_PULSES;
    }
    return mzf_profile_data_mark_long;
}

static uint16_t mzf_mark_short_pulses(void)
{
    if (mzf_stage == MZF_STAGE_HEADER)
    {
        return mzf_profile_header_mark_short;
    }
    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        return MZF_IC_TURBO_MARK_SHORT_PULSES;
    }
    return mzf_profile_data_mark_short;
}

static uint16_t mzf_mark_final_long_pulses(void)
{
    return (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_FINAL_LONG_PULSES :
        mzf_profile_final_mark_long;
}

static bool mzf_stage_uses_tc_trailing(void)
{
    return mzf_loader_is_tc_turbo() &&
           ((mzf_stage == MZF_STAGE_DATA) ||
            (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA));
}

static uint16_t mzf_trailing_pulses(void)
{
    return mzf_stage_uses_tc_trailing() ?
        MZF_TC_LOADER_TRAILING_SHORT_PULSES :
        MZF_MZ800_TRAILING_LONG_PULSES;
}
static void mzf_begin_normal_stage(mzf_stage_t stage)
{
    mzf_stage = stage;
    mzf_normal_step = MZF_STEP_BEGIN;
    mzf_normal_loop = 0U;
    mzf_normal_bytes_remaining = 0UL;
    mzf_normal_checksum = 0U;
    mzf_normal_data = 0U;
    mzf_normal_bits_remaining = 0U;
    mzf_normal_checksum_byte_index = 0U;
    mzf_normal_header_preamble = (stage == MZF_STAGE_HEADER);
    mzf_native_copy_index = 0U;
    mzf_native_repeat_refill_requested = false;
    mzf_native_repeat_refill_ready = false;
    mzf_boundary_waiting = false;
    mzf_motor_low_seen = 0U;
    mzf_boundary_auto_timer_armed = false;
    mzf_boundary_auto_start_ms = 0U;
    mzf_fast3_start_delay_armed = false;
    mzf_fast3_start_delay_started_ms = 0U;
    mzf_paused_mid_pulse = false;
    mzf_pwm_bootstrap_pending = false;
    mzf_pwm_stop_pending = false;
    mzf_pwm_next_valid = false;
    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;

    if (stage == MZF_STAGE_HEADER)
    {
        mzf_header_offset = 0U;
    }
}

static uint32_t mzf_stage_byte_count(void)
{
    if (mzf_stage == MZF_STAGE_HEADER) return MZF_HEADER_BYTES;
    if ((mzf_stage == MZF_STAGE_DATA) ||
        (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)) return mzf_record_data_length;
    return 0UL;
}

static bool mzf_next_source_byte_from_isr(uint8_t *value)
{
    if (value == NULL)
    {
        return false;
    }

    if (mzf_stage == MZF_STAGE_HEADER)
    {
        if (mzf_header_offset >= MZF_HEADER_BYTES)
        {
            return false;
        }
        *value = mzf_header[mzf_header_offset++];
        return true;
    }

    if ((mzf_stage == MZF_STAGE_DATA) ||
        (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA))
    {
        if (!mzf_fifo_pop_from_isr(value))
        {
            return false;
        }
        return true;
    }

    return false;
}

static bool mzf_next_normal_pulse_from_isr(uint16_t *high_ticks,
                                            uint16_t *low_ticks)
{
    uint32_t byte_count;

    if ((high_ticks == NULL) || (low_ticks == NULL))
    {
        return false;
    }

    for (;;)
    {
        switch (mzf_normal_step)
        {
            case MZF_STEP_BEGIN:
                mzf_normal_loop = mzf_gap_short_pulses();
                mzf_normal_step = MZF_STEP_GAP;
                continue;

            case MZF_STEP_GAP:
                if (mzf_normal_loop == 0U)
                {
                    mzf_normal_loop = mzf_mark_long_pulses();
                    mzf_normal_step = MZF_STEP_TAPE_MARK_LONG;
                    continue;
                }
                mzf_set_short_pulse(high_ticks, low_ticks);
                mzf_normal_loop--;
                return true;

            case MZF_STEP_TAPE_MARK_LONG:
                if (mzf_normal_loop == 0U)
                {
                    mzf_normal_loop = mzf_mark_short_pulses();
                    mzf_normal_step = MZF_STEP_TAPE_MARK_SHORT;
                    continue;
                }
                mzf_set_long_pulse(high_ticks, low_ticks);
                mzf_normal_loop--;
                return true;

            case MZF_STEP_TAPE_MARK_SHORT:
                if (mzf_normal_loop == 0U)
                {
                    mzf_normal_loop = mzf_mark_final_long_pulses();
                    mzf_normal_step = MZF_STEP_TAPE_MARK_FINAL;
                    continue;
                }
                mzf_set_short_pulse(high_ticks, low_ticks);
                mzf_normal_loop--;
                return true;

            case MZF_STEP_TAPE_MARK_FINAL:
                if (mzf_normal_loop == 0U)
                {
                    byte_count = mzf_stage_byte_count();
                    mzf_normal_bytes_remaining = byte_count;
                    mzf_normal_checksum = 0U;
                    mzf_normal_checksum_byte_index = 0U;
                    mzf_normal_step = MZF_STEP_BYTE_LOAD;
                    continue;
                }
                mzf_set_long_pulse(high_ticks, low_ticks);
                mzf_normal_loop--;
                return true;

            case MZF_STEP_BYTE_LOAD:
                if (mzf_normal_bytes_remaining == 0UL)
                {
                    mzf_normal_checksum_byte_index = 0U;
                    mzf_normal_step = MZF_STEP_CHECKSUM_LOAD;
                    continue;
                }
                if (!mzf_next_source_byte_from_isr(&mzf_normal_data))
                {
                    mzf_set_error_from_isr_P(PSTR("MZF UNDER"), MZF_PLAYBACK_UNDERRUN);
                    return false;
                }
                mzf_normal_bytes_remaining--;
                mzf_normal_bits_remaining = 8U;
                mzf_normal_step = MZF_STEP_BYTE_BITS;
                continue;

            case MZF_STEP_BYTE_BITS:
                if (mzf_normal_bits_remaining == 0U)
                {
                    mzf_normal_step = MZF_STEP_BYTE_STOP;
                    continue;
                }
                if ((mzf_normal_data & 0x80U) != 0U)
                {
                    mzf_set_long_pulse(high_ticks, low_ticks);
                    mzf_normal_checksum++;
                }
                else
                {
                    mzf_set_short_pulse(high_ticks, low_ticks);
                }
                mzf_normal_data <<= 1U;
                mzf_normal_bits_remaining--;
                return true;

            case MZF_STEP_BYTE_STOP:
                /* MZ-800 ROM format: one long stop pulse follows every byte. */
                mzf_set_long_pulse(high_ticks, low_ticks);
                mzf_normal_step = MZF_STEP_BYTE_LOAD;
                return true;

            case MZF_STEP_CHECKSUM_LOAD:
                if (mzf_normal_checksum_byte_index >= 2U)
                {
                    mzf_normal_loop = mzf_trailing_pulses();
                    mzf_normal_step = MZF_STEP_TRAILING_LONGS;
                    continue;
                }
                mzf_normal_data = (mzf_normal_checksum_byte_index == 0U) ?
                    (uint8_t)(mzf_normal_checksum >> 8U) :
                    (uint8_t)(mzf_normal_checksum & 0xFFU);
                mzf_normal_checksum_byte_index++;
                mzf_normal_bits_remaining = 8U;
                mzf_normal_step = MZF_STEP_CHECKSUM_BITS;
                continue;

            case MZF_STEP_CHECKSUM_BITS:
                if (mzf_normal_bits_remaining == 0U)
                {
                    mzf_normal_step = MZF_STEP_CHECKSUM_STOP;
                    continue;
                }
                if ((mzf_normal_data & 0x80U) != 0U)
                {
                    mzf_set_long_pulse(high_ticks, low_ticks);
                }
                else
                {
                    mzf_set_short_pulse(high_ticks, low_ticks);
                }
                mzf_normal_data <<= 1U;
                mzf_normal_bits_remaining--;
                return true;

            case MZF_STEP_CHECKSUM_STOP:
                mzf_set_long_pulse(high_ticks, low_ticks);
                mzf_normal_step = MZF_STEP_CHECKSUM_LOAD;
                return true;

            case MZF_STEP_TRAILING_LONGS:
                if (mzf_normal_loop == 0U)
                {
                    if (mzf_native_mz700 &&
                        (mzf_profile_duplicate_gap != 0U) &&
                        (mzf_native_copy_index == 0U) &&
                        ((mzf_stage == MZF_STAGE_HEADER) ||
                         (mzf_stage == MZF_STAGE_DATA)))
                    {
                        mzf_native_copy_index = 1U;
                        mzf_normal_loop = mzf_profile_duplicate_gap;
                        mzf_normal_step = MZF_STEP_DUPLICATE_GAP;
                        if (mzf_stage == MZF_STAGE_HEADER)
                        {
                            mzf_header_offset = 0U;
                            mzf_native_repeat_refill_ready = true;
                        }
                        else
                        {
                            mzf_native_repeat_refill_ready = false;
                            mzf_native_repeat_refill_requested = true;
                        }
                        continue;
                    }
                    mzf_normal_step = MZF_STEP_BOUNDARY;
                    continue;
                }
                if (mzf_stage_uses_tc_trailing())
                {
                    mzf_set_short_pulse(high_ticks, low_ticks);
                }
                else
                {
                    mzf_set_long_pulse(high_ticks, low_ticks);
                }
                mzf_normal_loop--;
                return true;

            case MZF_STEP_DUPLICATE_GAP:
                if (mzf_normal_loop != 0U)
                {
                    mzf_set_short_pulse(high_ticks, low_ticks);
                    mzf_normal_loop--;
                    return true;
                }
                if ((mzf_stage == MZF_STAGE_DATA) &&
                    !mzf_native_repeat_refill_ready)
                {
                    mzf_set_error_from_isr_P(PSTR("MZ7 REFILL"),
                                             MZF_PLAYBACK_UNDERRUN);
                    return false;
                }
                mzf_normal_bytes_remaining = mzf_stage_byte_count();
                mzf_normal_checksum = 0U;
                mzf_normal_checksum_byte_index = 0U;
                mzf_normal_data = 0U;
                mzf_normal_bits_remaining = 0U;
                mzf_normal_step = MZF_STEP_BYTE_LOAD;
                continue;

            case MZF_STEP_BOUNDARY:
                /*
                    A final declared data record does not require another
                    MOTOR edge. Finish here so a single-record MZF/M12 and
                    the last MZT record return to the browser immediately.
                    Earlier records remain at the boundary and advance only
                    after the monitor turns MOTOR off.
                */
                if ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_ul_active())
                {
                    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_BOUNDARY;
                    return false;
                }

                if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)
                {
                    mzf_pwm_terminal_pending =
                        ((mzf_format == FILE_FORMAT_MZT) &&
                         (mzf_record_data_file_end < mzf_file_size)) ?
                            MZF_PWM_TERMINAL_BOUNDARY :
                            MZF_PWM_TERMINAL_FINISHED;
                    return false;
                }

                if ((mzf_stage == MZF_STAGE_DATA) &&
                    (mzf_record_data_file_end >= mzf_file_size))
                {
                    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_FINISHED;
                    return false;
                }

                mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_BOUNDARY;
                return false;

            default:
                mzf_set_error_from_isr_P(PSTR("MZF STATE"), MZF_PLAYBACK_BAD_FILE);
                return false;
        }
    }
}

static bool mzf_queue_next_pwm_pulse_from_isr(void)
{
    uint16_t high_ticks;
    uint16_t low_ticks;

    if (!mzf_next_normal_pulse_from_isr(&high_ticks, &low_ticks))
    {
        return false;
    }

    mzf_pwm_write_next_from_isr(high_ticks, low_ticks);
    return true;
}

static bool mzf_physical_low_first(void)
{
    return mzf_stage_uses_low_first_timing() != mzf_wave_invert_signal;
}

static bool mzf_start_normal_output_immediate(void)
{
    uint16_t high_ticks;
    uint16_t low_ticks;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (mzf_next_normal_pulse_from_isr(&high_ticks, &low_ticks))
        {
            mzf_pwm_start_first_from_isr(high_ticks, low_ticks,
                                         mzf_physical_low_first());
        }
    }
    return (mzf_state == MZF_PLAYBACK_RUNNING) && !mzf_boundary_waiting;
}

static bool mzf_start_normal_output(void)
{
    /* Keep READ low for 400 ms between the synthetic header and the first
       FAST3 leader pulse. The wait is serviced without blocking foreground. */
    if (mzf_loader_is_mz700_fast3() &&
        (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA) &&
        (mzf_normal_step == MZF_STEP_BEGIN))
    {
        if (!mzf_fast3_start_delay_armed)
        {
            mz_read_set(false);
            mzf_fast3_start_delay_armed = true;
            mzf_fast3_start_delay_started_ms = (uint16_t)millis();
        }
        return true;
    }

    return mzf_start_normal_output_immediate();
}

static bool mzf_start_ultrafast_output(void)
{
    if (!mzf_loader_begin())
    {
        mzf_set_error(mzf_loader_get_error_text(), MZF_PLAYBACK_IO_ERROR);
        return false;
    }
    return true;
}

static bool mzf_prepare_tape_turbo_payload(void)
{
    if (!sdcard_file_seek(mzf_original_data_offset))
    {
        mzf_set_error_P(PSTR("TURB SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    mzf_record_data_length = mzf_original_data_length;
    mzf_record_data_file_end = mzf_original_data_offset + mzf_original_data_length;
    mzf_record_data_read = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_fifo_reset();
    if (!mzf_prefill_data())
    {
        return false;
    }
    mzf_tape_turbo_payload_prepared = true;
    return true;
}

/* The first native MZ700 data copy has consumed the FIFO. Rewind the same
   logical MZF payload while Timer3 emits the 256-short duplicate separator.
   One bounded refill is enough to make the second copy runnable; normal
   foreground refill continues during the separator and payload. */
static bool mzf_prepare_native_mz700_repeat(void)
{
    bool requested;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        requested = mzf_native_repeat_refill_requested;
        if (requested)
        {
            mzf_native_repeat_refill_requested = false;
        }
    }
    if (!requested)
    {
        return true;
    }

    if (!sdcard_file_seek(mzf_record_data_file_start))
    {
        mzf_set_error_P(PSTR("MZ7 SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    mzf_record_data_read = 0UL;
    mzf_fifo_reset();
    if ((mzf_record_data_length != 0UL) && !mzf_refill_data_once())
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        mzf_native_repeat_refill_ready = true;
    }
    return true;
}

static bool mzf_loader_mode_is_ul_family(loader_mode_t loader_mode)
{
    return (loader_mode == LOADER_MODE_UL) ||
           (loader_mode == LOADER_MODE_UL_MZ800) ||
           (loader_mode == LOADER_MODE_UL_MZ700);
}

static bool mzf_resolve_mzt_loader_mode(uint16_t record_index,
                                        loader_mode_t *loader_mode)
{
    uint32_t resume_position;

    if ((loader_mode == NULL) || (mzf_source_path == NULL) ||
        (record_index == 0U))
    {
        return false;
    }

    if (mzf_requested_loader_mode != LOADER_MODE_AUTO)
    {
        mzf_mzt_record_loader_from_sidecar = false;
        *loader_mode = mzf_requested_loader_mode;
        return true;
    }

    /* sdcard owns one global file handle. Temporarily close MZT, inspect its
       .MTI, then reopen MZT at the exact byte after the just-read header. */
    resume_position = sdcard_file_position();
    sdcard_file_close();

    *loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_mzt_record_loader_from_sidecar =
        mzi_sidecar_read_loader_for_mzt_record(
            mzf_source_path, record_index, loader_mode);

    if (!sdcard_file_open_read(mzf_source_path))
    {
        mzf_set_error_P(PSTR("MZT REOPEN"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }
    if (!sdcard_file_seek(resume_position))
    {
        mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }
    return true;
}


static bool mzf_add_current_normal_record_duration(
    uint32_t *half_milliseconds)
{
    uint32_t header_ones = 0UL;
    uint16_t header_checksum = 0U;
    uint32_t data_ones;
    uint16_t data_checksum;

    if (half_milliseconds == NULL) return false;

    for (uint8_t index = 0U; index < MZF_HEADER_BYTES; ++index)
    {
        const uint8_t ones = mzf_popcount8(mzf_header[index]);
        header_ones += (uint32_t)ones;
        header_checksum = (uint16_t)(header_checksum + (uint16_t)ones);
    }

    if (!mzf_add_stage_duration(true, MZF_HEADER_BYTES, header_ones,
                                header_checksum, half_milliseconds))
    {
        return false;
    }

    if (!sdcard_file_seek(mzf_original_data_offset))
    {
        return false;
    }
    if (!mzf_scan_payload_ones(mzf_original_data_length,
                               &data_ones, &data_checksum) ||
        !mzf_add_stage_duration(false, mzf_original_data_length, data_ones,
                                data_checksum, half_milliseconds))
    {
        return false;
    }
    return true;
}

/*
    MZT is indexed without a RAM directory.  Scan only the 128-byte headers
    and declared payload lengths, count the records and leave the requested
    logical MZF header loaded.  This makes selecting a record in the middle of
    a large MZT cheap in SRAM; selection cost is foreground-only O(n) SD seeks.
*/
static bool mzf_locate_mzt_record(uint16_t wanted_record)
{
    uint16_t record_index = 0U;
    uint32_t wanted_offset = 0UL;
    bool found = false;

    if (wanted_record == 0U) wanted_record = 1U;
    mzf_mzt_record_count = 0U;

    if (!sdcard_file_seek(0UL))
    {
        mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    while (sdcard_file_position() < mzf_file_size)
    {
        uint32_t header_offset = sdcard_file_position();

        if ((mzf_file_size - header_offset) < MZF_HEADER_BYTES)
        {
            mzf_set_error_P(PSTR("MZT HEADER"), MZF_PLAYBACK_BAD_FILE);
            return false;
        }
        if (!mzf_read_header_record())
        {
            return false;
        }
        if (record_index == 0xFFFFU)
        {
            mzf_set_error_P(PSTR("MZT COUNT"), MZF_PLAYBACK_BAD_FILE);
            return false;
        }
        record_index++;
        if (record_index == wanted_record)
        {
            wanted_offset = header_offset;
            found = true;
        }

        if (!sdcard_file_seek(mzf_record_data_file_end))
        {
            mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
            return false;
        }
    }

    if ((sdcard_file_position() != mzf_file_size) || (record_index == 0U))
    {
        mzf_set_error_P(PSTR("MZT FORMAT"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    mzf_mzt_record_count = record_index;
    if (!found)
    {
        mzf_set_error_P(PSTR("MZT RECORD"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    if (!sdcard_file_seek(wanted_offset) || !mzf_read_header_record())
    {
        if (mzf_state != MZF_PLAYBACK_BAD_FILE)
        {
            mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
        }
        return false;
    }

    mzf_mzt_record_index = wanted_record;
    return true;
}

/*
    Resolve the effective loader of record n+1 exactly once.

    mzf_resolve_mzt_loader_mode() temporarily uses the global sidecar-source
    flag because that flag normally describes the active record. Capture the
    prospective record's source, then restore the current record until n+1 is
    actually committed. This avoids a check/use split where MTI could be read
    once for the UL->UL decision and a second time for real playback.
*/
static bool mzf_resolve_next_mzt_loader(loader_mode_t *loader_mode,
                                        bool *loader_from_sidecar)
{
    bool current_loader_from_sidecar;

    if ((loader_mode == NULL) || (loader_from_sidecar == NULL) ||
        (mzf_format != FILE_FORMAT_MZT) ||
        (mzf_mzt_record_index == 0U) ||
        (mzf_mzt_record_index >= mzf_mzt_record_count))
    {
        return false;
    }

    current_loader_from_sidecar = mzf_mzt_record_loader_from_sidecar;
    if (!mzf_resolve_mzt_loader_mode((uint16_t)(mzf_mzt_record_index + 1U),
                                     loader_mode))
    {
        mzf_mzt_record_loader_from_sidecar = current_loader_from_sidecar;
        return false;
    }

    *loader_from_sidecar = mzf_mzt_record_loader_from_sidecar;
    mzf_mzt_record_loader_from_sidecar = current_loader_from_sidecar;
    return true;
}

static bool mzf_calculate_current_mzt_normal_duration(void)
{
    uint32_t half_milliseconds = 0UL;

    mzf_total_duration_ms = 0UL;
    mzf_exact_duration_half_ms = 0UL;
    if (!mzf_add_current_normal_record_duration(&half_milliseconds))
    {
        return false;
    }

    mzf_total_duration_ms = (half_milliseconds == 0xFFFFFFFFUL) ?
        0xFFFFFFFFUL : (half_milliseconds + 1UL) / 2UL;

    /* The duration scan consumed the payload.  Playback must start again at
       the current record's real data bytes. */
    if (!sdcard_file_seek(mzf_original_data_offset))
    {
        mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }
    return true;
}

static bool mzf_prepare_current_record(loader_mode_t loader_mode)
{
    file_format_t loader_format = mzf_format;
    uint32_t loader_file_end = mzf_file_size;
    bool loader_active;

    if (loader_mode == LOADER_MODE_AUTO)
    {
        loader_mode = LOADER_MODE_NORMAL_1_1;
    }

    mzf_wave_invert_signal = false;
    mzf_configure_normal_speed(loader_mode);
    mzf_original_data_offset = sdcard_file_position();
    mzf_original_data_length = mzf_record_data_length;
    mzf_mzt_record_loader_mode = loader_mode;
    mzf_capture_mzt_record_title();

    /* A record inside MZT is logically one MZF for the generated loader.
       Clamp its visible file end to this record so no UL/turbo transport can
       consume the following MZT header as payload. */
    if (mzf_format == FILE_FORMAT_MZT)
    {
        loader_format = FILE_FORMAT_MZF;
        loader_file_end = mzf_record_data_file_end;
        mzf_total_duration_ms = 0UL;
        mzf_exact_duration_half_ms = 0UL;
    }

    loader_active = mzf_loader_prepare(loader_format, loader_mode, mzf_header,
                                       loader_file_end,
                                       mzf_original_data_offset);
    if (loader_active)
    {
        mzf_wave_invert_signal = mzf_loader_is_tc_turbo();
        if (!mzf_loader_patch_loader_header(mzf_header))
        {
            mzf_set_error_P(PSTR("LDR HEADER"), MZF_PLAYBACK_BAD_FILE);
            return false;
        }
        if (!mzf_loader_is_header_only() &&
            !mzf_loader_is_ic_turbo() &&
            !mzf_loader_is_mz700_fast3() &&
            !mzf_prepare_loader_block_data())
        {
            return false;
        }

        /* IC/TC/MZ700 FAST3 have deterministic generated tape timing, so the
           PLAY clock can describe this one logical MZT record exactly.  The
           classic UL/UL800/UL700 payload is WRITE/SENSE handshaked and has no
           meaningful file-derived wall-clock duration. */
        if (mzf_loader_is_tape_turbo())
        {
            if (!mzf_calculate_tape_turbo_duration())
            {
                if (mzf_state != MZF_PLAYBACK_IO_ERROR)
                {
                    mzf_set_error_P((mzf_format == FILE_FORMAT_MZT) ?
                                        PSTR("MZT TIME") : PSTR("MZF TIME"),
                                    MZF_PLAYBACK_BAD_FILE);
                }
                return false;
            }
        }
        else if (mzf_format == FILE_FORMAT_MZT)
        {
            mzf_total_duration_ms = 0UL;
            mzf_exact_duration_half_ms = 0UL;
        }
        else
        {
            mzf_total_duration_ms = 0UL;
        }

        /* IC and MZ700 FAST3 go directly from the generated header to the
           original payload. Prefill it now and preserve it over the boundary. */
        if ((mzf_loader_is_ic_turbo() || mzf_loader_is_mz700_fast3()) &&
            !mzf_prepare_tape_turbo_payload())
        {
            return false;
        }
    }
    else
    {
        if ((mzf_format == FILE_FORMAT_MZT) &&
            !mzf_calculate_current_mzt_normal_duration())
        {
            return false;
        }
        if (!mzf_prefill_data())
        {
            return false;
        }
    }

    mzf_begin_normal_stage(MZF_STAGE_HEADER);
    return true;
}

static bool mzf_start_next_mzt_record_resolved(
    loader_mode_t loader_mode,
    bool loader_from_sidecar)
{
    if ((mzf_mzt_record_index == 0U) ||
        (mzf_mzt_record_index >= mzf_mzt_record_count))
    {
        mzf_state = MZF_PLAYBACK_FINISHED;
        return true;
    }

    /* Duration scans and FIFO read-ahead may leave the shared SD stream at a
       different physical position. The next logical MZT header is defined by
       the current record end, not by whichever foreground read ran last. */
    if (!sdcard_file_seek(mzf_original_data_offset +
                          mzf_original_data_length))
    {
        mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    mzf_mzt_record_index++;
    if (!mzf_read_header_record())
    {
        return false;
    }

    /* Use the loader decision already resolved for this record. MTI is not
       read again between validation and record preparation. */
    mzf_mzt_record_loader_from_sidecar = loader_from_sidecar;
    return mzf_prepare_current_record(loader_mode);
}

static bool mzf_start_next_mzt_record(void)
{
    loader_mode_t loader_mode;

    if ((mzf_mzt_record_index == 0U) ||
        (mzf_mzt_record_index >= mzf_mzt_record_count))
    {
        mzf_state = MZF_PLAYBACK_FINISHED;
        return true;
    }

    /* At a normal boundary, read the next header and then resolve that
       record's effective loader. The UL -> next path uses the pre-resolved
       helper so validation and use share one MTI result. */
    if (!sdcard_file_seek(mzf_original_data_offset +
                          mzf_original_data_length))
    {
        mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    mzf_mzt_record_index++;
    if (!mzf_read_header_record())
    {
        return false;
    }
    if (!mzf_resolve_mzt_loader_mode(mzf_mzt_record_index, &loader_mode))
    {
        return false;
    }
    return mzf_prepare_current_record(loader_mode);
}

static bool mzf_advance_after_boundary(void)
{
    if (mzf_stage == MZF_STAGE_HEADER)
    {
        if (mzf_loader_is_mz700_fast3())
        {
            if (!mzf_tape_turbo_payload_prepared &&
                !mzf_prepare_tape_turbo_payload())
            {
                return false;
            }
            mzf_begin_normal_stage(MZF_STAGE_TAPE_TURBO_DATA);
            return true;
        }
        if (mzf_loader_is_header_only())
        {
            if (mzf_loader_is_mz700_ul())
            {
                /* UL700 waits at its explicit READ-high start gate. */
                mzf_stop_timer_from_foreground(false);
                mz_read_set_fast(true);
            }
            else
            {
                /* UL800 enters its monitor/prolog delay with READ low. */
                mzf_stop_timer_from_foreground(true);
            }
            mzf_stage = MZF_STAGE_ULTRAFAST;
            mzf_boundary_waiting = false;
            mzf_motor_low_seen = 0U;
            mzf_boundary_auto_timer_armed = false;
            mzf_boundary_auto_start_ms = 0U;
            return true;
        }
        if (mzf_loader_is_ic_turbo())
        {
            if (!mzf_tape_turbo_payload_prepared &&
                !mzf_prepare_tape_turbo_payload())
            {
                return false;
            }
            mzf_begin_normal_stage(MZF_STAGE_TAPE_TURBO_DATA);
            return true;
        }
        mzf_begin_normal_stage(MZF_STAGE_DATA);
        return true;
    }

    if ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_tape_turbo())
    {
        if (!mzf_prepare_tape_turbo_payload())
        {
            return false;
        }
        mzf_begin_normal_stage(MZF_STAGE_TAPE_TURBO_DATA);
        return true;
    }

    if ((mzf_stage != MZF_STAGE_DATA) &&
        (mzf_stage != MZF_STAGE_TAPE_TURBO_DATA))
    {
        mzf_set_error_P(PSTR("MZF STATE"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    if ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_ul_active())
    {
        /* Classic UL hands control to its loader with READ low. */
        mzf_stop_timer_from_foreground(true);
        mzf_stage = MZF_STAGE_ULTRAFAST;
        mzf_boundary_waiting = false;
        mzf_motor_low_seen = 0U;
        mzf_boundary_auto_timer_armed = false;
        mzf_boundary_auto_start_ms = 0U;
        return true;
    }

    if (mzf_format == FILE_FORMAT_MZT)
    {
        if (sdcard_file_position() < mzf_file_size)
        {
            return mzf_start_next_mzt_record();
        }
    }
    else if ((mzf_stage == MZF_STAGE_DATA) &&
             (sdcard_file_position() < mzf_file_size))
    {
        /* MZF/M12 compatibility: trailing bytes are emitted as a follow-on
           data block after the next MOTOR restart. */
        mzf_record_data_length = mzf_file_size - sdcard_file_position();
        mzf_record_data_file_end = mzf_file_size;
        mzf_record_data_read = 0UL;
        mzf_fifo_reset();
        if (!mzf_prefill_data())
        {
            return false;
        }
        mzf_begin_normal_stage(MZF_STAGE_DATA);
        return true;
    }

    mzf_state = MZF_PLAYBACK_FINISHED;
    return true;
}

/*
    Continue a completed PWM block when MOTOR stays high. This is required in
    PLAY CTRL / MANUAL and also handles a MOTOR-low indication shorter than a
    foreground service interval. A persistent MOTOR-low level is handled by
    mzf_playback_pause().
*/
static void mzf_service_boundary_auto_continue(void)
{
    bool ul_loader_boundary;
    bool tc_turbo_boundary;
    uint16_t now;

    if (!mzf_boundary_waiting || (mzf_state != MZF_PLAYBACK_RUNNING))
    {
        return;
    }

    ul_loader_boundary = mzf_loader_is_ul_active() &&
                         (((mzf_stage == MZF_STAGE_DATA) &&
                           !mzf_loader_is_header_only()) ||
                          ((mzf_stage == MZF_STAGE_HEADER) &&
                           mzf_loader_is_header_only()));
    tc_turbo_boundary = (mzf_stage == MZF_STAGE_DATA) &&
                        mzf_loader_is_tc_turbo();

    /* In MOTOR mode let the controller preserve the native MOTOR-low pause.
       In MANUAL mode the controller deliberately ignores MOTOR, so the backend
       must not strand itself at this boundary waiting for a signal nobody will
       resume. */
    if (mzf_motor_control_enabled && !ul_loader_boundary && !mz_motor_get())
    {
        return;
    }

    if (!ul_loader_boundary)
    {
        now = (uint16_t)millis();
        if (!mzf_boundary_auto_timer_armed)
        {
            mzf_boundary_auto_start_ms = now;
            mzf_boundary_auto_timer_armed = true;

            /* A low edge captured at timer precision already satisfies the
               boundary, so no additional silent wait is required. */
            if ((mzf_motor_low_seen == 0U) || tc_turbo_boundary)
            {
                return;
            }
        }
        else if (((mzf_motor_low_seen == 0U) || tc_turbo_boundary) &&
                 ((uint16_t)(now - mzf_boundary_auto_start_ms) <
                  mzf_boundary_auto_continue_ms()))
        {
            return;
        }
    }

    if (!mzf_advance_after_boundary())
    {
        return;
    }

    if (mzf_state == MZF_PLAYBACK_FINISHED)
    {
        return;
    }

    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        if (!mzf_start_ultrafast_output() && (mzf_state == MZF_PLAYBACK_RUNNING))
        {
            mzf_set_error_P(PSTR("UL START"), MZF_PLAYBACK_BAD_FILE);
        }
        return;
    }

    /* UL/UL800/UL700 completion releases SENSE.  When an MZT record is
       auto-continued without a MOTOR-low pause, restore the normal CMT gate
       before the next record starts; resume() already does this on a real
       pause/restart path. */
    mz_sense_set(false);
    if (!mzf_start_normal_output() && (mzf_state == MZF_PLAYBACK_RUNNING))
    {
        mzf_set_error_P(PSTR("MZF START"), MZF_PLAYBACK_BAD_FILE);
    }
}

void mzf_playback_init(void)
{
    mzf_stop_timer_from_foreground(true);
    mzf_wave_invert_signal = false;
    mzf_state = MZF_PLAYBACK_STOPPED;
    mzf_error_text[0] = '\0';
    mzf_format = FILE_FORMAT_UNKNOWN;
    mzf_source_path = NULL;
    mzf_requested_loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_motor_control_enabled = true;
    mzf_mzt_record_index = 0U;
    mzf_mzt_record_count = 0U;
    mzf_mzt_record_loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_mzt_record_loader_from_sidecar = false;
    mzf_mzt_record_title[0] = '\0';
    mzf_configure_normal_speed(LOADER_MODE_NORMAL_1_1);
    mzf_file_size = 0UL;
    mzf_total_duration_ms = 0UL;
    mzf_record_data_length = 0UL;
    mzf_record_data_file_end = 0UL;
    mzf_record_data_file_start = 0UL;
    mzf_record_data_read = 0UL;
    mzf_original_data_offset = 0UL;
    mzf_original_data_length = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_stage = MZF_STAGE_NONE;
    mzf_boundary_waiting = false;
    mzf_motor_low_seen = 0U;
    mzf_boundary_auto_timer_armed = false;
    mzf_boundary_auto_start_ms = 0U;
    mzf_fast3_start_delay_armed = false;
    mzf_fast3_start_delay_started_ms = 0U;
    mzf_paused_mid_pulse = false;
    mzf_pwm_low_first = false;
    mzf_pwm_bootstrap_pending = false;
    mzf_pwm_stop_pending = false;
    mzf_pwm_next_valid = false;
    mzf_pwm_paused_com_connected = false;
    mzf_pwm_paused_resume_level = 0U;
    mzf_pwm_current_compare_ticks = 1U;
    mzf_pwm_next_compare_ticks = 1U;
    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;
    mzf_native_copy_index = 0U;
    mzf_native_repeat_refill_requested = false;
    mzf_native_repeat_refill_ready = false;
    mzf_fifo_reset();
    mzf_loader_reset();
}

bool mzf_playback_prepare(const char *path,
                          file_format_t format,
                          loader_mode_t loader_mode,
                          uint16_t mzt_start_record,
                          bool motor_control_enabled)
{
    loader_mode_t first_record_mode = loader_mode;

    mzf_playback_stop();
    mzf_error_text[0] = '\0';
    mzf_wave_invert_signal = false;

    if ((path == NULL) || !file_format_is_sharp_tape(format))
    {
        mzf_set_error_P(PSTR("MZF ARG"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    mzf_format = format;
    mzf_source_path = path;
    mzf_requested_loader_mode = loader_mode;
    mzf_motor_control_enabled = motor_control_enabled;
    mzf_mzt_record_index = 0U;
    mzf_mzt_record_count = 0U;
    mzf_mzt_record_loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_mzt_record_loader_from_sidecar = false;
    mzf_mzt_record_title[0] = '\0';
    if ((format == FILE_FORMAT_MZT) && (mzt_start_record == 0U))
    {
        mzt_start_record = 1U;
    }

    /* AUTO is meaningful inside this module only for MZT. The controller has
       already resolved MZF .MFI and maps M12 AUTO to NORMAL 1:1. */
    if ((format != FILE_FORMAT_MZT) && (first_record_mode == LOADER_MODE_AUTO))
    {
        first_record_mode = LOADER_MODE_NORMAL_1_1;
    }
    mzf_configure_normal_speed(
        (first_record_mode == LOADER_MODE_AUTO) ?
            LOADER_MODE_NORMAL_1_1 : first_record_mode);

    if (!sdcard_file_open_read(path))
    {
        mzf_set_error_P(PSTR("MZF OPEN"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }

    mzf_file_size = sdcard_file_size();
    if (mzf_file_size < MZF_HEADER_BYTES)
    {
        sdcard_file_close();
        mzf_set_error_P(PSTR("MZF SHORT"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }

    if (format == FILE_FORMAT_MZT)
    {
        if (!mzf_locate_mzt_record(mzt_start_record))
        {
            sdcard_file_close();
            return false;
        }
        if (!mzf_resolve_mzt_loader_mode(mzf_mzt_record_index,
                                         &first_record_mode))
        {
            sdcard_file_close();
            return false;
        }
    }
    else
    {
        if (!mzf_calculate_total_duration() || !mzf_read_header_record())
        {
            sdcard_file_close();
            return false;
        }

        /* The scanner uses the generator's pulse widths and leader counts,
           so its exact half-millisecond duration is authoritative. */
        mzf_total_duration_ms =
            (mzf_exact_duration_half_ms == 0xFFFFFFFFUL) ?
                0xFFFFFFFFUL :
                (mzf_exact_duration_half_ms + 1UL) / 2UL;
    }

    if (!mzf_prepare_current_record(first_record_mode))
    {
        sdcard_file_close();
        return false;
    }

    mzf_state = MZF_PLAYBACK_READY;
    return true;
}

bool mzf_playback_start(void)
{
    if ((mzf_state != MZF_PLAYBACK_READY) && (mzf_state != MZF_PLAYBACK_PAUSED))
    {
        return false;
    }

    mzf_state = MZF_PLAYBACK_RUNNING;
    if (mzf_stage != MZF_STAGE_ULTRAFAST)
    {
        mz_sense_set(false);
    }

    if (mzf_paused_mid_pulse)
    {
        /* This low MOTOR belonged to a mid-block pause, not to the next
           header/data boundary. */
        mzf_motor_low_seen = 0U;
        mzf_pwm_resume_from_foreground();
        return true;
    }

    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        return mzf_start_ultrafast_output();
    }

    return mzf_start_normal_output();
}

bool mzf_playback_pause(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING)
    {
        return false;
    }

    if (mzf_fast3_start_delay_armed)
    {
        /* No Timer3 waveform is running yet. Preserve the armed wall-clock
           gap and let resume/service start the first pulse once it has elapsed. */
        mzf_state = MZF_PLAYBACK_PAUSED;
        return true;
    }

    /* A completed UL/UL800/UL700 record in MZT is parked in ULTRAFAST with
       boundary_waiting set. Handle that record boundary before the generic
       UL pause case; otherwise resume would call mzf_loader_begin() for the
       already-finished loader instead of preparing the next MZT record. */
    if (mzf_boundary_waiting)
    {
        if (!mzf_advance_after_boundary())
        {
            return false;
        }
        if (mzf_state == MZF_PLAYBACK_FINISHED)
        {
            return true;
        }

        mzf_state = MZF_PLAYBACK_PAUSED;
        return true;
    }

    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        mzf_state = MZF_PLAYBACK_PAUSED;
        return true;
    }

    mzf_pwm_pause_from_foreground();
    return true;
}

bool mzf_playback_resume(void)
{
    return mzf_playback_start();
}

void mzf_playback_stop(void)
{
    mzf_stop_timer_from_foreground(true);
    sdcard_file_close();
    mzf_fifo_reset();
    mzf_state = MZF_PLAYBACK_STOPPED;
    mzf_stage = MZF_STAGE_NONE;
    mzf_boundary_waiting = false;
    mzf_motor_low_seen = 0U;
    mzf_boundary_auto_timer_armed = false;
    mzf_boundary_auto_start_ms = 0U;
    mzf_fast3_start_delay_armed = false;
    mzf_fast3_start_delay_started_ms = 0U;
    mzf_paused_mid_pulse = false;
    mzf_pwm_low_first = false;
    mzf_pwm_bootstrap_pending = false;
    mzf_pwm_stop_pending = false;
    mzf_pwm_next_valid = false;
    mzf_pwm_paused_com_connected = false;
    mzf_pwm_paused_resume_level = 0U;
    mzf_pwm_current_compare_ticks = 1U;
    mzf_pwm_next_compare_ticks = 1U;
    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;
    mzf_source_path = NULL;
    mzf_requested_loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_motor_control_enabled = true;
    mzf_mzt_record_index = 0U;
    mzf_mzt_record_count = 0U;
    mzf_mzt_record_loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_mzt_record_loader_from_sidecar = false;
    mzf_mzt_record_title[0] = '\0';
    mzf_file_size = 0UL;
    mzf_total_duration_ms = 0UL;
    mzf_wave_invert_signal = false;
    mzf_configure_normal_speed(LOADER_MODE_NORMAL_1_1);
    mzf_record_data_length = 0UL;
    mzf_record_data_file_end = 0UL;
    mzf_record_data_file_start = 0UL;
    mzf_record_data_read = 0UL;
    mzf_original_data_offset = 0UL;
    mzf_original_data_length = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_native_copy_index = 0U;
    mzf_native_repeat_refill_requested = false;
    mzf_native_repeat_refill_ready = false;
    mzf_loader_reset();
    mz_sense_set(true);
}

void mzf_playback_service(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING)
    {
        return;
    }

    if (mzf_fast3_start_delay_armed)
    {
        if ((uint16_t)((uint16_t)millis() -
                       mzf_fast3_start_delay_started_ms) <
            MZF_MZ700_FAST3_START_DELAY_MS)
        {
            return;
        }

        mzf_fast3_start_delay_armed = false;
        if (!mzf_start_normal_output_immediate() &&
            (mzf_state == MZF_PLAYBACK_RUNNING))
        {
            mzf_set_error_P(PSTR("MZF START"), MZF_PLAYBACK_BAD_FILE);
        }
        return;
    }

    if (mzf_boundary_waiting)
    {
        mzf_service_boundary_auto_continue();
        return;
    }

    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        if (!mzf_loader_pump(MZF_LOADER_PUMP_BYTES))
        {
            mzf_set_error(mzf_loader_get_error_text(), MZF_PLAYBACK_IO_ERROR);
            return;
        }
        if (mzf_loader_is_finished())
        {
            loader_mode_t next_loader_mode;
            bool next_loader_from_sidecar;

            mzf_boundary_waiting = false;
            mzf_boundary_auto_timer_armed = false;
            mzf_boundary_auto_start_ms = 0U;

            /*
                UL -> UL-family must stop: the second UL load is not valid
                after the first UL program has taken control.

                Resolve record n+1 only once. The same exact loader decision is
                used both for this UL-family guard and, when permitted, for
                preparing the next record:
                  - AUTO: RECORD=n+1 from MTI, fallback NORMAL 1:1
                  - MANUAL: the manual session loader still has priority
            */
            if ((mzf_format == FILE_FORMAT_MZT) &&
                (mzf_mzt_record_index < mzf_mzt_record_count))
            {
                if (!mzf_resolve_next_mzt_loader(
                        &next_loader_mode, &next_loader_from_sidecar))
                {
                    return;
                }

                if (!mzf_loader_mode_is_ul_family(next_loader_mode))
                {
                    if (!mzf_start_next_mzt_record_resolved(
                            next_loader_mode, next_loader_from_sidecar))
                    {
                        return;
                    }

                    if (mzf_state != MZF_PLAYBACK_FINISHED)
                    {
                        /* UL completion releases SENSE; restore the ordinary
                           CMT gate before the following non-UL header starts. */
                        mz_sense_set(false);
                        if (!mzf_start_normal_output() &&
                            (mzf_state == MZF_PLAYBACK_RUNNING))
                        {
                            mzf_set_error_P(PSTR("MZF START"),
                                            MZF_PLAYBACK_BAD_FILE);
                        }
                    }
                    return;
                }
            }

            mzf_state = MZF_PLAYBACK_FINISHED;
            mz_sense_set(true);
        }
        return;
    }

    if (mzf_native_mz700 &&
        (mzf_stage == MZF_STAGE_DATA) &&
        mzf_native_repeat_refill_requested &&
        !mzf_prepare_native_mz700_repeat())
    {
        if (mzf_state == MZF_PLAYBACK_RUNNING)
        {
            mzf_set_error_P(PSTR("MZ7 REFILL"), MZF_PLAYBACK_IO_ERROR);
        }
        return;
    }

    if (((mzf_stage == MZF_STAGE_DATA) ||
         (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)) && !mzf_boundary_waiting &&
        (mzf_fifo_used_snapshot() <= MZF_REFILL_RESERVE) &&
        (mzf_record_data_read < mzf_record_data_length))
    {
        (void)mzf_refill_data_once();
    }
}

mzf_playback_state_t mzf_playback_get_state(void)
{
    uint8_t state;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        state = mzf_state;
    }
    return (mzf_playback_state_t)state;
}

const char *mzf_playback_get_error_text(void) { return mzf_error_text; }

uint8_t mzf_playback_get_buffer_fill_percent(void)
{
    uint16_t used;
    uint32_t consumed;
    uint32_t remaining;
    uint32_t target;
    uint32_t percent;

    /* ULTRAFAST and inactive stages do not consume the MZF read FIFO. */
    if ((mzf_stage != MZF_STAGE_HEADER) &&
        (mzf_stage != MZF_STAGE_DATA) &&
        (mzf_stage != MZF_STAGE_TAPE_TURBO_DATA))
    {
        return 100U;
    }

    /* Header-only UL variants hand off to a different transport after the
       generated header; this FIFO has no pending source obligation there. */
    if ((mzf_stage == MZF_STAGE_HEADER) && mzf_loader_is_header_only())
    {
        return 100U;
    }
    if (mzf_record_data_length == 0UL)
    {
        return 100U;
    }

    used = mzf_fifo_used_snapshot();
    if (used > MZF_FIFO_CAPACITY)
    {
        return 0U;
    }
    if (mzf_record_data_read < (uint32_t)used)
    {
        return 0U;
    }

    /* Readiness is measured against what can still be useful, not against the
       physical FIFO size. Bytes already popped by the ISR are consumed; bytes
       already read from SD but not popped remain prepared in RAM. */
    consumed = mzf_record_data_read - (uint32_t)used;
    if (consumed >= mzf_record_data_length)
    {
        return 100U;
    }

    remaining = mzf_record_data_length - consumed;
    target = (remaining < (uint32_t)MZF_FIFO_CAPACITY) ?
        remaining : (uint32_t)MZF_FIFO_CAPACITY;
    if (target == 0UL)
    {
        return 100U;
    }

    percent = ((uint32_t)used * 100UL) / target;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}

uint16_t mzf_playback_get_mzt_record_index(void)
{
    return (mzf_format == FILE_FORMAT_MZT) ? mzf_mzt_record_index : 0U;
}

uint16_t mzf_playback_get_mzt_record_count(void)
{
    return (mzf_format == FILE_FORMAT_MZT) ? mzf_mzt_record_count : 0U;
}

const char *mzf_playback_get_mzt_record_title(void)
{
    return (mzf_format == FILE_FORMAT_MZT) ? mzf_mzt_record_title : "";
}

loader_mode_t mzf_playback_get_mzt_record_loader_mode(void)
{
    return (mzf_format == FILE_FORMAT_MZT) ?
        mzf_mzt_record_loader_mode : mzf_requested_loader_mode;
}

bool mzf_playback_get_mzt_record_loader_from_sidecar(void)
{
    return (mzf_format == FILE_FORMAT_MZT) &&
           mzf_mzt_record_loader_from_sidecar;
}


uint32_t mzf_playback_get_total_duration_ms(void)
{
    return mzf_total_duration_ms;
}

static uint16_t mzf_progress_gap_pulses(mzf_stage_t stage)
{
    if (stage == MZF_STAGE_HEADER)
    {
        return mzf_profile_header_leader;
    }
    if (stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_TC_1_2:
                return MZF_TC_1_2_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_3:
                return MZF_TC_1_3_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_4:
                return MZF_TC_1_4_TURBO_GAP_SHORT_PULSES;
            default:
                return MZF_IC_TURBO_GAP_SHORT_PULSES;
        }
    }
    return mzf_profile_data_leader;
}

static uint16_t mzf_progress_mark_long_pulses(mzf_stage_t stage)
{
    if (stage == MZF_STAGE_HEADER)
    {
        return MZF_MZ800_LONG_MARK_LONG_PULSES;
    }
    return (stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_LONG_PULSES :
        MZF_MZ800_SHORT_MARK_LONG_PULSES;
}

static uint16_t mzf_progress_mark_short_pulses(mzf_stage_t stage)
{
    if (stage == MZF_STAGE_HEADER)
    {
        return MZF_MZ800_LONG_MARK_SHORT_PULSES;
    }
    return (stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_SHORT_PULSES :
        MZF_MZ800_SHORT_MARK_SHORT_PULSES;
}

static uint16_t mzf_progress_mark_final_pulses(mzf_stage_t stage)
{
    return (stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_FINAL_LONG_PULSES :
        MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES;
}

static uint16_t mzf_progress_trailing_pulses(mzf_stage_t stage)
{
    return (mzf_loader_is_tc_turbo() &&
            ((stage == MZF_STAGE_DATA) ||
             (stage == MZF_STAGE_TAPE_TURBO_DATA))) ?
        MZF_TC_LOADER_TRAILING_SHORT_PULSES :
        MZF_MZ800_TRAILING_LONG_PULSES;
}

static uint16_t mzf_progress_done_pulses(uint16_t total, uint16_t remaining)
{
    return (remaining >= total) ? 0U : (uint16_t)(total - remaining);
}

static uint32_t mzf_progress_stage_total_units(mzf_stage_t stage,
                                               uint32_t byte_count)
{
    return (uint32_t)mzf_progress_gap_pulses(stage) +
           (uint32_t)mzf_progress_mark_long_pulses(stage) +
           (uint32_t)mzf_progress_mark_short_pulses(stage) +
           (uint32_t)mzf_progress_mark_final_pulses(stage) +
           (byte_count * 9UL) + 18UL +
           (uint32_t)mzf_progress_trailing_pulses(stage);
}

static uint32_t mzf_progress_byte_units(mzf_normal_step_t step,
                                        uint32_t bytes_read,
                                        uint32_t byte_count,
                                        uint8_t bits_remaining)
{
    if (bytes_read > byte_count) bytes_read = byte_count;

    if (((step == MZF_STEP_BYTE_BITS) ||
         (step == MZF_STEP_BYTE_STOP)) && (bytes_read != 0UL))
    {
        uint32_t done = (bytes_read - 1UL) * 9UL;
        done += (step == MZF_STEP_BYTE_BITS) ?
            (uint32_t)(8U - bits_remaining) : 8UL;
        return done;
    }

    return bytes_read * 9UL;
}

static uint32_t mzf_progress_checksum_units(mzf_normal_step_t step,
                                            uint8_t checksum_byte_index,
                                            uint8_t bits_remaining)
{
    uint32_t done;

    if (checksum_byte_index > 2U) checksum_byte_index = 2U;
    done = (uint32_t)checksum_byte_index * 9UL;

    if (((step == MZF_STEP_CHECKSUM_BITS) ||
         (step == MZF_STEP_CHECKSUM_STOP)) && (checksum_byte_index != 0U))
    {
        done = (uint32_t)(checksum_byte_index - 1U) * 9UL;
        done += (step == MZF_STEP_CHECKSUM_BITS) ?
            (uint32_t)(8U - bits_remaining) : 8UL;
    }

    return (done > 18UL) ? 18UL : done;
}

static uint32_t mzf_progress_stage_done_units(mzf_stage_t stage,
                                              mzf_normal_step_t step,
                                              uint16_t loop,
                                              uint32_t bytes_read,
                                              uint32_t byte_count,
                                              uint8_t bits_remaining,
                                              uint8_t checksum_byte_index)
{
    uint16_t gap = mzf_progress_gap_pulses(stage);
    uint16_t mark_long = mzf_progress_mark_long_pulses(stage);
    uint16_t mark_short = mzf_progress_mark_short_pulses(stage);
    uint16_t mark_final = mzf_progress_mark_final_pulses(stage);
    uint32_t preamble = (uint32_t)gap + (uint32_t)mark_long +
                        (uint32_t)mark_short + (uint32_t)mark_final;
    uint32_t data_units = byte_count * 9UL;

    switch (step)
    {
        case MZF_STEP_BEGIN:
            return 0UL;
        case MZF_STEP_GAP:
            return (uint32_t)mzf_progress_done_pulses(gap, loop);
        case MZF_STEP_TAPE_MARK_LONG:
            return (uint32_t)gap +
                   (uint32_t)mzf_progress_done_pulses(mark_long, loop);
        case MZF_STEP_TAPE_MARK_SHORT:
            return (uint32_t)gap + (uint32_t)mark_long +
                   (uint32_t)mzf_progress_done_pulses(mark_short, loop);
        case MZF_STEP_TAPE_MARK_FINAL:
            return (uint32_t)gap + (uint32_t)mark_long +
                   (uint32_t)mark_short +
                   (uint32_t)mzf_progress_done_pulses(mark_final, loop);
        case MZF_STEP_BYTE_LOAD:
        case MZF_STEP_BYTE_BITS:
        case MZF_STEP_BYTE_STOP:
            return preamble + mzf_progress_byte_units(step, bytes_read,
                                                       byte_count,
                                                       bits_remaining);
        case MZF_STEP_CHECKSUM_LOAD:
        case MZF_STEP_CHECKSUM_BITS:
        case MZF_STEP_CHECKSUM_STOP:
            return preamble + data_units +
                   mzf_progress_checksum_units(step, checksum_byte_index,
                                               bits_remaining);
        case MZF_STEP_TRAILING_LONGS:
            return preamble + data_units + 18UL +
                   (uint32_t)mzf_progress_done_pulses(
                       mzf_progress_trailing_pulses(stage), loop);
        case MZF_STEP_BOUNDARY:
            return mzf_progress_stage_total_units(stage, byte_count);
        default:
            return 0UL;
    }
}

uint8_t mzf_playback_get_progress_percent(void)
{
    uint32_t percent;
    uint32_t total;
    uint32_t done = 0UL;
    uint32_t header_units;
    uint32_t loader_units = 0UL;
    uint32_t data_units = 0UL;
    uint32_t stage_bytes_read;
    uint16_t read_sequence;
    uint16_t loop;
    uint8_t header_offset;
    uint8_t bits_remaining;
    uint8_t checksum_byte_index;
    mzf_stage_t stage;
    mzf_normal_step_t step;

    if (!mzf_loader_is_active())
    {
        return 0U;
    }

    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        return mzf_loader_get_progress_percent();
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        stage = mzf_stage;
        step = mzf_normal_step;
        loop = mzf_normal_loop;
        header_offset = mzf_header_offset;
        read_sequence = mzf_fifo_read_sequence;
        bits_remaining = mzf_normal_bits_remaining;
        checksum_byte_index = mzf_normal_checksum_byte_index;
    }

    header_units = mzf_progress_stage_total_units(MZF_STAGE_HEADER,
                                                  MZF_HEADER_BYTES);
    total = header_units;

    if (mzf_loader_is_ic_turbo() || mzf_loader_is_mz700_fast3())
    {
        data_units = mzf_progress_stage_total_units(MZF_STAGE_TAPE_TURBO_DATA,
                                                    mzf_original_data_length);
        total += data_units;
    }
    else if (!mzf_loader_is_header_only())
    {
        loader_units = mzf_progress_stage_total_units(
            MZF_STAGE_DATA, (uint32_t)mzf_loader_get_loader_size());
        total += loader_units;
        if (mzf_loader_is_tc_turbo())
        {
            data_units = mzf_progress_stage_total_units(
                MZF_STAGE_TAPE_TURBO_DATA, mzf_original_data_length);
            total += data_units;
        }
    }

    if (total == 0UL)
    {
        return 0U;
    }

    if (stage == MZF_STAGE_HEADER)
    {
        done = mzf_progress_stage_done_units(stage, step, loop,
                                             (uint32_t)header_offset,
                                             MZF_HEADER_BYTES,
                                             bits_remaining,
                                             checksum_byte_index);
    }
    else if (stage == MZF_STAGE_DATA)
    {
        stage_bytes_read = (uint32_t)read_sequence;
        done = header_units +
               mzf_progress_stage_done_units(stage, step, loop,
                                             stage_bytes_read,
                                             (uint32_t)mzf_loader_get_loader_size(),
                                             bits_remaining,
                                             checksum_byte_index);
    }
    else if (stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        stage_bytes_read = (uint32_t)read_sequence;
        done = header_units + loader_units +
               mzf_progress_stage_done_units(stage, step, loop,
                                             stage_bytes_read,
                                             mzf_original_data_length,
                                             bits_remaining,
                                             checksum_byte_index);
    }
    else if (stage != MZF_STAGE_NONE)
    {
        done = total;
    }

    if (done > total) done = total;
    percent = (done * 100UL) / total;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}
mzf_playback_phase_t mzf_playback_get_progress_phase(void)
{
    if (!mzf_loader_is_active())
    {
        return MZF_PLAYBACK_PHASE_NORMAL;
    }

    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        return MZF_PLAYBACK_PHASE_ULTRAFAST_DATA;
    }

    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        if (mzf_loader_is_mz700_fast3())
        {
            return mzf_loader_is_mz700_fast3_high() ?
                MZF_PLAYBACK_PHASE_MZ700_FAST3_HIGH :
                MZF_PLAYBACK_PHASE_MZ700_FAST3_LOW;
        }
        return mzf_loader_is_tc_turbo() ?
            MZF_PLAYBACK_PHASE_TC_TURBO_DATA :
            MZF_PLAYBACK_PHASE_IC_TURBO_DATA;
    }

    if (mzf_loader_is_ic_turbo())
    {
        return MZF_PLAYBACK_PHASE_IC_TURBO_DATA;
    }

    if (mzf_loader_is_tc_turbo())
    {
        return MZF_PLAYBACK_PHASE_TC_TURBO_LOADER;
    }

    switch (mzf_loader_get_variant())
    {
        case MZF_LOADER_VARIANT_LOW:
            return MZF_PLAYBACK_PHASE_ULTRAFAST_LOADER_LOW;
        case MZF_LOADER_VARIANT_HIGH:
            return MZF_PLAYBACK_PHASE_ULTRAFAST_LOADER_HIGH;
        case MZF_LOADER_VARIANT_MZ800_HEADER:
            return MZF_PLAYBACK_PHASE_ULTRAFAST_HEADER;
        case MZF_LOADER_VARIANT_MZ700_UL_LOW:
            return MZF_PLAYBACK_PHASE_MZ700_UL_LOW;
        case MZF_LOADER_VARIANT_MZ700_UL_HIGH:
            return MZF_PLAYBACK_PHASE_MZ700_UL_HIGH;
        case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            return MZF_PLAYBACK_PHASE_MZ700_FAST3_LOW;
        case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH:
            return MZF_PLAYBACK_PHASE_MZ700_FAST3_HIGH;
        default:
            return MZF_PLAYBACK_PHASE_NORMAL;
    }
}

bool mzf_playback_is_ul_loader_active(void)
{
    return mzf_loader_is_ul_active();
}

static void mzf_sample_motor_from_isr(void)
{
    if (mz_motor_sample_from_isr() == 0U)
    {
        mzf_motor_low_seen = 1U;
    }
}

/* OC3B has already generated the physical phase edge when this hook runs. */
bool mzf_playback_timer3_compb_from_isr(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING)
    {
        return false;
    }

    mzf_sample_motor_from_isr();

    /* Prime the double buffer during the first pulse.  Every later pulse is
       prepared at overflow, providing a full PWM period of timing margin. */
    if (mzf_pwm_bootstrap_pending)
    {
        /* Starting at TOP deliberately produced one initial overflow before
           this first compare.  Discard it before enabling the OVF pipeline. */
        TIFR3 = _BV(TOV3);
        mzf_pwm_bootstrap_pending = false;
        if (mzf_queue_next_pwm_pulse_from_isr())
        {
            TIMSK3 |= _BV(TOIE3);
            return true;
        }

        if (mzf_pwm_terminal_pending != MZF_PWM_TERMINAL_NONE)
        {
            mzf_pwm_arm_terminal_from_isr();
            mzf_pwm_disconnect_output_from_isr();
            return true;
        }

        mzf_stop_timer_from_isr();
        return false;
    }

    /* A terminal pulse must retain its second phase through TOP.  Disconnect
       OC3B here so BOTTOM cannot start an unwanted repeat before OVF stops. */
    if (mzf_pwm_stop_pending)
    {
        mzf_pwm_disconnect_output_from_isr();
    }
    return true;
}

static bool mzf_playback_timer3_ovf_from_isr(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING)
    {
        mzf_stop_timer_from_isr();
        return false;
    }

    mzf_sample_motor_from_isr();
    if (mzf_pwm_stop_pending)
    {
        mzf_pwm_finish_terminal_from_isr();
        return false;
    }

    if (!mzf_pwm_next_valid)
    {
        mzf_set_error_from_isr_P(PSTR("PWM PIPE"), MZF_PLAYBACK_BAD_FILE);
        mzf_stop_timer_from_isr();
        return false;
    }

    /* The buffered OCR values became active at this BOTTOM. */
    mzf_pwm_current_compare_ticks = mzf_pwm_next_compare_ticks;
    mzf_pwm_next_valid = false;

    if (mzf_queue_next_pwm_pulse_from_isr())
    {
        return true;
    }

    if (mzf_pwm_terminal_pending != MZF_PWM_TERMINAL_NONE)
    {
        mzf_pwm_arm_terminal_from_isr();
        return true;
    }

    mzf_stop_timer_from_isr();
    return false;
}

ISR(TIMER3_OVF_vect)
{
    if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_MZF)
    {
        (void)mzf_playback_timer3_ovf_from_isr();
        return;
    }

    TIMSK3 &= (uint8_t)~_BV(TOIE3);
    TIFR3 = _BV(TOV3);
}
