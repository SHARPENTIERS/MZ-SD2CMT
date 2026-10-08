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

#define MZF_HEADER_BYTES 128U
#define MZF_HEADER_DATA_LENGTH_OFFSET 0x12U
#define MZQ_HEADER_BYTES 64U
#define MZQ_HEADER_SHIFT_OFFSET 22U
#define MZQ_MAX_FILES 50U
#define MZQ_DIRECTORY_POSITION_BYTES 3U
#define MZQ_DIRECTORY_ENTRY_BYTES (2U * MZQ_DIRECTORY_POSITION_BYTES)
#define MZQ_DIRECTORY_OFFSET MZF_HEADER_BYTES
#define MZQ_DIRECTORY_BYTES (MZQ_MAX_FILES * MZQ_DIRECTORY_ENTRY_BYTES)
#define MZQ_RAW_BUFFER_OFFSET (MZQ_DIRECTORY_OFFSET + MZQ_DIRECTORY_BYTES)
#define MZQ_RAW_BUFFER_BYTES (512U - MZQ_RAW_BUFFER_OFFSET)
#define MZQ_CACHE_NONE 0U
#define MZQ_CACHE_PHYSICAL 1U
#define MZQ_CACHE_QDF 2U
#define MZQ_PHYSICAL_POSITION_MAX 0x7FFFFUL
#define MZQ_PHYSICAL_DURATION_MAX 0x03FFU

/*
   MZF pulse reference (source us, SHORT/LONG = HIGH/LOW; H/D = header/data):

   Mode or stage                SHORT H/L       LONG H/L        Leader H/D Origin
   NRL1:1, IC hdr, TC hdr/ldr   237.956/context 469.145/context 11000/5500 ROM800
   UL pre-transfer data/marks   237.956/context 469.145/context see below  ROM800
   IC1:1 data                   234.573/263.894 469.145/494.802 -/5500     IC1200
   NRL1:2, IC1:2 data           113.621/139.278 234.573/260.229 11000/5500 IC2400
   NRL1:3, IC1:3 data            87.965/124.617 175.930/223.577 11000/5500 IC2800
   NRL1:4, IC1:4 data            76.969/117.286 157.604/179.595 11000/5500 IC3200
   MZ700 NRL1:1, MZ7-3 hdr      240.000/264.000 464.000/494.000 11000/5500 ROM700
   MZ700 FAST3 data              80.000/80.000  160.000/160.000 -/5500     ROM700
   TC1:1 data                   250.909/250.000 500.909/500.909 -/5500     TurboCopy
   TC1:2 data                   141.818/140.909 282.727/282.727 -/5500     TurboCopy
   TC1:3 data                   105.455/104.545 210.000/210.000 -/5500     TurboCopy
   UL/UL800 leader              237.956/258.819 -               2000/1000  ROM800
   UL700 leader                 240.000/264.000 -               2000/1000  ROM700

   ROM800 LOW context: leader S=258.819; mark S/L=256.000/487.189;
   data S->S/S->L=255.436/252.617, L->S/L->L=486.626/483.806 us.
   IC values are Intercopy writer-derived. TC values are Turbo Copy MODE3
   timer-derived. All H/L values describe the logical Sharp/MZ 8255-side
   waveform. Timer3 maps logical HIGH to connector READ LOW and logical LOW
   to connector READ HIGH without exchanging the two durations.
*/
#define MZF_TICKS_PER_US ((uint16_t)(F_CPU / 1000000UL))
#define MZF_US_TO_TICKS(us) ((uint16_t)((uint32_t)(us) * MZF_TICKS_PER_US))
#define MZF_SHORT_HIGH_TICKS ((uint16_t)3807U)
#define MZF_SHORT_LOW_TICKS  ((uint16_t)4087U)
#define MZF_LONG_HIGH_TICKS  ((uint16_t)7506U)
#define MZF_LONG_LOW_TICKS   ((uint16_t)7786U)
#define MZF_MZ800_LEADER_SHORT_LOW_TICKS ((uint16_t)4141U)
#define MZF_MZ800_MARK_SHORT_LOW_TICKS   ((uint16_t)4096U)
#define MZF_MZ800_MARK_LONG_LOW_TICKS    ((uint16_t)7795U)
#define MZF_MZ800_DATA_SHORT_LONG_LOW_TICKS ((uint16_t)4042U)
#define MZF_MZ800_DATA_LONG_LONG_LOW_TICKS  ((uint16_t)7741U)
#define MZF_MZ700_LEADER_SHORT_HIGH_TICKS MZF_US_TO_TICKS(240U)
#define MZF_MZ700_LEADER_SHORT_LOW_TICKS  MZF_US_TO_TICKS(264U)

#define MZF_IC_1_1_SHORT_HIGH_TICKS ((uint16_t)3753U)
#define MZF_IC_1_1_SHORT_LOW_TICKS  ((uint16_t)4222U)
#define MZF_IC_1_1_LONG_HIGH_TICKS  ((uint16_t)7506U)
#define MZF_IC_1_1_LONG_LOW_TICKS   ((uint16_t)7917U)
#define MZF_IC_1_4_SHORT_HIGH_TICKS ((uint16_t)1232U)
#define MZF_IC_1_4_SHORT_LOW_TICKS  ((uint16_t)1877U)
#define MZF_IC_1_4_LONG_HIGH_TICKS  ((uint16_t)2522U)
#define MZF_IC_1_4_LONG_LOW_TICKS   ((uint16_t)2874U)
#define MZF_IC_1_3_SHORT_HIGH_TICKS ((uint16_t)1407U)
#define MZF_IC_1_3_SHORT_LOW_TICKS  ((uint16_t)1994U)
#define MZF_IC_1_3_LONG_HIGH_TICKS  ((uint16_t)2815U)
#define MZF_IC_1_3_LONG_LOW_TICKS   ((uint16_t)3577U)
#define MZF_MZ700_3X_SHORT_TICKS MZF_US_TO_TICKS(80U)
#define MZF_MZ700_3X_LONG_TICKS  MZF_US_TO_TICKS(160U)
#define MZF_IC_1_2_SHORT_HIGH_TICKS ((uint16_t)1818U)
#define MZF_IC_1_2_SHORT_LOW_TICKS  ((uint16_t)2228U)
#define MZF_IC_1_2_LONG_HIGH_TICKS  ((uint16_t)3753U)
#define MZF_IC_1_2_LONG_LOW_TICKS   ((uint16_t)4164U)
#define MZF_TC_1_1_SHORT_HIGH_TICKS ((uint16_t)4015U)
#define MZF_TC_1_1_SHORT_LOW_TICKS  ((uint16_t)4000U)
#define MZF_TC_1_1_LONG_HIGH_TICKS  ((uint16_t)8015U)
#define MZF_TC_1_1_LONG_LOW_TICKS   ((uint16_t)8015U)
#define MZF_TC_1_3_SHORT_HIGH_TICKS ((uint16_t)1687U)
#define MZF_TC_1_3_SHORT_LOW_TICKS  ((uint16_t)1673U)
#define MZF_TC_1_3_LONG_HIGH_TICKS  ((uint16_t)3360U)
#define MZF_TC_1_3_LONG_LOW_TICKS   ((uint16_t)3360U)
#define MZF_TC_1_2_SHORT_HIGH_TICKS ((uint16_t)2269U)
#define MZF_TC_1_2_SHORT_LOW_TICKS  ((uint16_t)2255U)
#define MZF_TC_1_2_LONG_HIGH_TICKS  ((uint16_t)4524U)
#define MZF_TC_1_2_LONG_LOW_TICKS   ((uint16_t)4524U)

#define MZF_MZ800_LONG_GAP_SHORT_PULSES 11000U
#define MZF_MZ800_SHORT_GAP_SHORT_PULSES 5500U
#define MZF_MZ800_LONG_MARK_LONG_PULSES 40U
#define MZF_MZ800_LONG_MARK_SHORT_PULSES 40U
#define MZF_MZ800_SHORT_MARK_LONG_PULSES 20U
#define MZF_MZ800_SHORT_MARK_SHORT_PULSES 20U
#define MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES 2U
#define MZF_MZ800_TRAILING_LONG_PULSES 2U
#define MZF_TC_LOADER_TRAILING_SHORT_PULSES 98U

#define MZF_UL_HEADER_LEADER_SHORT_PULSES 5500U
#define MZF_UL_DATA_LEADER_SHORT_PULSES   5500U
#define MZF_UL_LEADER_TIMING_NONE  0U
#define MZF_UL_LEADER_TIMING_MZ800 1U
#define MZF_UL_LEADER_TIMING_MZ700 2U

#define MZF_FIFO_BYTES WAV_SAMPLE_STREAM_BUFFER_BYTES
#define MZF_FIFO_CAPACITY (MZF_FIFO_BYTES - 1U)
#define MZF_FIFO_MASK (MZF_FIFO_BYTES - 1U)
#define MZF_REFILL_BLOCK WAV_SAMPLE_STREAM_REFILL_BLOCK
#define MZF_REFILL_RESERVE (MZF_FIFO_CAPACITY - MZF_REFILL_BLOCK)
#define MZF_BOUNDARY_AUTO_CONTINUE_MS 120U
#define MZF_IC_TURBO_START_DELAY_MS 345U
#define MZF_MZ700_FAST3_START_DELAY_MS 400U
#define MZF_TC_TURBO_START_DELAY_MS 110U
#define MZF_IC_TURBO_GAP_SHORT_PULSES 5500U
#define MZF_TC_1_1_TURBO_GAP_SHORT_PULSES 5500U
#define MZF_TC_1_3_TURBO_GAP_SHORT_PULSES 5500U
#define MZF_TC_1_2_TURBO_GAP_SHORT_PULSES 5500U
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

typedef enum
{
    MZF_PULSE_REGION_LEADER = 0,
    MZF_PULSE_REGION_TAPE_MARK,
    MZF_PULSE_REGION_DATA
} mzf_pulse_region_t;

static volatile uint8_t mzf_state = MZF_PLAYBACK_STOPPED;
#define mzf_error_text cmt_playback_backend_error
static file_format_t mzf_format = FILE_FORMAT_UNKNOWN;
static const char *mzf_source_path = NULL;
static loader_mode_t mzf_requested_loader_mode = LOADER_MODE_NORMAL_1_1;
static bool mzf_motor_control_enabled = true;
static uint16_t mzf_mzt_record_index = 0U;
static uint16_t mzf_mzt_record_count = 0U;
static loader_mode_t mzf_mzt_record_loader_mode = LOADER_MODE_NORMAL_1_1;
static bool mzf_mzt_record_loader_from_sidecar = false;
static char mzf_mzt_record_title[18];
static uint8_t mzf_normal_speed_divisor = 1U;
static bool mzf_native_mz700 = false;
static uint16_t mzf_profile_short_high_ticks = MZF_SHORT_HIGH_TICKS;
static uint16_t mzf_profile_short_low_ticks = MZF_SHORT_LOW_TICKS;
static uint16_t mzf_profile_long_high_ticks = MZF_LONG_HIGH_TICKS;
static uint16_t mzf_profile_long_low_ticks = MZF_LONG_LOW_TICKS;
static bool mzf_profile_uses_mz800_rom_timing = true;
static uint8_t mzf_ul_leader_timing = MZF_UL_LEADER_TIMING_NONE;
static uint16_t mzf_profile_header_leader = MZF_MZ800_LONG_GAP_SHORT_PULSES;
static uint16_t mzf_profile_data_leader = MZF_MZ800_SHORT_GAP_SHORT_PULSES;
static uint8_t mzf_profile_header_mark_long = MZF_MZ800_LONG_MARK_LONG_PULSES;
static uint8_t mzf_profile_header_mark_short = MZF_MZ800_LONG_MARK_SHORT_PULSES;
static uint8_t mzf_profile_data_mark_long = MZF_MZ800_SHORT_MARK_LONG_PULSES;
static uint8_t mzf_profile_data_mark_short = MZF_MZ800_SHORT_MARK_SHORT_PULSES;
static uint8_t mzf_profile_final_mark_long = MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES;
static uint16_t mzf_profile_duplicate_gap = 0U;

#define mzf_header cmt_mode_scratch.edge_record_stage_bytes
#define mzf_mzq_directory_bytes \
    (cmt_mode_scratch.edge_record_stage_bytes + MZQ_DIRECTORY_OFFSET)
#define mzf_mzq_raw_buffer \
    (cmt_mode_scratch.edge_record_stage_bytes + MZQ_RAW_BUFFER_OFFSET)
static_assert(MZQ_RAW_BUFFER_BYTES >= 64U,
              "QD workspace must retain a useful SD read buffer");
static volatile uint8_t mzf_header_offset = 0U;
static volatile uint16_t mzf_fifo_read_sequence = 0U;
static volatile uint16_t mzf_fifo_write_sequence = 0U;
static volatile uint8_t mzf_fifo_source_finished = 1U;
static uint32_t mzf_file_size = 0UL;
static uint32_t mzf_record_data_length = 0UL;
static uint32_t mzf_record_data_file_end = 0UL;
static uint32_t mzf_record_data_file_start = 0UL;
static uint32_t mzf_record_data_read = 0UL;
static uint32_t mzf_original_data_offset = 0UL;
static uint32_t mzf_original_data_length = 0UL;
static bool mzf_mzq_physical_source = false;
static uint32_t mzf_mzq_track_offset = 0UL;
static uint32_t mzf_mzq_track_length = 0UL;
static uint8_t mzf_mzq_data_phase = 0U;
static uint8_t mzf_mzq_preferred_phase = 0U;
static uint32_t mzf_mzq_body_logical_offset = 0UL;
static uint32_t mzf_mzq_source_position = 0UL;
static uint32_t mzf_mzq_raw_buffer_offset = 0UL;
static uint16_t mzf_mzq_raw_buffer_length = 0U;
/* The persistent QD directory lives in the otherwise unused tail of
   cmt_mode_scratch. QDF uses two 24-bit positions. Physical MFM entries pack
   two 19-bit logical positions and a 10-bit duration into the same six bytes. */
static bool mzf_mzq_directory_valid = false;
static uint8_t mzf_mzq_directory_kind = MZQ_CACHE_NONE;
static uint32_t mzf_mzq_directory_file_size = 0UL;
static uint32_t mzf_mzq_directory_track_offset = 0UL;
static uint32_t mzf_mzq_directory_track_length = 0UL;
static uint8_t mzf_mzq_directory_phase = 0U;
static uint8_t mzf_mzq_directory_count = 0U;
static mzf_qd_analysis_progress_callback_t mzf_qd_progress_callback = NULL;
static mzf_qd_analysis_cancel_callback_t mzf_qd_cancel_callback = NULL;
static bool mzf_qd_analysis_active = false;
static bool mzf_qd_analysis_cancelled = false;
static bool mzf_qd_load_active = false;
static bool mzf_qd_load_cancelled = false;
static uint8_t mzf_qd_last_progress = 0xFFU;
static bool mzf_tape_turbo_payload_prepared = false;
static bool mzf_mzq_duration_deferred = false;
static uint32_t mzf_total_duration_ms = 0UL;
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
static volatile bool mzf_boundary_waiting = false;
static volatile uint8_t mzf_motor_low_seen = 0U;
static bool mzf_paused_mid_pulse = false;
static bool mzf_pwm_bootstrap_pending = false;
static bool mzf_pwm_stop_pending = false;
static bool mzf_pwm_next_valid = false;
static bool mzf_pwm_paused_com_connected = false;
static uint8_t mzf_pwm_paused_resume_level = 0U;
static uint16_t mzf_pwm_current_compare_ticks = 1U;
static uint16_t mzf_pwm_next_compare_ticks = 1U;
static volatile uint8_t mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;
static bool mzf_boundary_auto_timer_armed = false;
static uint16_t mzf_boundary_auto_start_ms = 0U;
static bool mzf_fast3_start_delay_armed = false;
static uint16_t mzf_fast3_start_delay_started_ms = 0U;

static void mzf_stop_timer_from_foreground(bool force_low);

static void mzf_set_error_P(PGM_P text, mzf_playback_state_t state)
{
    flash_text_copy(mzf_error_text, sizeof(mzf_error_text), text);
    mzf_state = (uint8_t)state;
    if (mzf_format == FILE_FORMAT_MZQ)
    {
        mzf_stop_timer_from_foreground(true);
        mz_sense_set(true);
    }
}

static void mzf_set_error(const char *text, mzf_playback_state_t state)
{
    if (text == NULL) flash_text_copy(mzf_error_text, sizeof(mzf_error_text), PSTR("MZF ERROR"));
    else
    {
        strncpy(mzf_error_text, text, sizeof(mzf_error_text) - 1U);
        mzf_error_text[sizeof(mzf_error_text) - 1U] = '\0';
    }
    mzf_state = (uint8_t)state;
    if (mzf_format == FILE_FORMAT_MZQ)
    {
        mzf_stop_timer_from_foreground(true);
        mz_sense_set(true);
    }
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
    TCCR3A = 0U; TCCR3B = 0U;
    TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
    if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_MZF)
        timer3b_owner_set_from_isr(TIMER3B_OWNER_NONE);
    mzf_pwm_bootstrap_pending = false;
    mzf_pwm_stop_pending = false;
    mzf_pwm_next_valid = false;
    mz_read_set_fast_from_isr(read_level);
    monitor_set_tape_activity_from_isr(false);
}

static void mzf_stop_timer_from_isr(void) { mzf_stop_timer_from_isr_with_read(0U); }

static void mzf_stop_timer_from_foreground(bool force_low)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
        TCCR3A = 0U; TCCR3B = 0U;
        TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
        if (timer3b_owner_get_from_isr() == TIMER3B_OWNER_MZF)
            timer3b_owner_set_from_isr(TIMER3B_OWNER_NONE);
        mzf_pwm_bootstrap_pending = false;
        mzf_pwm_stop_pending = false;
        mzf_pwm_next_valid = false;
    }
    if (force_low) mz_read_set(false);
    monitor_disable();
}

static constexpr uint16_t mzf_pwm_compare_ticks(uint16_t logical_high_ticks,
                                                 uint16_t)
{
    return (logical_high_ticks == 0U) ? 1U : logical_high_ticks;
}

static_assert(mzf_pwm_compare_ticks(10U, 20U) == 10U,
              "logical HIGH must own the first connector LOW duration");

static void mzf_pwm_write_next_from_isr(uint16_t logical_high_ticks,
                                         uint16_t logical_low_ticks)
{
    uint16_t total_ticks = (uint16_t)(logical_high_ticks + logical_low_ticks);
    uint16_t compare_ticks = mzf_pwm_compare_ticks(logical_high_ticks, logical_low_ticks);
    if (total_ticks < 2U) total_ticks = 2U;
    OCR3A = (uint16_t)(total_ticks - 1U);
    OCR3B = compare_ticks;
    mzf_pwm_next_compare_ticks = compare_ticks;
    mzf_pwm_next_valid = true;
}

static void mzf_pwm_start_first_from_isr(uint16_t logical_high_ticks,
                                         uint16_t logical_low_ticks)
{
    uint16_t total_ticks = (uint16_t)(logical_high_ticks + logical_low_ticks);
    if (total_ticks < 2U) total_ticks = 2U;
    mzf_pwm_current_compare_ticks = mzf_pwm_compare_ticks(logical_high_ticks, logical_low_ticks);
    mzf_pwm_next_compare_ticks = 1U;
    mzf_pwm_next_valid = false;
    mzf_pwm_bootstrap_pending = true;
    mzf_pwm_stop_pending = false;
    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;
    TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
    TCCR3A = 0U; TCCR3B = 0U;
    OCR3A = (uint16_t)(total_ticks - 1U);
    OCR3B = mzf_pwm_current_compare_ticks;
    TCNT3 = OCR3A;
    TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
    mz_read_set_fast_from_isr(0U);
    timer3b_owner_set_from_isr(TIMER3B_OWNER_MZF);
    monitor_set_tape_activity_from_isr(true);
    TCCR3A = (uint8_t)(_BV(COM3B1) | _BV(COM3B0) | _BV(WGM31) | _BV(WGM30));
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
    bool hold_ul700_start = (terminal == MZF_PWM_TERMINAL_BOUNDARY) &&
                            (mzf_stage == MZF_STAGE_HEADER) && mzf_loader_is_mz700_ul();
    mzf_pwm_terminal_pending = MZF_PWM_TERMINAL_NONE;
    mzf_stop_timer_from_isr_with_read(hold_ul700_start ? 1U : 0U);
    if (terminal == MZF_PWM_TERMINAL_BOUNDARY) mzf_boundary_waiting = true;
    else if (terminal == MZF_PWM_TERMINAL_FINISHED) mzf_state = MZF_PLAYBACK_FINISHED;
}

static void mzf_pwm_pause_from_foreground(void)
{
    uint16_t counter;
    bool first_phase;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        counter = TCNT3;
        first_phase = counter < mzf_pwm_current_compare_ticks;
        mzf_pwm_paused_resume_level = first_phase ? 0U : 1U;
        mzf_pwm_paused_com_connected = (TCCR3A & _BV(COM3B1)) != 0U;
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
        if (mzf_pwm_paused_com_connected) com_bits = (uint8_t)(_BV(COM3B1) | _BV(COM3B0));
        timer3b_owner_set_from_isr(TIMER3B_OWNER_MZF);
        monitor_set_tape_activity_from_isr(true);
        TIFR3 = (uint8_t)(_BV(OCF3B) | _BV(TOV3));
        TCCR3A = (uint8_t)(com_bits | _BV(WGM31) | _BV(WGM30));
        TIMSK3 &= (uint8_t)~(_BV(OCIE3B) | _BV(TOIE3));
        if (mzf_pwm_paused_com_connected) TIMSK3 |= _BV(OCIE3B);
        if (!mzf_pwm_bootstrap_pending || mzf_pwm_stop_pending) TIMSK3 |= _BV(TOIE3);
        TCCR3B = (uint8_t)(_BV(WGM33) | _BV(WGM32) | _BV(CS30));
        mzf_paused_mid_pulse = false;
    }
}

static uint16_t mzf_fifo_used_snapshot(void)
{
    uint16_t read_sequence, write_sequence;
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

static bool mzf_mzq_directory_set_position(uint8_t index, uint32_t position)
{
    uint16_t offset;
    if ((index >= MZQ_MAX_FILES) || (position > 0xFFFFFFUL)) return false;
    offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES;
    mzf_mzq_directory_bytes[offset] = (uint8_t)position;
    mzf_mzq_directory_bytes[offset + 1U] = (uint8_t)(position >> 8U);
    mzf_mzq_directory_bytes[offset + 2U] = (uint8_t)(position >> 16U);
    return true;
}

static uint32_t mzf_mzq_directory_get_position(uint8_t index)
{
    uint16_t offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES;
    return (uint32_t)mzf_mzq_directory_bytes[offset] |
           ((uint32_t)mzf_mzq_directory_bytes[offset + 1U] << 8U) |
           ((uint32_t)mzf_mzq_directory_bytes[offset + 2U] << 16U);
}

static bool mzf_mzq_directory_set_body_position(uint8_t index,
                                                 uint32_t position)
{
    uint16_t offset;
    if ((index >= MZQ_MAX_FILES) || (position > 0xFFFFFFUL)) return false;
    offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES +
             MZQ_DIRECTORY_POSITION_BYTES;
    mzf_mzq_directory_bytes[offset] = (uint8_t)position;
    mzf_mzq_directory_bytes[offset + 1U] = (uint8_t)(position >> 8U);
    mzf_mzq_directory_bytes[offset + 2U] = (uint8_t)(position >> 16U);
    return true;
}

static uint32_t mzf_mzq_directory_get_body_position(uint8_t index)
{
    uint16_t offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES +
                      MZQ_DIRECTORY_POSITION_BYTES;
    return (uint32_t)mzf_mzq_directory_bytes[offset] |
           ((uint32_t)mzf_mzq_directory_bytes[offset + 1U] << 8U) |
           ((uint32_t)mzf_mzq_directory_bytes[offset + 2U] << 16U);
}

static bool mzf_mzq_directory_set_physical(uint8_t index,
                                            uint32_t header_position,
                                            uint32_t body_position,
                                            uint16_t duration_seconds)
{
    uint16_t offset;
    if ((index >= MZQ_MAX_FILES) ||
        (header_position > MZQ_PHYSICAL_POSITION_MAX) ||
        (body_position > MZQ_PHYSICAL_POSITION_MAX)) return false;
    if (duration_seconds > MZQ_PHYSICAL_DURATION_MAX)
        duration_seconds = MZQ_PHYSICAL_DURATION_MAX;
    offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES;
    mzf_mzq_directory_bytes[offset] = (uint8_t)header_position;
    mzf_mzq_directory_bytes[offset + 1U] = (uint8_t)(header_position >> 8U);
    mzf_mzq_directory_bytes[offset + 2U] =
        (uint8_t)((header_position >> 16U) | (body_position << 3U));
    mzf_mzq_directory_bytes[offset + 3U] = (uint8_t)(body_position >> 5U);
    mzf_mzq_directory_bytes[offset + 4U] =
        (uint8_t)((body_position >> 13U) | (duration_seconds << 6U));
    mzf_mzq_directory_bytes[offset + 5U] = (uint8_t)(duration_seconds >> 2U);
    return true;
}

static uint32_t mzf_mzq_directory_get_physical_header(uint8_t index)
{
    uint16_t offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES;
    return (uint32_t)mzf_mzq_directory_bytes[offset] |
           ((uint32_t)mzf_mzq_directory_bytes[offset + 1U] << 8U) |
           (((uint32_t)mzf_mzq_directory_bytes[offset + 2U] & 0x07UL) << 16U);
}

static uint32_t mzf_mzq_directory_get_physical_body(uint8_t index)
{
    uint16_t offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES;
    return ((uint32_t)mzf_mzq_directory_bytes[offset + 2U] >> 3U) |
           ((uint32_t)mzf_mzq_directory_bytes[offset + 3U] << 5U) |
           (((uint32_t)mzf_mzq_directory_bytes[offset + 4U] & 0x3FUL) << 13U);
}

static uint16_t mzf_mzq_directory_get_physical_duration(uint8_t index)
{
    uint16_t offset = (uint16_t)index * MZQ_DIRECTORY_ENTRY_BYTES;
    return (uint16_t)(mzf_mzq_directory_bytes[offset + 4U] >> 6U) |
           ((uint16_t)mzf_mzq_directory_bytes[offset + 5U] << 2U);
}

static void mzf_qd_analysis_begin(void)
{
    mzf_qd_analysis_active = true;
    mzf_qd_analysis_cancelled = false;
    mzf_qd_last_progress = 0xFFU;
    if (mzf_qd_progress_callback != NULL) mzf_qd_progress_callback(0U);
}

static void mzf_qd_analysis_finish(bool success)
{
    if (success && (mzf_qd_progress_callback != NULL))
        mzf_qd_progress_callback(100U);
    mzf_qd_analysis_active = false;
}

static bool mzf_qd_analysis_poll(uint32_t position, uint32_t total)
{
    uint8_t percent;
    if (!mzf_qd_analysis_active && !mzf_qd_load_active)
        return mzf_qd_analysis_cancelled;
    if ((mzf_qd_cancel_callback != NULL) && mzf_qd_cancel_callback())
    {
        if (mzf_qd_load_active) mzf_qd_load_cancelled = true;
        mzf_qd_analysis_cancelled = true;
        mzf_qd_analysis_active = false;
        mzf_qd_load_active = false;
        if (!mzf_qd_load_cancelled)
            mzf_set_error_P(PSTR("QD CANCEL"), MZF_PLAYBACK_BAD_FILE);
        return true;
    }
    if (mzf_qd_load_active) return false;
    percent = (total == 0UL) ? 0U :
        (uint8_t)(((uint64_t)position * 100ULL) / total);
    if ((mzf_qd_progress_callback != NULL) &&
        ((mzf_qd_last_progress == 0xFFU) ||
         (percent >= (uint8_t)(mzf_qd_last_progress + 5U))))
    {
        mzf_qd_last_progress = percent;
        mzf_qd_progress_callback(percent);
    }
    return false;
}

static void mzf_qd_set_error_unless_cancelled(PGM_P text,
                                               mzf_playback_state_t state)
{
    if (!mzf_qd_analysis_cancelled) mzf_set_error_P(text, state);
}

static bool mzf_fifo_pop_from_isr(uint8_t *value)
{
    uint16_t read_sequence = mzf_fifo_read_sequence;
    if ((value == NULL) || (read_sequence == mzf_fifo_write_sequence)) return false;
    *value = wav_sample_stream_isr_bytes[read_sequence & MZF_FIFO_MASK];
    mzf_fifo_read_sequence = (uint16_t)(read_sequence + 1U);
    return true;
}

static bool mzf_mzq_raw_byte(uint32_t relative_offset, uint8_t *value)
{
    uint32_t remaining;
    uint16_t request;
    int16_t received;
    const bool use_scan_buffer = mzf_qd_analysis_active;
    uint8_t *raw_buffer = use_scan_buffer ?
        const_cast<uint8_t *>(wav_sample_stream_isr_bytes) : mzf_mzq_raw_buffer;
    const uint16_t raw_buffer_bytes = use_scan_buffer ?
        WAV_SAMPLE_STREAM_BUFFER_BYTES : MZQ_RAW_BUFFER_BYTES;
    if ((value == NULL) || (relative_offset >= mzf_mzq_track_length)) return false;
    if ((mzf_mzq_raw_buffer_length == 0U) ||
        (relative_offset < mzf_mzq_raw_buffer_offset) ||
        (relative_offset >= (mzf_mzq_raw_buffer_offset + mzf_mzq_raw_buffer_length)))
    {
        if (mzf_qd_analysis_poll(relative_offset, mzf_mzq_track_length))
            return false;
        mzf_mzq_raw_buffer_offset = relative_offset;
        remaining = mzf_mzq_track_length - relative_offset;
        request = (remaining > raw_buffer_bytes) ?
            raw_buffer_bytes : (uint16_t)remaining;
        if (!sdcard_file_seek(mzf_mzq_track_offset + relative_offset)) return false;
        received = sdcard_file_read(raw_buffer, request);
        if (received != (int16_t)request) return false;
        mzf_mzq_raw_buffer_length = request;
    }
    *value = raw_buffer[relative_offset - mzf_mzq_raw_buffer_offset];
    return true;
}

static bool mzf_mzq_decode_byte(uint32_t logical_index, uint8_t *value)
{
    uint32_t first_bit, raw_offset, raw_bits;
    uint16_t spread_bits;
    uint8_t raw, bit_shift;
    if (value == NULL) return false;
    first_bit = (uint32_t)mzf_mzq_data_phase + logical_index * 16UL;
    if ((first_bit + 14UL) >= (mzf_mzq_track_length * 8UL)) return false;
    raw_offset = first_bit >> 3U;
    bit_shift = (uint8_t)(first_bit & 7UL);
    if (!mzf_mzq_raw_byte(raw_offset, &raw)) return false;
    raw_bits = raw;
    if (!mzf_mzq_raw_byte(raw_offset + 1UL, &raw)) return false;
    raw_bits |= (uint32_t)raw << 8U;
    if (bit_shift >= 2U)
    {
        if (!mzf_mzq_raw_byte(raw_offset + 2UL, &raw)) return false;
        raw_bits |= (uint32_t)raw << 16U;
    }

    /* MFM data bits occupy every other cell. Compact the 16-cell window
       into one byte instead of fetching and testing all eight bits alone. */
    spread_bits = (uint16_t)(raw_bits >> bit_shift) & 0x5555U;
    spread_bits = (uint16_t)((spread_bits | (spread_bits >> 1U)) & 0x3333U);
    spread_bits = (uint16_t)((spread_bits | (spread_bits >> 2U)) & 0x0F0FU);
    spread_bits = (uint16_t)((spread_bits | (spread_bits >> 4U)) & 0x00FFU);
    *value = (uint8_t)spread_bits;
    return true;
}

static bool mzf_record_source_seek(uint32_t position)
{
    if (!mzf_mzq_physical_source)
    {
        if ((mzf_format == FILE_FORMAT_MZQ) &&
            ((position < mzf_original_data_offset) ||
             ((position - mzf_original_data_offset) > mzf_original_data_length)))
            return false;
        return sdcard_file_seek(position);
    }
    if (position > mzf_original_data_length) return false;
    mzf_mzq_source_position = position;
    mzf_mzq_raw_buffer_length = 0U;
    return true;
}

static int16_t mzf_record_source_read(void *buffer, uint16_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    uint16_t count = 0U;
    if (!mzf_mzq_physical_source)
    {
        if (mzf_format == FILE_FORMAT_MZQ)
        {
            const uint32_t position = sdcard_file_position();
            if ((position < mzf_original_data_offset) ||
                ((position - mzf_original_data_offset) > mzf_original_data_length))
                return -1;
            const uint32_t remaining = mzf_original_data_length -
                                       (position - mzf_original_data_offset);
            if (remaining == 0UL) return 0;
            if ((uint32_t)size > remaining) size = (uint16_t)remaining;
        }
        return sdcard_file_read(buffer, size);
    }
    while ((count < size) && (mzf_mzq_source_position < mzf_original_data_length))
    {
        if (!mzf_mzq_decode_byte(mzf_mzq_body_logical_offset +
                                 mzf_mzq_source_position, bytes + count))
            return (count == 0U) ? -1 : (int16_t)count;
        ++count;
        ++mzf_mzq_source_position;
    }
    return (int16_t)count;
}

static bool mzf_refill_data_once(void)
{
    uint16_t used, free_bytes, request, write_sequence;
    int16_t received;
    uint8_t *work;
    if (mzf_record_data_read >= mzf_record_data_length)
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { mzf_fifo_source_finished = 1U; }
        return true;
    }
    used = mzf_fifo_used_snapshot();
    if (used > MZF_FIFO_CAPACITY) { mzf_set_error_P(PSTR("MZF FIFO"), MZF_PLAYBACK_IO_ERROR); return false; }
    free_bytes = (uint16_t)(MZF_FIFO_CAPACITY - used);
    if (free_bytes == 0U) return true;
    request = free_bytes;
    if (request > MZF_REFILL_BLOCK) request = MZF_REFILL_BLOCK;
    if ((uint32_t)request > (mzf_record_data_length - mzf_record_data_read))
        request = (uint16_t)(mzf_record_data_length - mzf_record_data_read);
    work = wav_sample_stream_get_shared_work_buffer();
    received = mzf_record_source_read(work, request);
    if (received < 0) { mzf_set_error_P(PSTR("MZF READ"), MZF_PLAYBACK_IO_ERROR); return false; }
    if (received == 0) { mzf_set_error_P(PSTR("MZF SHORT"), MZF_PLAYBACK_BAD_FILE); return false; }
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
        if (mzf_record_data_read >= mzf_record_data_length) mzf_fifo_source_finished = 1U;
    }
    return true;
}

static bool mzf_prefill_data(void)
{
    while (mzf_record_data_read < mzf_record_data_length)
    {
        uint16_t before = mzf_fifo_used_snapshot();
        if (!mzf_refill_data_once()) return false;
        if (mzf_fifo_used_snapshot() == before) break;
    }
    return (mzf_record_data_length == 0UL) || (mzf_fifo_used_snapshot() != 0U);
}

static bool mzf_prepare_loader_block_data(void)
{
    uint8_t *work = wav_sample_stream_get_shared_work_buffer();
    uint16_t length = mzf_loader_build_loader(work, MZF_REFILL_BLOCK);
    uint16_t write_sequence = 0U;
    if ((length == 0U) || (length > MZF_FIFO_CAPACITY))
    { mzf_set_error_P(PSTR("LDR BLOCK"), MZF_PLAYBACK_BAD_FILE); return false; }
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
    if (!file_format_is_record_container(mzf_format))
    { mzf_mzt_record_title[0] = '\0'; return; }
    if (!mz_title_decode_display(&mzf_header[1U], 17U, mzf_mzt_record_title,
                                 sizeof(mzf_mzt_record_title)))
        flash_text_copy(mzf_mzt_record_title, sizeof(mzf_mzt_record_title), PSTR("RECORD"));
}

static bool mzf_read_header_record(void)
{
    int16_t received;
    uint32_t remaining;
    if ((mzf_file_size - sdcard_file_position()) < MZF_HEADER_BYTES)
    { mzf_set_error_P(PSTR("MZT HEADER"), MZF_PLAYBACK_BAD_FILE); return false; }
    received = sdcard_file_read(mzf_header, MZF_HEADER_BYTES);
    if (received != (int16_t)MZF_HEADER_BYTES)
    {
        mzf_set_error_P((received < 0) ? PSTR("MZT READ") : PSTR("MZT SHORT"),
                        (received < 0) ? MZF_PLAYBACK_IO_ERROR : MZF_PLAYBACK_BAD_FILE);
        return false;
    }
    mzf_record_data_length = mzf_header_data_length();
    remaining = mzf_file_size - sdcard_file_position();
    if (mzf_record_data_length > remaining)
    { mzf_set_error_P(PSTR("MZF LENGTH"), MZF_PLAYBACK_BAD_FILE); return false; }
    mzf_record_data_file_start = sdcard_file_position();
    mzf_record_data_file_end = mzf_record_data_file_start + mzf_record_data_length;
    mzf_header_offset = 0U;
    mzf_record_data_read = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_fifo_reset();
    return true;
}

static uint8_t mzf_popcount8(uint8_t value)
{
    value = (uint8_t)(value - ((value >> 1U) & 0x55U));
    value = (uint8_t)((value & 0x33U) + ((value >> 2U) & 0x33U));
    return (uint8_t)((value + (value >> 4U)) & 0x0FU);
}

static uint8_t mzf_popcount16(uint16_t value)
{
    return (uint8_t)(mzf_popcount8((uint8_t)value) + mzf_popcount8((uint8_t)(value >> 8U)));
}

static bool mzf_add_half_milliseconds(uint32_t *total, uint32_t amount)
{
    if (total == NULL) return false;
    if ((0xFFFFFFFFUL - *total) < amount) { *total = 0xFFFFFFFFUL; return false; }
    *total += amount; return true;
}

static void mzf_set_short_pulse(uint16_t *high_ticks, uint16_t *low_ticks);
static void mzf_set_long_pulse(uint16_t *high_ticks, uint16_t *low_ticks);

static bool mzf_add_stage_duration(bool header_stage,
                                   uint32_t byte_count,
                                   uint32_t one_count,
                                   uint16_t checksum,
                                   uint32_t *half_milliseconds)
{
    uint16_t short_high, short_low, long_high, long_low;
    uint32_t gap_pulses;
    uint8_t copies;
    const uint32_t checksum_ones = mzf_popcount16(checksum);
    uint64_t encoded_short_pulses, encoded_long_pulses;
    uint64_t short_pulses, long_pulses, stage_ticks, stage_half_milliseconds;
    const uint32_t half_millisecond_ticks = F_CPU / 2000UL;

    mzf_set_short_pulse(&short_high, &short_low);
    mzf_set_long_pulse(&long_high, &long_low);
    const uint32_t short_period = (uint32_t)short_high + (uint32_t)short_low;
    gap_pulses = header_stage ? mzf_profile_header_leader : mzf_profile_data_leader;
    copies = (mzf_native_mz700 && (mzf_profile_duplicate_gap != 0U)) ? 2U : 1U;
    encoded_short_pulses = ((uint64_t)byte_count * 8ULL - one_count) +
                           (16ULL - checksum_ones);
    encoded_long_pulses = one_count + byte_count + checksum_ones + 2ULL;
    short_pulses = (uint64_t)gap_pulses +
                   (header_stage ? mzf_profile_header_mark_short :
                                   mzf_profile_data_mark_short) +
                   ((uint64_t)copies * encoded_short_pulses) +
                   ((copies == 2U) ? mzf_profile_duplicate_gap : 0U);
    long_pulses = (header_stage ? mzf_profile_header_mark_long :
                                  mzf_profile_data_mark_long) +
                  mzf_profile_final_mark_long +
                  ((uint64_t)copies *
                   (encoded_long_pulses + MZF_MZ800_TRAILING_LONG_PULSES));
    stage_ticks = short_pulses * short_period +
                  long_pulses * ((uint32_t)long_high + (uint32_t)long_low);
    stage_half_milliseconds =
        (stage_ticks + (half_millisecond_ticks / 2UL)) / half_millisecond_ticks;
    if (stage_half_milliseconds > 0xFFFFFFFFULL)
    { *half_milliseconds = 0xFFFFFFFFUL; return false; }
    if (!mzf_add_half_milliseconds(&mzf_exact_duration_half_ms,
                                   (uint32_t)stage_half_milliseconds))
    { *half_milliseconds = 0xFFFFFFFFUL; return false; }
    return mzf_add_half_milliseconds(half_milliseconds,
                                     (uint32_t)stage_half_milliseconds);
}

static bool mzf_add_profiled_stage_duration(
    uint32_t byte_count, uint32_t one_count, uint16_t checksum,
    uint32_t gap_short_pulses, uint16_t mark_long_pulses,
    uint16_t mark_short_pulses, uint16_t mark_final_long_pulses,
    uint16_t trailing_short_pulses, uint16_t trailing_long_pulses,
    uint32_t short_period_ticks, uint32_t long_period_ticks,
    uint32_t *half_milliseconds)
{
    const uint32_t checksum_ones = mzf_popcount16(checksum);
    const uint32_t half_millisecond_ticks = F_CPU / 2000UL;
    uint64_t short_pulses, long_pulses, stage_ticks, stage_half_milliseconds;
    if ((half_milliseconds == NULL) || (one_count > (byte_count * 8UL))) return false;
    short_pulses = (uint64_t)gap_short_pulses + mark_short_pulses +
                   ((uint64_t)byte_count * 8ULL - one_count) +
                   (16ULL - checksum_ones) + trailing_short_pulses;
    long_pulses = (uint64_t)mark_long_pulses + mark_final_long_pulses +
                  one_count + byte_count + checksum_ones + 2ULL +
                  trailing_long_pulses;
    stage_ticks = short_pulses * short_period_ticks + long_pulses * long_period_ticks;
    stage_half_milliseconds =
        (stage_ticks + (half_millisecond_ticks / 2UL)) / half_millisecond_ticks;
    if (stage_half_milliseconds > 0xFFFFFFFFULL)
    { *half_milliseconds = 0xFFFFFFFFUL; return false; }
    return mzf_add_half_milliseconds(half_milliseconds,
                                     (uint32_t)stage_half_milliseconds);
}

static bool mzf_scan_payload_ones(uint32_t length, uint32_t *one_count,
                                  uint16_t *checksum)
{
    uint8_t *work = wav_sample_stream_get_shared_work_buffer();
    if ((one_count == NULL) || (checksum == NULL)) return false;
    *one_count = 0UL; *checksum = 0U;
    while (length != 0UL)
    {
        uint16_t request = (length > MZF_REFILL_BLOCK) ? MZF_REFILL_BLOCK : (uint16_t)length;
        int16_t received = mzf_record_source_read(work, request);
        if (received != (int16_t)request)
        {
            mzf_set_error_P((received < 0) ? PSTR("MZF READ") : PSTR("MZF SHORT"),
                            (received < 0) ? MZF_PLAYBACK_IO_ERROR : MZF_PLAYBACK_BAD_FILE);
            return false;
        }
        for (uint16_t index = 0U; index < request; ++index)
        {
            uint8_t ones = mzf_popcount8(work[index]);
            if ((0xFFFFFFFFUL - *one_count) < (uint32_t)ones) *one_count = 0xFFFFFFFFUL;
            else *one_count += (uint32_t)ones;
            *checksum = (uint16_t)(*checksum + (uint16_t)ones);
        }
        length -= (uint32_t)request;
    }
    return true;
}

static bool mzf_scan_header_record_duration(uint32_t *half_milliseconds)
{
    uint32_t remaining, header_ones = 0UL, data_ones;
    uint16_t header_checksum = 0U, data_checksum;
    int16_t received;
    if ((mzf_file_size - sdcard_file_position()) < MZF_HEADER_BYTES)
    { mzf_set_error_P(PSTR("MZT HEADER"), MZF_PLAYBACK_BAD_FILE); return false; }
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
    { mzf_set_error_P(PSTR("MZF LENGTH"), MZF_PLAYBACK_BAD_FILE); return false; }
    if (!mzf_add_stage_duration(true, MZF_HEADER_BYTES, header_ones,
                                header_checksum, half_milliseconds) ||
        !mzf_scan_payload_ones(mzf_record_data_length, &data_ones, &data_checksum) ||
        !mzf_add_stage_duration(false, mzf_record_data_length, data_ones,
                                data_checksum, half_milliseconds)) return false;
    return true;
}

static bool mzf_calculate_total_duration(void)
{
    mzf_exact_duration_half_ms = 0UL;
    uint32_t half_milliseconds = 0UL;
    mzf_total_duration_ms = 0UL;
    if (!sdcard_file_seek(0UL))
    { mzf_set_error_P(PSTR("MZF SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    if (mzf_format == FILE_FORMAT_MZT)
    {
        do { if (!mzf_scan_header_record_duration(&half_milliseconds)) return false; }
        while (sdcard_file_position() < mzf_file_size);
    }
    else
    {
        uint32_t trailing_length, trailing_ones;
        uint16_t trailing_checksum;
        if (!mzf_scan_header_record_duration(&half_milliseconds)) return false;
        trailing_length = mzf_file_size - sdcard_file_position();
        if (trailing_length != 0UL)
        {
            if (!mzf_scan_payload_ones(trailing_length, &trailing_ones, &trailing_checksum) ||
                !mzf_add_stage_duration(false, trailing_length, trailing_ones,
                                        trailing_checksum, &half_milliseconds)) return false;
        }
    }
    if (half_milliseconds == 0xFFFFFFFFUL) mzf_total_duration_ms = 0xFFFFFFFFUL;
    else
    {
        const uint8_t units_per_ms = (uint8_t)(2U * mzf_normal_speed_divisor);
        mzf_total_duration_ms = half_milliseconds / units_per_ms;
        if ((half_milliseconds % units_per_ms) != 0UL) mzf_total_duration_ms++;
    }
    if (!sdcard_file_seek(0UL))
    { mzf_set_error_P(PSTR("MZF SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    return true;
}

static void mzf_count_buffer_ones(const uint8_t *bytes, uint16_t length,
                                  uint32_t *one_count, uint16_t *checksum)
{
    *one_count = 0UL; *checksum = 0U;
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
    uint32_t short_period, long_period, gap_pulses;
    uint16_t trailing_short = 0U;
    uint16_t trailing_long = MZF_MZ800_TRAILING_LONG_PULSES;
    switch (mzf_loader_get_variant())
    {
        case MZF_LOADER_VARIANT_IC_1_1:
            short_period = (uint32_t)MZF_IC_1_1_SHORT_HIGH_TICKS + MZF_IC_1_1_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_IC_1_1_LONG_HIGH_TICKS + MZF_IC_1_1_LONG_LOW_TICKS;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_IC_1_2:
            short_period = (uint32_t)MZF_IC_1_2_SHORT_HIGH_TICKS + MZF_IC_1_2_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_IC_1_2_LONG_HIGH_TICKS + MZF_IC_1_2_LONG_LOW_TICKS;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_IC_1_3:
            short_period = (uint32_t)MZF_IC_1_3_SHORT_HIGH_TICKS + MZF_IC_1_3_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_IC_1_3_LONG_HIGH_TICKS + MZF_IC_1_3_LONG_LOW_TICKS;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_IC_1_4:
            short_period = (uint32_t)MZF_IC_1_4_SHORT_HIGH_TICKS + MZF_IC_1_4_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_IC_1_4_LONG_HIGH_TICKS + MZF_IC_1_4_LONG_LOW_TICKS;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
        case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH:
            short_period = (uint32_t)MZF_MZ700_3X_SHORT_TICKS * 2UL;
            long_period = (uint32_t)MZF_MZ700_3X_LONG_TICKS * 2UL;
            gap_pulses = MZF_IC_TURBO_GAP_SHORT_PULSES;
            break;
        case MZF_LOADER_VARIANT_TC_1_1:
            short_period = (uint32_t)MZF_TC_1_1_SHORT_HIGH_TICKS + MZF_TC_1_1_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_TC_1_1_LONG_HIGH_TICKS + MZF_TC_1_1_LONG_LOW_TICKS;
            gap_pulses = MZF_TC_1_1_TURBO_GAP_SHORT_PULSES;
            trailing_short = MZF_TC_LOADER_TRAILING_SHORT_PULSES;
            trailing_long = 0U;
            break;
        case MZF_LOADER_VARIANT_TC_1_2:
            short_period = (uint32_t)MZF_TC_1_2_SHORT_HIGH_TICKS + MZF_TC_1_2_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_TC_1_2_LONG_HIGH_TICKS + MZF_TC_1_2_LONG_LOW_TICKS;
            gap_pulses = MZF_TC_1_2_TURBO_GAP_SHORT_PULSES;
            trailing_short = MZF_TC_LOADER_TRAILING_SHORT_PULSES;
            trailing_long = 0U;
            break;
        case MZF_LOADER_VARIANT_TC_1_3:
            short_period = (uint32_t)MZF_TC_1_3_SHORT_HIGH_TICKS + MZF_TC_1_3_SHORT_LOW_TICKS;
            long_period = (uint32_t)MZF_TC_1_3_LONG_HIGH_TICKS + MZF_TC_1_3_LONG_LOW_TICKS;
            gap_pulses = MZF_TC_1_3_TURBO_GAP_SHORT_PULSES;
            trailing_short = MZF_TC_LOADER_TRAILING_SHORT_PULSES;
            trailing_long = 0U;
            break;
        default: return false;
    }
    return mzf_add_profiled_stage_duration(
        byte_count, one_count, checksum, gap_pulses,
        MZF_IC_TURBO_MARK_LONG_PULSES, MZF_IC_TURBO_MARK_SHORT_PULSES,
        MZF_IC_TURBO_MARK_FINAL_LONG_PULSES, trailing_short, trailing_long,
        short_period, long_period, half_milliseconds);
}

static bool mzf_add_current_tape_turbo_duration(uint32_t *half_milliseconds)
{
    uint32_t one_count;
    uint16_t checksum;
    uint8_t *work;
    uint16_t loader_length;
    bool ok;
    if (half_milliseconds == NULL) return false;
    mzf_count_buffer_ones(mzf_header, MZF_HEADER_BYTES, &one_count, &checksum);
    if (!mzf_add_profiled_stage_duration(
            MZF_HEADER_BYTES, one_count, checksum,
            mzf_profile_header_leader, mzf_profile_header_mark_long,
            mzf_profile_header_mark_short, mzf_profile_final_mark_long,
            0U, MZF_MZ800_TRAILING_LONG_PULSES,
            (uint32_t)mzf_profile_short_high_ticks + mzf_profile_short_low_ticks,
            (uint32_t)mzf_profile_long_high_ticks + mzf_profile_long_low_ticks,
            half_milliseconds)) return false;
    if (mzf_loader_is_tc_turbo())
    {
        work = wav_sample_stream_get_shared_work_buffer();
        loader_length = mzf_loader_build_loader(work, MZF_REFILL_BLOCK);
        if ((loader_length == 0U) || (loader_length != mzf_loader_get_loader_size())) return false;
        mzf_count_buffer_ones(work, loader_length, &one_count, &checksum);
        if (!mzf_add_profiled_stage_duration(
                loader_length, one_count, checksum,
                mzf_profile_data_leader, mzf_profile_data_mark_long,
                mzf_profile_data_mark_short, mzf_profile_final_mark_long,
                MZF_TC_LOADER_TRAILING_SHORT_PULSES, 0U,
                (uint32_t)mzf_profile_short_high_ticks + mzf_profile_short_low_ticks,
                (uint32_t)mzf_profile_long_high_ticks + mzf_profile_long_low_ticks,
                half_milliseconds)) return false;
    }
    if (!mzf_record_source_seek(mzf_original_data_offset)) return false;
    ok = mzf_scan_payload_ones(mzf_original_data_length, &one_count, &checksum) &&
         mzf_add_tape_turbo_payload_duration(mzf_original_data_length,
                                             one_count, checksum,
                                             half_milliseconds);
    if (!mzf_record_source_seek(mzf_original_data_offset)) return false;
    if (!ok) return false;
    if (mzf_loader_is_mz700_fast3() &&
        !mzf_add_half_milliseconds(half_milliseconds,
            (uint32_t)MZF_MZ700_FAST3_START_DELAY_MS * 2UL)) return false;
    return true;
}

static bool mzf_calculate_tape_turbo_duration(void)
{
    uint32_t half_milliseconds = 0UL;
    if (!mzf_add_current_tape_turbo_duration(&half_milliseconds)) return false;
    mzf_total_duration_ms = (half_milliseconds == 0xFFFFFFFFUL) ?
        0xFFFFFFFFUL : (half_milliseconds + 1UL) / 2UL;
    return true;
}

static bool mzf_stage_uses_tape_turbo_timing(void)
{
    return mzf_stage == MZF_STAGE_TAPE_TURBO_DATA;
}

static uint16_t mzf_boundary_auto_continue_ms(void)
{
    if (((mzf_stage == MZF_STAGE_HEADER) &&
         (mzf_loader_is_ic_turbo() || mzf_loader_is_mz700_fast3())) ||
        ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_tape_turbo()))
    {
        if ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_tc_turbo())
            return MZF_TC_TURBO_START_DELAY_MS;
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
            case MZF_LOADER_VARIANT_IC_1_1: return MZF_IC_1_1_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH: return MZF_MZ700_3X_SHORT_TICKS;
            case MZF_LOADER_VARIANT_TC_1_1: return MZF_TC_1_1_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_SHORT_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_SHORT_HIGH_TICKS;
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
            case MZF_LOADER_VARIANT_IC_1_1: return MZF_IC_1_1_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH: return MZF_MZ700_3X_SHORT_TICKS;
            case MZF_LOADER_VARIANT_TC_1_1: return MZF_TC_1_1_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_SHORT_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_SHORT_LOW_TICKS;
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
            case MZF_LOADER_VARIANT_IC_1_1: return MZF_IC_1_1_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH: return MZF_MZ700_3X_LONG_TICKS;
            case MZF_LOADER_VARIANT_TC_1_1: return MZF_TC_1_1_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_LONG_HIGH_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_LONG_HIGH_TICKS;
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
            case MZF_LOADER_VARIANT_IC_1_1: return MZF_IC_1_1_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_IC_1_2: return MZF_IC_1_2_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_IC_1_3: return MZF_IC_1_3_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_MZ700_FAST3_LOW:
            case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH: return MZF_MZ700_3X_LONG_TICKS;
            case MZF_LOADER_VARIANT_TC_1_1: return MZF_TC_1_1_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_LONG_LOW_TICKS;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_LONG_LOW_TICKS;
            default: return MZF_IC_1_4_LONG_LOW_TICKS;
        }
    }
    return mzf_profile_long_low_ticks;
}

static void mzf_configure_normal_speed(loader_mode_t loader_mode)
{
    mz_tape_profile_id_t profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_1X;
    mz_tape_profile_t profile;
    const bool ic_loader = (loader_mode == LOADER_MODE_IC_1_1) ||
                           (loader_mode == LOADER_MODE_IC_1_2) ||
                           (loader_mode == LOADER_MODE_IC_1_3) ||
                           (loader_mode == LOADER_MODE_IC_1_4);
    const bool tc_loader = (loader_mode == LOADER_MODE_TC_1_1) ||
                           (loader_mode == LOADER_MODE_TC_1_2) ||
                           (loader_mode == LOADER_MODE_TC_1_3);
    const bool ul_loader = (loader_mode == LOADER_MODE_UL) ||
                           (loader_mode == LOADER_MODE_UL_MZ800) ||
                           (loader_mode == LOADER_MODE_UL_MZ700);
    if (loader_mode == LOADER_MODE_NORMAL_1_2)
    { mzf_normal_speed_divisor = 2U; profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_2X; }
    else if (loader_mode == LOADER_MODE_NORMAL_1_3)
    { mzf_normal_speed_divisor = 3U; profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_3X; }
    else if (loader_mode == LOADER_MODE_NORMAL_1_4)
    { mzf_normal_speed_divisor = 4U; profile_id = MZ_TAPE_PROFILE_MZ800_NORMAL_4X; }
    else mzf_normal_speed_divisor = 1U;
    mzf_native_mz700 = (loader_mode == LOADER_MODE_MZ700_1X);
    if (mzf_native_mz700 || (loader_mode == LOADER_MODE_MZ700_3X))
        profile_id = MZ_TAPE_PROFILE_MZ700_NORMAL_1X;
    if (!mz_tape_profile_read(profile_id, &profile)) return;
    mzf_profile_short_high_ticks = profile.short_high_ticks;
    mzf_profile_short_low_ticks = profile.short_low_ticks;
    mzf_profile_long_high_ticks = profile.long_high_ticks;
    mzf_profile_long_low_ticks = profile.long_low_ticks;
    mzf_profile_uses_mz800_rom_timing =
        (loader_mode == LOADER_MODE_NORMAL_1_1) || ic_loader || tc_loader || ul_loader;
    mzf_ul_leader_timing = MZF_UL_LEADER_TIMING_NONE;
    mzf_profile_header_leader = (uint16_t)profile.header_leader_short_pulses;
    mzf_profile_data_leader = (uint16_t)profile.data_leader_short_pulses;
    mzf_profile_header_mark_long = profile.header_mark_long_pulses;
    mzf_profile_header_mark_short = profile.header_mark_short_pulses;
    mzf_profile_data_mark_long = profile.data_mark_long_pulses;
    mzf_profile_data_mark_short = profile.data_mark_short_pulses;
    mzf_profile_final_mark_long = profile.final_mark_long_pulses;
    mzf_profile_duplicate_gap = profile.duplicate_gap_short_pulses;
    if (ul_loader)
    {
        mzf_profile_header_leader = MZF_UL_HEADER_LEADER_SHORT_PULSES;
        mzf_profile_data_leader = MZF_UL_DATA_LEADER_SHORT_PULSES;
        mzf_ul_leader_timing = (loader_mode == LOADER_MODE_UL_MZ700) ?
            MZF_UL_LEADER_TIMING_MZ700 : MZF_UL_LEADER_TIMING_MZ800;
    }
}

static void mzf_set_short_pulse(uint16_t *high_ticks, uint16_t *low_ticks)
{ *high_ticks = mzf_short_high_ticks(); *low_ticks = mzf_short_low_ticks(); }
static void mzf_set_long_pulse(uint16_t *high_ticks, uint16_t *low_ticks)
{ *high_ticks = mzf_long_high_ticks(); *low_ticks = mzf_long_low_ticks(); }

static void mzf_set_profiled_pulse(bool is_long, mzf_pulse_region_t region,
                                   bool next_is_long, uint16_t *high_ticks,
                                   uint16_t *low_ticks)
{
    if (is_long) mzf_set_long_pulse(high_ticks, low_ticks);
    else mzf_set_short_pulse(high_ticks, low_ticks);
    if ((region == MZF_PULSE_REGION_LEADER) && !is_long &&
        (mzf_ul_leader_timing != MZF_UL_LEADER_TIMING_NONE))
    {
        if (mzf_ul_leader_timing == MZF_UL_LEADER_TIMING_MZ700)
        { *high_ticks = MZF_MZ700_LEADER_SHORT_HIGH_TICKS; *low_ticks = MZF_MZ700_LEADER_SHORT_LOW_TICKS; }
        else { *high_ticks = MZF_SHORT_HIGH_TICKS; *low_ticks = MZF_MZ800_LEADER_SHORT_LOW_TICKS; }
        return;
    }
    if (!mzf_profile_uses_mz800_rom_timing || mzf_stage_uses_tape_turbo_timing()) return;
    if (region == MZF_PULSE_REGION_LEADER)
    { if (!is_long) *low_ticks = MZF_MZ800_LEADER_SHORT_LOW_TICKS; return; }
    if (region == MZF_PULSE_REGION_TAPE_MARK)
    { *low_ticks = is_long ? MZF_MZ800_MARK_LONG_LOW_TICKS : MZF_MZ800_MARK_SHORT_LOW_TICKS; return; }
    if (is_long) *low_ticks = next_is_long ? MZF_MZ800_DATA_LONG_LONG_LOW_TICKS : MZF_LONG_LOW_TICKS;
    else *low_ticks = next_is_long ? MZF_MZ800_DATA_SHORT_LONG_LOW_TICKS : MZF_SHORT_LOW_TICKS;
}

static uint16_t mzf_gap_short_pulses(void)
{
    if (mzf_stage == MZF_STAGE_HEADER) return mzf_profile_header_leader;
    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_TC_1_1: return MZF_TC_1_1_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_TURBO_GAP_SHORT_PULSES;
            default: return MZF_IC_TURBO_GAP_SHORT_PULSES;
        }
    }
    return mzf_profile_data_leader;
}

static uint16_t mzf_mark_long_pulses(void)
{
    if (mzf_stage == MZF_STAGE_HEADER) return mzf_profile_header_mark_long;
    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA) return MZF_IC_TURBO_MARK_LONG_PULSES;
    return mzf_profile_data_mark_long;
}
static uint16_t mzf_mark_short_pulses(void)
{
    if (mzf_stage == MZF_STAGE_HEADER) return mzf_profile_header_mark_short;
    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA) return MZF_IC_TURBO_MARK_SHORT_PULSES;
    return mzf_profile_data_mark_short;
}
static uint16_t mzf_mark_final_long_pulses(void)
{
    return (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_FINAL_LONG_PULSES : mzf_profile_final_mark_long;
}
static bool mzf_stage_uses_tc_trailing(void)
{
    return mzf_loader_is_tc_turbo() &&
           ((mzf_stage == MZF_STAGE_DATA) || (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA));
}
static uint16_t mzf_trailing_pulses(void)
{
    return mzf_stage_uses_tc_trailing() ? MZF_TC_LOADER_TRAILING_SHORT_PULSES :
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
    if (stage == MZF_STAGE_HEADER) mzf_header_offset = 0U;
}

static uint32_t mzf_stage_byte_count(void)
{
    if (mzf_stage == MZF_STAGE_HEADER) return MZF_HEADER_BYTES;
    if ((mzf_stage == MZF_STAGE_DATA) || (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA))
        return mzf_record_data_length;
    return 0UL;
}

static bool mzf_next_source_byte_from_isr(uint8_t *value)
{
    if (value == NULL) return false;
    if (mzf_stage == MZF_STAGE_HEADER)
    {
        if (mzf_header_offset >= MZF_HEADER_BYTES) return false;
        *value = mzf_header[mzf_header_offset++];
        return true;
    }
    if ((mzf_stage == MZF_STAGE_DATA) || (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA))
        return mzf_fifo_pop_from_isr(value);
    return false;
}

static bool mzf_peek_next_source_msb_from_isr(bool *is_long)
{
    if (is_long == NULL) return false;
    if (mzf_stage == MZF_STAGE_HEADER)
    {
        if (mzf_header_offset >= MZF_HEADER_BYTES) return false;
        *is_long = (mzf_header[mzf_header_offset] & 0x80U) != 0U;
        return true;
    }
    if ((mzf_stage == MZF_STAGE_DATA) || (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA))
    {
        const uint16_t read_sequence = mzf_fifo_read_sequence;
        if (read_sequence == mzf_fifo_write_sequence) return false;
        *is_long = (wav_sample_stream_isr_bytes[read_sequence & MZF_FIFO_MASK] & 0x80U) != 0U;
        return true;
    }
    return false;
}

static bool mzf_next_normal_pulse_from_isr(uint16_t *high_ticks,
                                            uint16_t *low_ticks)
{
    uint32_t byte_count;
    if ((high_ticks == NULL) || (low_ticks == NULL)) return false;
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
                mzf_set_profiled_pulse(false, MZF_PULSE_REGION_LEADER, false,
                                       high_ticks, low_ticks);
                mzf_normal_loop--;
                return true;
            case MZF_STEP_TAPE_MARK_LONG:
                if (mzf_normal_loop == 0U)
                {
                    mzf_normal_loop = mzf_mark_short_pulses();
                    mzf_normal_step = MZF_STEP_TAPE_MARK_SHORT;
                    continue;
                }
                mzf_set_profiled_pulse(true, MZF_PULSE_REGION_TAPE_MARK, false,
                                       high_ticks, low_ticks);
                mzf_normal_loop--;
                return true;
            case MZF_STEP_TAPE_MARK_SHORT:
                if (mzf_normal_loop == 0U)
                {
                    mzf_normal_loop = mzf_mark_final_long_pulses();
                    mzf_normal_step = MZF_STEP_TAPE_MARK_FINAL;
                    continue;
                }
                mzf_set_profiled_pulse(false, MZF_PULSE_REGION_TAPE_MARK, false,
                                       high_ticks, low_ticks);
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
                mzf_set_profiled_pulse(true, MZF_PULSE_REGION_TAPE_MARK, false,
                                       high_ticks, low_ticks);
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
                {
                    const bool is_long = (mzf_normal_data & 0x80U) != 0U;
                    const bool next_is_long = (mzf_normal_bits_remaining > 1U) ?
                        ((mzf_normal_data & 0x40U) != 0U) : true;
                    mzf_set_profiled_pulse(is_long, MZF_PULSE_REGION_DATA,
                                           next_is_long, high_ticks, low_ticks);
                    if (is_long) mzf_normal_checksum++;
                }
                mzf_normal_data <<= 1U;
                mzf_normal_bits_remaining--;
                return true;
            case MZF_STEP_BYTE_STOP:
                {
                    bool next_is_long;
                    if (mzf_normal_bytes_remaining == 0UL)
                        next_is_long = (mzf_normal_checksum & 0x8000U) != 0U;
                    else if (!mzf_peek_next_source_msb_from_isr(&next_is_long))
                        next_is_long = false;
                    mzf_set_profiled_pulse(true, MZF_PULSE_REGION_DATA,
                                           next_is_long, high_ticks, low_ticks);
                }
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
                {
                    const bool is_long = (mzf_normal_data & 0x80U) != 0U;
                    const bool next_is_long = (mzf_normal_bits_remaining > 1U) ?
                        ((mzf_normal_data & 0x40U) != 0U) : true;
                    mzf_set_profiled_pulse(is_long, MZF_PULSE_REGION_DATA,
                                           next_is_long, high_ticks, low_ticks);
                }
                mzf_normal_data <<= 1U;
                mzf_normal_bits_remaining--;
                return true;
            case MZF_STEP_CHECKSUM_STOP:
                mzf_set_profiled_pulse(
                    true, MZF_PULSE_REGION_DATA,
                    (mzf_normal_checksum_byte_index < 2U) ?
                        ((mzf_normal_checksum & 0x0080U) != 0U) :
                        !mzf_stage_uses_tc_trailing(),
                    high_ticks, low_ticks);
                mzf_normal_step = MZF_STEP_CHECKSUM_LOAD;
                return true;
            case MZF_STEP_TRAILING_LONGS:
                if (mzf_normal_loop == 0U)
                {
                    if (mzf_native_mz700 && (mzf_profile_duplicate_gap != 0U) &&
                        (mzf_native_copy_index == 0U) &&
                        ((mzf_stage == MZF_STAGE_HEADER) || (mzf_stage == MZF_STAGE_DATA)))
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
                    mzf_set_profiled_pulse(false, MZF_PULSE_REGION_DATA, false,
                                           high_ticks, low_ticks);
                else
                    mzf_set_profiled_pulse(true, MZF_PULSE_REGION_DATA,
                                           mzf_normal_loop > 1U, high_ticks, low_ticks);
                mzf_normal_loop--;
                return true;
            case MZF_STEP_DUPLICATE_GAP:
                if (mzf_normal_loop != 0U)
                {
                    mzf_set_profiled_pulse(false, MZF_PULSE_REGION_LEADER, false,
                                           high_ticks, low_ticks);
                    mzf_normal_loop--;
                    return true;
                }
                if ((mzf_stage == MZF_STAGE_DATA) && !mzf_native_repeat_refill_ready)
                {
                    mzf_set_error_from_isr_P(PSTR("MZ7 REFILL"), MZF_PLAYBACK_UNDERRUN);
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
                            MZF_PWM_TERMINAL_BOUNDARY : MZF_PWM_TERMINAL_FINISHED;
                    return false;
                }
                if ((mzf_stage == MZF_STAGE_DATA) &&
                    ((mzf_format == FILE_FORMAT_MZQ) ||
                     (mzf_record_data_file_end >= mzf_file_size)))
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
    uint16_t high_ticks, low_ticks;
    if (!mzf_next_normal_pulse_from_isr(&high_ticks, &low_ticks)) return false;
    mzf_pwm_write_next_from_isr(high_ticks, low_ticks);
    return true;
}

static bool mzf_start_normal_output_immediate(void)
{
    uint16_t high_ticks, low_ticks;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (mzf_next_normal_pulse_from_isr(&high_ticks, &low_ticks))
            mzf_pwm_start_first_from_isr(high_ticks, low_ticks);
    }
    return (mzf_state == MZF_PLAYBACK_RUNNING) && !mzf_boundary_waiting;
}

static bool mzf_start_normal_output(void)
{
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
    if (!mzf_record_source_seek(mzf_original_data_offset))
    { mzf_set_error_P(PSTR("TURB SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    mzf_record_data_length = mzf_original_data_length;
    mzf_record_data_file_end = mzf_original_data_offset + mzf_original_data_length;
    mzf_record_data_read = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_fifo_reset();
    if (!mzf_prefill_data()) return false;
    mzf_tape_turbo_payload_prepared = true;
    return true;
}

static bool mzf_prepare_native_mz700_repeat(void)
{
    bool requested;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        requested = mzf_native_repeat_refill_requested;
        if (requested) mzf_native_repeat_refill_requested = false;
    }
    if (!requested) return true;
    if (!mzf_record_source_seek(mzf_record_data_file_start))
    { mzf_set_error_P(PSTR("MZ7 SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    mzf_record_data_read = 0UL;
    mzf_fifo_reset();
    if ((mzf_record_data_length != 0UL) && !mzf_refill_data_once()) return false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { mzf_native_repeat_refill_ready = true; }
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
    if ((loader_mode == NULL) || (mzf_source_path == NULL) || (record_index == 0U))
        return false;
    if (mzf_requested_loader_mode != LOADER_MODE_AUTO)
    {
        mzf_mzt_record_loader_from_sidecar = false;
        *loader_mode = mzf_requested_loader_mode;
        return true;
    }
    resume_position = sdcard_file_position();
    sdcard_file_close();
    *loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_mzt_record_loader_from_sidecar =
        mzi_sidecar_read_loader_for_mzt_record(mzf_source_path, record_index, loader_mode);
    if (!sdcard_file_open_read(mzf_source_path))
    { mzf_set_error_P(PSTR("MZT REOPEN"), MZF_PLAYBACK_IO_ERROR); return false; }
    if (!sdcard_file_seek(resume_position))
    { mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    return true;
}

static bool mzf_add_current_normal_record_duration(uint32_t *half_milliseconds)
{
    uint32_t header_ones = 0UL, data_ones;
    uint16_t header_checksum = 0U, data_checksum;
    if (half_milliseconds == NULL) return false;
    for (uint8_t index = 0U; index < MZF_HEADER_BYTES; ++index)
    {
        const uint8_t ones = mzf_popcount8(mzf_header[index]);
        header_ones += (uint32_t)ones;
        header_checksum = (uint16_t)(header_checksum + (uint16_t)ones);
    }
    if (!mzf_add_stage_duration(true, MZF_HEADER_BYTES, header_ones,
                                header_checksum, half_milliseconds)) return false;
    if (!mzf_record_source_seek(mzf_original_data_offset)) return false;
    if (!mzf_scan_payload_ones(mzf_original_data_length, &data_ones, &data_checksum) ||
        !mzf_add_stage_duration(false, mzf_original_data_length, data_ones,
                                data_checksum, half_milliseconds)) return false;
    return true;
}

static bool mzf_locate_mzt_record(uint16_t wanted_record)
{
    uint16_t record_index = 0U;
    uint32_t wanted_offset = 0UL;
    bool found = false;
    if (wanted_record == 0U) wanted_record = 1U;
    mzf_mzt_record_count = 0U;
    if (!sdcard_file_seek(0UL))
    { mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    while (sdcard_file_position() < mzf_file_size)
    {
        uint32_t header_offset = sdcard_file_position();
        if ((mzf_file_size - header_offset) < MZF_HEADER_BYTES)
        { mzf_set_error_P(PSTR("MZT HEADER"), MZF_PLAYBACK_BAD_FILE); return false; }
        if (!mzf_read_header_record()) return false;
        if (record_index == 0xFFFFU)
        { mzf_set_error_P(PSTR("MZT COUNT"), MZF_PLAYBACK_BAD_FILE); return false; }
        record_index++;
        if (record_index == wanted_record) { wanted_offset = header_offset; found = true; }
        if (!sdcard_file_seek(mzf_record_data_file_end))
        { mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    }
    if ((sdcard_file_position() != mzf_file_size) || (record_index == 0U))
    { mzf_set_error_P(PSTR("MZT FORMAT"), MZF_PLAYBACK_BAD_FILE); return false; }
    mzf_mzt_record_count = record_index;
    if (!found) { mzf_set_error_P(PSTR("MZT RECORD"), MZF_PLAYBACK_BAD_FILE); return false; }
    if (!sdcard_file_seek(wanted_offset) || !mzf_read_header_record())
    {
        if (mzf_state != MZF_PLAYBACK_BAD_FILE)
            mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR);
        return false;
    }
    mzf_mzt_record_index = wanted_record;
    return true;
}

/* Locate the next QuickDisk frame marker. MZQ stores every block as
   00 16 16 A5 followed by its block payload and a three-byte CRC trailer.
   Searching instead of assuming one fixed gap byte also accepts images made
   from real media with longer inter-block gaps. */
static bool mzf_mzq_read_logical(void *buffer, uint16_t size, PGM_P short_error)
{
    int16_t received = sdcard_file_read(buffer, size);
    if (received == (int16_t)size) return true;
    mzf_set_error_P((received < 0) ? PSTR("QD READ") : short_error,
                    (received < 0) ? MZF_PLAYBACK_IO_ERROR : MZF_PLAYBACK_BAD_FILE);
    return false;
}

static bool mzf_mzq_find_frame(void)
{
    uint8_t matched = 0U;
    uint8_t value;
    while (sdcard_file_position() < mzf_file_size)
    {
        /* A damaged declared record may make the gap search reach the tail.
           Poll STOP without adding work to the selected payload data path. */
        if (((sdcard_file_position() & 0x1FFUL) == 0UL) &&
            (mzf_qd_cancel_callback != NULL) && mzf_qd_cancel_callback())
        {
            mzf_qd_analysis_cancelled = true;
            mzf_set_error_P(PSTR("QD CANCEL"), MZF_PLAYBACK_BAD_FILE);
            return false;
        }
        if (!mzf_mzq_read_logical(&value, 1U, PSTR("QD SHORT HDR"))) return false;
        switch (matched)
        {
            case 0U: matched = (value == 0x00U) ? 1U : 0U; break;
            case 1U:
                if (value == 0x16U) matched = 2U;
                else matched = (value == 0x00U) ? 1U : 0U;
                break;
            case 2U:
                if (value == 0x16U) matched = 3U;
                else matched = (value == 0x00U) ? 1U : 0U;
                break;
            default:
                if (value == 0xA5U) return true;
                matched = (value == 0x00U) ? 1U : 0U;
                break;
        }
    }
    mzf_set_error_P(PSTR("QD SHORT HDR"), MZF_PLAYBACK_BAD_FILE);
    return false;
}

static uint32_t mzf_read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint16_t mzf_mzq_crc_byte(uint16_t crc, uint8_t data)
{
    for (uint8_t bit = 0U; bit < 8U; ++bit)
    {
        uint8_t mix = (uint8_t)(data & 1U);
        data >>= 1U;
        if ((crc & 0x8000U) != 0U) mix ^= 1U;
        crc <<= 1U;
        if (mix != 0U) crc ^= 0x8005U;
    }
    return crc;
}

static void mzf_mzq_build_tape_header(const uint8_t *qd_header,
                                      uint16_t body_length)
{
    memset(mzf_header, 0, MZF_HEADER_BYTES);
    memcpy(mzf_header, qd_header, 18U);
    mzf_header[MZF_HEADER_DATA_LENGTH_OFFSET] = (uint8_t)(body_length & 0xFFU);
    mzf_header[MZF_HEADER_DATA_LENGTH_OFFSET + 1U] = (uint8_t)(body_length >> 8U);
    memcpy(mzf_header + 20U, qd_header + MZQ_HEADER_SHIFT_OFFSET,
           MZQ_HEADER_BYTES - MZQ_HEADER_SHIFT_OFFSET);
}

static bool mzf_mzq_configure_physical(const uint8_t *prefix, bool hxc)
{
    uint8_t descriptor[16];
    uint32_t descriptor_offset;
    uint32_t track_offset, track_length, window_start, window_end;
    if (hxc)
    {
        if ((mzf_file_size < 40UL) || (mzf_read_le32(prefix + 12U) != 1UL) ||
            (mzf_read_le32(prefix + 16U) != 1UL) ||
            (mzf_read_le32(prefix + 20U) != 0UL)) return false;
        descriptor_offset = mzf_read_le32(prefix + 36U);
    }
    else descriptor_offset = 0x200UL;
    if ((descriptor_offset > mzf_file_size) ||
        ((mzf_file_size - descriptor_offset) < sizeof(descriptor)) ||
        !sdcard_file_seek(descriptor_offset) ||
        (sdcard_file_read(descriptor, sizeof(descriptor)) != (int16_t)sizeof(descriptor)))
        return false;
    track_offset = mzf_read_le32(descriptor);
    track_length = mzf_read_le32(descriptor + 4U);
    window_start = mzf_read_le32(descriptor + 8U);
    window_end = mzf_read_le32(descriptor + 12U);
    if ((track_offset < 0x400UL) || (track_length == 0UL) ||
        (track_offset > mzf_file_size) ||
        (track_length > (mzf_file_size - track_offset)) ||
        (window_start > window_end) || (window_end > track_length)) return false;
    mzf_mzq_track_offset = track_offset;
    mzf_mzq_track_length = track_length;
    /* MZTools canonical writers place the first data cell at phase 6 for
       HxC and phase 9 for FlashFloppy. Try that first, then retain the full
       16-phase analysis for imported/captured images. */
    mzf_mzq_preferred_phase = hxc ? 6U : 9U;
    mzf_mzq_raw_buffer_length = 0U;
    return true;
}

static uint32_t mzf_mzq_scan_position = 0UL;

static bool mzf_mzq_scan_read(uint8_t *value)
{
    if (!mzf_mzq_decode_byte(mzf_mzq_scan_position, value)) return false;
    ++mzf_mzq_scan_position;
    return true;
}

static bool mzf_mzq_scan_find_sync(void)
{
    uint8_t sync_count = 0U;
    uint8_t bytes_since_break = 0xFFU;
    uint8_t value;
    while (mzf_mzq_scan_read(&value))
    {
        if (value == 0x00U)
        {
            bytes_since_break = 0U;
            sync_count = 0U;
        }
        else
        {
            if (bytes_since_break != 0xFFU) ++bytes_since_break;
            if (value == 0x16U)
            {
                if (sync_count != 0xFFU) ++sync_count;
            }
            else
            {
                if ((value == 0xA5U) && (sync_count >= 2U) &&
                    ((uint16_t)bytes_since_break <=
                     ((uint16_t)sync_count + 17U)))
                    return true;
                sync_count = 0U;
            }
        }
    }
    return false;
}

static bool mzf_mzq_find_physical_count(uint8_t *block_count)
{
    uint8_t value, crc_lo, crc_hi;
    for (uint8_t attempt = 0U; attempt < 16U; ++attempt)
    {
        uint8_t phase = (attempt == 0U) ? mzf_mzq_preferred_phase :
            (uint8_t)(attempt - (attempt <= mzf_mzq_preferred_phase ? 1U : 0U));
        mzf_mzq_data_phase = phase;
        /* Start at bit zero exactly as the proven decoder did. Starting at a
           descriptor window can miss the break/sync context and then spend a
           very long time exhausting all remaining wrong phases. */
        mzf_mzq_scan_position = 0UL;
        mzf_qd_last_progress = 0xFFU;
        mzf_mzq_raw_buffer_length = 0U;
        while (mzf_mzq_scan_find_sync())
        {
            uint16_t crc = mzf_mzq_crc_byte(0U, 0xA5U);
            if (!mzf_mzq_scan_read(&value) || !mzf_mzq_scan_read(&crc_lo) ||
                !mzf_mzq_scan_read(&crc_hi)) break;
            crc = mzf_mzq_crc_byte(crc, value);
            crc = mzf_mzq_crc_byte(crc, crc_lo);
            crc = mzf_mzq_crc_byte(crc, crc_hi);
            if ((crc == 0U) && ((value & 1U) == 0U) &&
                (value <= (MZQ_MAX_FILES * 2U)))
            {
                *block_count = value;
                return true;
            }
        }
    }
    return false;
}

static bool mzf_mzq_read_physical_frame(uint8_t expected_type,
                                        uint8_t *payload, uint16_t *length,
                                        uint32_t *payload_position,
                                        uint32_t *one_count,
                                        uint16_t *checksum)
{
    uint8_t type, lo, hi, value;
    uint16_t crc = mzf_mzq_crc_byte(0U, 0xA5U);
    if (!mzf_mzq_scan_find_sync() || !mzf_mzq_scan_read(&type) ||
        !mzf_mzq_scan_read(&lo) || !mzf_mzq_scan_read(&hi)) return false;
    if ((expected_type == 0x00U) ? (type != 0x00U) :
        ((type != 0x01U) && (type != 0x05U))) return false;
    *length = (uint16_t)lo | ((uint16_t)hi << 8U);
    if ((expected_type == 0x00U) && (*length != MZQ_HEADER_BYTES)) return false;
    crc = mzf_mzq_crc_byte(crc, type);
    crc = mzf_mzq_crc_byte(crc, lo);
    crc = mzf_mzq_crc_byte(crc, hi);
    if (payload_position != NULL) *payload_position = mzf_mzq_scan_position;
    if (one_count != NULL) *one_count = 0UL;
    if (checksum != NULL) *checksum = 0U;
    for (uint16_t index = 0U; index < *length; ++index)
    {
        uint8_t ones;
        if (!mzf_mzq_scan_read(&value)) return false;
        if ((payload != NULL) && (index < MZQ_HEADER_BYTES)) payload[index] = value;
        if ((one_count != NULL) || (checksum != NULL))
        {
            ones = mzf_popcount8(value);
            if (one_count != NULL) *one_count += (uint32_t)ones;
            if (checksum != NULL)
                *checksum = (uint16_t)(*checksum + (uint16_t)ones);
        }
        crc = mzf_mzq_crc_byte(crc, value);
    }
    for (uint8_t index = 0U; index < 2U; ++index)
    {
        if (!mzf_mzq_scan_read(&value)) return false;
        crc = mzf_mzq_crc_byte(crc, value);
    }
    return crc == 0U;
}

static uint32_t mzf_qdf_scan_position = 0UL;

static bool mzf_qdf_scan_read(uint8_t *value)
{
    uint32_t remaining;
    uint16_t request;
    int16_t received;
    if ((value == NULL) || (mzf_qdf_scan_position >= mzf_file_size)) return false;
    if ((mzf_mzq_raw_buffer_length == 0U) ||
        (mzf_qdf_scan_position < mzf_mzq_raw_buffer_offset) ||
        (mzf_qdf_scan_position >=
         (mzf_mzq_raw_buffer_offset + mzf_mzq_raw_buffer_length)))
    {
        if (mzf_qd_analysis_poll(mzf_qdf_scan_position, mzf_file_size))
            return false;
        mzf_mzq_raw_buffer_offset = mzf_qdf_scan_position;
        remaining = mzf_file_size - mzf_qdf_scan_position;
        request = (remaining > MZQ_RAW_BUFFER_BYTES) ?
            MZQ_RAW_BUFFER_BYTES : (uint16_t)remaining;
        if (!sdcard_file_seek(mzf_qdf_scan_position)) return false;
        received = sdcard_file_read(mzf_mzq_raw_buffer, request);
        if (received != (int16_t)request) return false;
        mzf_mzq_raw_buffer_length = request;
    }
    *value = mzf_mzq_raw_buffer[mzf_qdf_scan_position -
                                mzf_mzq_raw_buffer_offset];
    ++mzf_qdf_scan_position;
    return true;
}

static bool mzf_qdf_find_sync(void)
{
    uint8_t sync_count = 0U;
    uint8_t value;
    bool break_seen = false;
    while (mzf_qdf_scan_position < mzf_file_size)
    {
        if (!mzf_qdf_scan_read(&value)) return false;
        if (value == 0x00U)
        {
            break_seen = true;
            sync_count = 0U;
        }
        else if (break_seen && (value == 0x16U))
        {
            if (sync_count != 0xFFU) ++sync_count;
        }
        else if (break_seen && (value == 0xA5U) && (sync_count >= 2U))
        {
            return true;
        }
        else
        {
            break_seen = false;
            sync_count = 0U;
        }
    }
    return false;
}

static bool mzf_qdf_read_frame(uint8_t expected_type, uint8_t *payload,
                               uint16_t *length, uint32_t *payload_position)
{
    uint8_t type, lo, hi, value;
    uint16_t crc = mzf_mzq_crc_byte(0U, 0xA5U);
    if (!mzf_qdf_find_sync() ||
        !mzf_qdf_scan_read(&type) || !mzf_qdf_scan_read(&lo) ||
        !mzf_qdf_scan_read(&hi)) return false;
    if ((expected_type == 0x00U) ? (type != 0x00U) :
        ((type != 0x01U) && (type != 0x05U))) return false;
    *length = (uint16_t)lo | ((uint16_t)hi << 8U);
    if ((expected_type == 0x00U) && (*length != MZQ_HEADER_BYTES)) return false;
    crc = mzf_mzq_crc_byte(crc, type);
    crc = mzf_mzq_crc_byte(crc, lo);
    crc = mzf_mzq_crc_byte(crc, hi);
    if (payload_position != NULL) *payload_position = mzf_qdf_scan_position;
    for (uint16_t index = 0U; index < *length; ++index)
    {
        if (!mzf_qdf_scan_read(&value)) return false;
        if ((payload != NULL) && (index < MZQ_HEADER_BYTES)) payload[index] = value;
        crc = mzf_mzq_crc_byte(crc, value);
    }
    for (uint8_t index = 0U; index < 2U; ++index)
    {
        if (!mzf_qdf_scan_read(&value)) return false;
        crc = mzf_mzq_crc_byte(crc, value);
    }
    return crc == 0U;
}

static bool mzf_mzq_directory_matches(uint8_t kind)
{
    return mzf_mzq_directory_valid &&
           (mzf_mzq_directory_kind == kind) &&
           (mzf_mzq_directory_file_size == mzf_file_size) &&
           ((kind != MZQ_CACHE_PHYSICAL) ||
            ((mzf_mzq_directory_track_offset == mzf_mzq_track_offset) &&
             (mzf_mzq_directory_track_length == mzf_mzq_track_length))) &&
           (mzf_mzq_directory_count != 0U);
}

static bool mzf_mzq_read_cached_header(uint8_t index, uint8_t *header)
{
    uint8_t value;
    uint16_t crc;
    if ((header == NULL) || (index >= mzf_mzq_directory_count)) return false;
    mzf_mzq_data_phase = mzf_mzq_directory_phase;
    mzf_mzq_scan_position = mzf_mzq_directory_get_physical_header(index);
    mzf_mzq_raw_buffer_length = 0U;
    crc = mzf_mzq_crc_byte(0U, 0xA5U);
    crc = mzf_mzq_crc_byte(crc, 0x00U);
    crc = mzf_mzq_crc_byte(crc, MZQ_HEADER_BYTES);
    crc = mzf_mzq_crc_byte(crc, 0x00U);
    for (uint8_t offset = 0U; offset < MZQ_HEADER_BYTES; ++offset)
    {
        if (!mzf_mzq_scan_read(&header[offset])) return false;
        crc = mzf_mzq_crc_byte(crc, header[offset]);
    }
    for (uint8_t offset = 0U; offset < 2U; ++offset)
    {
        if (!mzf_mzq_scan_read(&value)) return false;
        crc = mzf_mzq_crc_byte(crc, value);
    }
    return crc == 0U;
}

static bool mzf_mzq_physical_duration_seconds(const uint8_t *header,
                                               uint16_t body_length,
                                               uint32_t body_ones,
                                               uint16_t body_checksum,
                                               uint16_t *duration_seconds)
{
    uint32_t header_ones = 0UL, half_milliseconds = 0UL;
    uint32_t saved_exact_duration = mzf_exact_duration_half_ms;
    uint16_t header_checksum = 0U;
    bool success;
    if ((header == NULL) || (duration_seconds == NULL)) return false;
    mzf_mzq_build_tape_header(header, body_length);
    for (uint8_t index = 0U; index < MZF_HEADER_BYTES; ++index)
    {
        uint8_t ones = mzf_popcount8(mzf_header[index]);
        header_ones += (uint32_t)ones;
        header_checksum = (uint16_t)(header_checksum + (uint16_t)ones);
    }
    success = mzf_add_stage_duration(true, MZF_HEADER_BYTES, header_ones,
                                     header_checksum, &half_milliseconds) &&
              mzf_add_stage_duration(false, body_length, body_ones,
                                     body_checksum, &half_milliseconds);
    mzf_exact_duration_half_ms = saved_exact_duration;
    if (!success) return false;
    half_milliseconds = (half_milliseconds + 1000UL) / 2000UL;
    if (half_milliseconds > MZQ_PHYSICAL_DURATION_MAX)
        half_milliseconds = MZQ_PHYSICAL_DURATION_MAX;
    *duration_seconds = (uint16_t)half_milliseconds;
    return true;
}

static bool mzf_mzq_select_physical_record(uint16_t wanted_record,
                                            uint16_t record_count,
                                            const uint8_t *header,
                                            uint16_t body_length,
                                            uint32_t body_position)
{
    if ((header == NULL) || (wanted_record == 0U) ||
        (wanted_record > record_count) ||
        (((uint16_t)header[20] | ((uint16_t)header[21] << 8U)) != body_length))
        return false;
    mzf_mzq_build_tape_header(header, body_length);
    mzf_mzt_record_count = record_count;
    mzf_mzt_record_index = wanted_record;
    mzf_record_data_length = body_length;
    mzf_record_data_file_start = 0UL;
    mzf_record_data_file_end = body_length;
    mzf_mzq_body_logical_offset = body_position;
    mzf_mzq_source_position = 0UL;
    mzf_mzq_physical_source = true;
    mzf_header_offset = 0U;
    mzf_record_data_read = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_fifo_reset();
    return true;
}

static bool mzf_locate_physical_mzq_record(uint16_t wanted_record)
{
    uint8_t block_count;
    uint8_t qd_header[MZQ_HEADER_BYTES];
    uint8_t selected_header[MZQ_HEADER_BYTES];
    uint16_t header_length, body_length, selected_length = 0U;
    uint32_t header_position, body_position, selected_position = 0UL;
    bool found = false;

    if (mzf_mzq_directory_matches(MZQ_CACHE_PHYSICAL))
    {
        uint8_t index;
        mzf_mzt_record_count = mzf_mzq_directory_count;
        if ((wanted_record == 0U) || (wanted_record > mzf_mzt_record_count))
            return false;
        index = (uint8_t)(wanted_record - 1U);
        if (!mzf_mzq_read_cached_header(index, selected_header)) return false;
        body_length = (uint16_t)selected_header[20] |
                      ((uint16_t)selected_header[21] << 8U);
        body_position = mzf_mzq_directory_get_physical_body(index);
        mzf_total_duration_ms =
            (uint32_t)mzf_mzq_directory_get_physical_duration(index) * 1000UL;
        return mzf_mzq_select_physical_record(
            wanted_record, mzf_mzt_record_count, selected_header,
            body_length, body_position);
    }

    mzf_mzq_directory_valid = false;
    mzf_qd_analysis_begin();
    if (!mzf_mzq_find_physical_count(&block_count) ||
        (block_count == 0U) || ((block_count & 1U) != 0U))
    { mzf_qd_analysis_finish(false); return false; }
    mzf_mzt_record_count = (uint16_t)(block_count / 2U);
    if ((wanted_record == 0U) || (wanted_record > mzf_mzt_record_count))
    { mzf_qd_analysis_finish(false); return false; }
    for (uint16_t record = 1U; record <= mzf_mzt_record_count; ++record)
    {
        uint32_t body_ones;
        uint16_t body_checksum, duration_seconds;
        if (!mzf_mzq_read_physical_frame(0x00U, qd_header, &header_length,
                                         &header_position, NULL, NULL) ||
            !mzf_mzq_read_physical_frame(0x05U, NULL, &body_length, &body_position,
                                         &body_ones, &body_checksum) ||
            (((uint16_t)qd_header[20] | ((uint16_t)qd_header[21] << 8U)) != body_length))
        { mzf_qd_analysis_finish(false); return false; }
        if (!mzf_mzq_physical_duration_seconds(qd_header, body_length,
                                                body_ones, body_checksum,
                                                &duration_seconds) ||
            !mzf_mzq_directory_set_physical((uint8_t)(record - 1U),
                                             header_position, body_position,
                                             duration_seconds))
        { mzf_qd_analysis_finish(false); return false; }
        if (record == wanted_record)
        {
            memcpy(selected_header, qd_header, sizeof(selected_header));
            selected_length = body_length;
            selected_position = body_position;
            mzf_total_duration_ms = (uint32_t)duration_seconds * 1000UL;
            found = true;
        }
    }
    if (!found) { mzf_qd_analysis_finish(false); return false; }
    mzf_mzq_directory_file_size = mzf_file_size;
    mzf_mzq_directory_track_offset = mzf_mzq_track_offset;
    mzf_mzq_directory_track_length = mzf_mzq_track_length;
    mzf_mzq_directory_phase = mzf_mzq_data_phase;
    mzf_mzq_directory_count = (uint8_t)mzf_mzt_record_count;
    mzf_mzq_directory_kind = MZQ_CACHE_PHYSICAL;
    mzf_mzq_directory_valid = true;
    mzf_qd_analysis_finish(true);
    return mzf_mzq_select_physical_record(wanted_record, mzf_mzt_record_count,
                                          selected_header, selected_length,
                                          selected_position);
}

static bool mzf_qdf_read_cached_header(uint8_t index, uint8_t *header)
{
    uint8_t value;
    uint16_t crc;
    if ((header == NULL) || (index >= mzf_mzq_directory_count)) return false;
    mzf_qdf_scan_position = mzf_mzq_directory_get_position(index);
    mzf_mzq_raw_buffer_length = 0U;
    crc = mzf_mzq_crc_byte(0U, 0xA5U);
    crc = mzf_mzq_crc_byte(crc, 0x00U);
    crc = mzf_mzq_crc_byte(crc, MZQ_HEADER_BYTES);
    crc = mzf_mzq_crc_byte(crc, 0x00U);
    for (uint8_t offset = 0U; offset < MZQ_HEADER_BYTES; ++offset)
    {
        if (!mzf_qdf_scan_read(&header[offset])) return false;
        crc = mzf_mzq_crc_byte(crc, header[offset]);
    }
    for (uint8_t offset = 0U; offset < 2U; ++offset)
    {
        if (!mzf_qdf_scan_read(&value)) return false;
        crc = mzf_mzq_crc_byte(crc, value);
    }
    return crc == 0U;
}

static bool mzf_qdf_select_record(uint16_t wanted_record,
                                  uint16_t record_count,
                                  const uint8_t *header,
                                  uint16_t body_length,
                                  uint32_t body_position)
{
    if ((header == NULL) ||
        (((uint16_t)header[20] | ((uint16_t)header[21] << 8U)) != body_length) ||
        !sdcard_file_seek(body_position)) return false;
    mzf_mzq_build_tape_header(header, body_length);
    mzf_mzt_record_count = record_count;
    mzf_mzt_record_index = wanted_record;
    mzf_mzq_physical_source = false;
    mzf_record_data_length = body_length;
    mzf_record_data_file_start = body_position;
    mzf_record_data_file_end = body_position + body_length;
    mzf_header_offset = 0U;
    mzf_record_data_read = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_fifo_reset();
    return true;
}

static bool mzf_locate_qdf_record(uint16_t wanted_record)
{
    uint8_t block_count, crc_lo, crc_hi;
    uint8_t qd_header[MZQ_HEADER_BYTES];
    uint8_t selected_header[MZQ_HEADER_BYTES];
    uint16_t header_length, body_length;
    uint16_t record_count;
    uint16_t selected_length = 0U;
    uint32_t header_position, body_position, selected_position = 0UL;
    bool found = false;
    uint16_t crc;

    if (mzf_mzq_directory_matches(MZQ_CACHE_QDF))
    {
        uint8_t index;
        record_count = mzf_mzq_directory_count;
        if ((wanted_record == 0U) || (wanted_record > record_count)) return false;
        index = (uint8_t)(wanted_record - 1U);
        if (!mzf_qdf_read_cached_header(index, qd_header)) return false;
        body_length = (uint16_t)qd_header[20] |
                      ((uint16_t)qd_header[21] << 8U);
        body_position = mzf_mzq_directory_get_body_position(index);
        return mzf_qdf_select_record(wanted_record, record_count, qd_header,
                                     body_length, body_position);
    }

    mzf_mzq_directory_valid = false;
    mzf_qdf_scan_position = 16UL;
    mzf_mzq_raw_buffer_length = 0U;
    mzf_qd_analysis_begin();
    if (!mzf_qdf_find_sync() || !mzf_qdf_scan_read(&block_count) ||
        !mzf_qdf_scan_read(&crc_lo) || !mzf_qdf_scan_read(&crc_hi))
    { mzf_qd_analysis_finish(false); mzf_qd_set_error_unless_cancelled(PSTR("QDF HEADER"), MZF_PLAYBACK_BAD_FILE); return false; }
    crc = mzf_mzq_crc_byte(0U, 0xA5U);
    crc = mzf_mzq_crc_byte(crc, block_count);
    crc = mzf_mzq_crc_byte(crc, crc_lo);
    crc = mzf_mzq_crc_byte(crc, crc_hi);
    if ((crc != 0U) || (block_count == 0U) || ((block_count & 1U) != 0U) ||
        (block_count > (MZQ_MAX_FILES * 2U)))
    { mzf_qd_analysis_finish(false); mzf_qd_set_error_unless_cancelled(PSTR("QDF COUNT"), MZF_PLAYBACK_BAD_FILE); return false; }

    record_count = (uint16_t)(block_count / 2U);
    if ((wanted_record == 0U) || (wanted_record > record_count))
    { mzf_qd_analysis_finish(false); mzf_qd_set_error_unless_cancelled(PSTR("QDF RECORD"), MZF_PLAYBACK_BAD_FILE); return false; }
    for (uint16_t record = 1U; record <= record_count; ++record)
    {
        if (!mzf_qdf_read_frame(0x00U, qd_header, &header_length,
                                &header_position) ||
            !mzf_qdf_read_frame(0x05U, NULL, &body_length, &body_position) ||
            (((uint16_t)qd_header[20] | ((uint16_t)qd_header[21] << 8U)) != body_length))
        { mzf_qd_analysis_finish(false); mzf_qd_set_error_unless_cancelled(PSTR("QDF FRAME"), MZF_PLAYBACK_BAD_FILE); return false; }
        if (!mzf_mzq_directory_set_position((uint8_t)(record - 1U),
                                             header_position) ||
            !mzf_mzq_directory_set_body_position((uint8_t)(record - 1U),
                                                  body_position))
        { mzf_qd_analysis_finish(false); return false; }
        if (record == wanted_record)
        {
            memcpy(selected_header, qd_header, sizeof(selected_header));
            selected_length = body_length;
            selected_position = body_position;
            found = true;
        }
    }
    if (!found)
    { mzf_qd_analysis_finish(false); mzf_qd_set_error_unless_cancelled(PSTR("QDF SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    mzf_mzq_directory_file_size = mzf_file_size;
    mzf_mzq_directory_track_offset = 0UL;
    mzf_mzq_directory_track_length = 0UL;
    mzf_mzq_directory_phase = 0U;
    mzf_mzq_directory_count = (uint8_t)record_count;
    mzf_mzq_directory_kind = MZQ_CACHE_QDF;
    mzf_mzq_directory_valid = true;
    mzf_qd_analysis_finish(true);
    if (!mzf_qdf_select_record(wanted_record, record_count, selected_header,
                               selected_length, selected_position))
    { mzf_set_error_P(PSTR("QDF SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    return true;
}

static bool mzf_locate_logical_mzq_record(uint16_t wanted_record)
{
    uint8_t prefix[8];
    uint8_t block_count;
    uint8_t block_header[3];
    uint8_t qd_header[MZQ_HEADER_BYTES];
    uint8_t trailer[3];
    uint16_t record_count;
    uint32_t selected_data_offset = 0UL;
    uint16_t selected_data_length = 0U;
    bool found = false;

    if (wanted_record == 0U) wanted_record = 1U;
    if (!sdcard_file_seek(0UL))
    { mzf_set_error_P(PSTR("QD SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    int16_t received = sdcard_file_read(prefix, sizeof(prefix));
    if (received != (int16_t)sizeof(prefix))
    {
        mzf_set_error_P((received < 0) ? PSTR("QD READ") : PSTR("QD SHORT HDR"),
                        (received < 0) ? MZF_PLAYBACK_IO_ERROR : MZF_PLAYBACK_BAD_FILE);
        return false;
    }
    if ((prefix[0] != 0x00U) || (prefix[1] != 0x16U) ||
        (prefix[2] != 0x16U) || (prefix[3] != 0xA5U) ||
        (prefix[5] != 'C') || (prefix[6] != 'R') || (prefix[7] != 'C'))
    { mzf_set_error_P(PSTR("MZQ HEADER"), MZF_PLAYBACK_BAD_FILE); return false; }
    block_count = prefix[4];
    if (block_count == 0U)
    { mzf_set_error_P(PSTR("MZQ EMPTY"), MZF_PLAYBACK_BAD_FILE); return false; }
    if (((block_count & 1U) != 0U) || (block_count > (MZQ_MAX_FILES * 2U)))
    { mzf_set_error_P(PSTR("MZQ COUNT"), MZF_PLAYBACK_BAD_FILE); return false; }

    record_count = (uint16_t)(block_count / 2U);
    if (wanted_record > record_count)
    { mzf_set_error_P(PSTR("MZQ RECORD"), MZF_PLAYBACK_BAD_FILE); return false; }
    for (uint16_t record = 1U; record <= record_count; ++record)
    {
        uint16_t body_length;
        uint32_t body_offset;

        if (!mzf_mzq_find_frame() ||
            !mzf_mzq_read_logical(block_header, sizeof(block_header), PSTR("QD SHORT HDR")))
            return false;
        if ((block_header[0] != 0x00U) ||
            (block_header[1] != MZQ_HEADER_BYTES) ||
            (block_header[2] != 0x00U))
        { mzf_set_error_P(PSTR("MZQ FILE HDR"), MZF_PLAYBACK_BAD_FILE); return false; }
        if (!mzf_mzq_read_logical(qd_header, sizeof(qd_header), PSTR("QD SHORT HDR")) ||
            !mzf_mzq_read_logical(trailer, sizeof(trailer), PSTR("QD SHORT HDR")))
            return false;
        if ((trailer[0] != 'C') || (trailer[1] != 'R') || (trailer[2] != 'C'))
        { mzf_set_error_P(PSTR("QD HEADER CRC"), MZF_PLAYBACK_BAD_FILE); return false; }

        if (!mzf_mzq_find_frame() ||
            !mzf_mzq_read_logical(block_header, sizeof(block_header), PSTR("QD SHORT BODY")))
            return false;
        if ((block_header[0] != 0x01U) && (block_header[0] != 0x05U))
        { mzf_set_error_P(PSTR("MZQ DATA HDR"), MZF_PLAYBACK_BAD_FILE); return false; }

        body_length = (uint16_t)block_header[1] |
                      ((uint16_t)block_header[2] << 8U);
        body_offset = sdcard_file_position();
        if (((uint16_t)qd_header[20] | ((uint16_t)qd_header[21] << 8U)) !=
            body_length)
        { mzf_set_error_P(PSTR("MZQ SIZE"), MZF_PLAYBACK_BAD_FILE); return false; }
        if ((body_offset > mzf_file_size) ||
            ((mzf_file_size - body_offset) < ((uint32_t)body_length + 3UL)))
        { mzf_set_error_P(PSTR("QD SHORT BODY"), MZF_PLAYBACK_BAD_FILE); return false; }

        if (record == wanted_record)
        {
            mzf_mzq_build_tape_header(qd_header, body_length);
            selected_data_offset = body_offset;
            selected_data_length = body_length;
            found = true;
        }

        if (!sdcard_file_seek(body_offset + (uint32_t)body_length))
        { mzf_set_error_P(PSTR("MZQ SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
        received = sdcard_file_read(trailer, sizeof(trailer));
        if (received < 0)
        { mzf_set_error_P(PSTR("QD READ"), MZF_PLAYBACK_IO_ERROR); return false; }
        if ((received != (int16_t)sizeof(trailer)) ||
            (trailer[0] != 'C') || (trailer[1] != 'R') || (trailer[2] != 'C'))
        { mzf_set_error_P(PSTR("QD BODY CRC"), MZF_PLAYBACK_BAD_FILE); return false; }
    }

    mzf_mzt_record_count = record_count;
    if (!found)
    { mzf_set_error_P(PSTR("MZQ RECORD"), MZF_PLAYBACK_BAD_FILE); return false; }
    if (!sdcard_file_seek(selected_data_offset))
    { mzf_set_error_P(PSTR("MZQ SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }

    mzf_mzt_record_index = wanted_record;
    mzf_mzq_physical_source = false;
    mzf_record_data_length = selected_data_length;
    mzf_record_data_file_start = selected_data_offset;
    mzf_record_data_file_end = selected_data_offset + selected_data_length;
    mzf_header_offset = 0U;
    mzf_record_data_read = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_fifo_reset();
    return true;
}

static bool mzf_locate_mzq_record(uint16_t wanted_record)
{
    static const uint8_t qdf_signature[16] = {
        '-', 'Q', 'D', ' ', 'f', 'o', 'r', 'm', 'a', 't', '-',
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
    };
    uint8_t prefix[40];
    int16_t received;
    mzf_mzq_physical_source = false;
    mzf_qd_analysis_cancelled = false;
    if (!sdcard_file_seek(0UL))
    { mzf_set_error_P(PSTR("QD SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    received = sdcard_file_read(prefix, sizeof(prefix));
    if (received < 0)
    { mzf_set_error_P(PSTR("QD READ"), MZF_PLAYBACK_IO_ERROR); return false; }
    if (received < 8)
    { mzf_set_error_P(PSTR("QD SHORT"), MZF_PLAYBACK_BAD_FILE); return false; }

    if (memcmp(prefix, "HXCQDDRV", 8U) == 0)
    {
        if ((received < 40) || !mzf_mzq_configure_physical(prefix, true) ||
            !mzf_locate_physical_mzq_record(wanted_record))
        { mzf_qd_set_error_unless_cancelled(PSTR("QD HXC"), MZF_PLAYBACK_BAD_FILE); return false; }
        return true;
    }
    if ((received >= (int16_t)sizeof(qdf_signature)) &&
        (memcmp(prefix, qdf_signature, sizeof(qdf_signature)) == 0))
        return mzf_locate_qdf_record(wanted_record);
    if ((prefix[3] == 'Q') && (prefix[4] == 'D'))
    {
        if (!mzf_mzq_configure_physical(prefix, false) ||
            !mzf_locate_physical_mzq_record(wanted_record))
        { mzf_qd_set_error_unless_cancelled(PSTR("QD FLASH"), MZF_PLAYBACK_BAD_FILE); return false; }
        return true;
    }
    return mzf_locate_logical_mzq_record(wanted_record);
}

static bool mzf_resolve_next_mzt_loader(loader_mode_t *loader_mode,
                                        bool *loader_from_sidecar)
{
    bool current_loader_from_sidecar;
    if ((loader_mode == NULL) || (loader_from_sidecar == NULL) ||
        (mzf_format != FILE_FORMAT_MZT) || (mzf_mzt_record_index == 0U) ||
        (mzf_mzt_record_index >= mzf_mzt_record_count)) return false;
    current_loader_from_sidecar = mzf_mzt_record_loader_from_sidecar;
    if (!mzf_resolve_mzt_loader_mode((uint16_t)(mzf_mzt_record_index + 1U), loader_mode))
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
    if (!mzf_add_current_normal_record_duration(&half_milliseconds)) return false;
    mzf_total_duration_ms = (half_milliseconds == 0xFFFFFFFFUL) ?
        0xFFFFFFFFUL : (half_milliseconds + 1UL) / 2UL;
    if (!mzf_record_source_seek(mzf_original_data_offset))
    { mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    return true;
}

static bool mzf_prepare_current_record(loader_mode_t loader_mode)
{
    file_format_t loader_format = mzf_format;
    uint32_t loader_file_end = mzf_file_size;
    bool loader_active;
    mzf_mzq_duration_deferred = false;
    if (loader_mode == LOADER_MODE_AUTO) loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_configure_normal_speed(loader_mode);
    mzf_original_data_offset = mzf_mzq_physical_source ? 0UL : sdcard_file_position();
    mzf_original_data_length = mzf_record_data_length;
    mzf_mzt_record_loader_mode = loader_mode;
    mzf_capture_mzt_record_title();
    if (mzf_format == FILE_FORMAT_M12) loader_format = FILE_FORMAT_MZF;
    if (file_format_is_record_container(mzf_format))
    {
        loader_format = FILE_FORMAT_MZF;
        loader_file_end = mzf_mzq_physical_source ?
            mzf_record_data_length : mzf_record_data_file_end;
        if (!mzf_mzq_physical_source) mzf_total_duration_ms = 0UL;
        mzf_exact_duration_half_ms = 0UL;
    }
    loader_active = mzf_loader_prepare(loader_format, loader_mode, mzf_header,
                                       loader_file_end, mzf_original_data_offset);
    /* Refused fast loaders use the same safe NORMAL 1:1 fallback while the
       user browses records. Show the effective profile in the selector. */
    if (!loader_active &&
        (loader_mode != LOADER_MODE_NORMAL_1_1) &&
        (loader_mode != LOADER_MODE_NORMAL_1_2) &&
        (loader_mode != LOADER_MODE_NORMAL_1_3) &&
        (loader_mode != LOADER_MODE_NORMAL_1_4) &&
        (loader_mode != LOADER_MODE_MZ700_1X))
    {
        loader_mode = LOADER_MODE_NORMAL_1_1;
        mzf_loader_reset();
        mzf_configure_normal_speed(loader_mode);
        mzf_mzt_record_loader_mode = loader_mode;
        /* Single files were measured before loader preparation. Re-measure
           with the recovered NORMAL profile, not the rejected UL leaders. */
        if (!file_format_is_record_container(mzf_format))
        {
            if (!mzf_calculate_total_duration() || !mzf_read_header_record()) return false;
            mzf_total_duration_ms = (mzf_exact_duration_half_ms == 0xFFFFFFFFUL) ?
                0xFFFFFFFFUL : (mzf_exact_duration_half_ms + 1UL) / 2UL;
        }
    }
    if (loader_active)
    {
        if (!mzf_loader_patch_loader_header(mzf_header))
        { mzf_set_error_P(PSTR("LDR HEADER"), MZF_PLAYBACK_BAD_FILE); return false; }
        if (!mzf_loader_is_header_only() && !mzf_loader_is_ic_turbo() &&
            !mzf_loader_is_mz700_fast3() && !mzf_prepare_loader_block_data())
            return false;
        if (mzf_loader_is_tape_turbo())
        {
            if (!mzf_calculate_tape_turbo_duration())
            {
                if (mzf_state != MZF_PLAYBACK_IO_ERROR)
                    mzf_set_error_P(file_format_is_record_container(mzf_format) ?
                                        PSTR("IMAGE TIME") : PSTR("MZF TIME"),
                                    MZF_PLAYBACK_BAD_FILE);
                return false;
            }
        }
        else if (file_format_is_record_container(mzf_format))
        {
            mzf_total_duration_ms = 0UL;
            mzf_exact_duration_half_ms = 0UL;
        }
        else mzf_total_duration_ms = 0UL;
        if ((mzf_loader_is_ic_turbo() || mzf_loader_is_mz700_fast3()) &&
            !mzf_prepare_tape_turbo_payload()) return false;
    }
    else
    {
        if (mzf_mzq_physical_source)
        {
            /* Fill only the playback FIFO while selecting a physical record.
               Keep the whole-body duration scan on PLAY, so navigation does
               not decode the complete MFM payload for every selection. */
            mzf_exact_duration_half_ms = 0UL;
            mzf_record_data_read = 0UL;
            mzf_fifo_reset();
            if (!mzf_record_source_seek(mzf_original_data_offset))
            { mzf_set_error_P(PSTR("QD SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
            if (!mzf_prefill_data()) return false;
            mzf_mzq_duration_deferred = true;
        }
        else
        {
            if (file_format_is_record_container(mzf_format) &&
                !mzf_calculate_current_mzt_normal_duration()) return false;
            if (!mzf_prefill_data()) return false;
        }
    }
    mzf_begin_normal_stage(MZF_STAGE_HEADER);
    return true;
}

static bool mzf_start_next_mzt_record_resolved(loader_mode_t loader_mode,
                                                bool loader_from_sidecar)
{
    if ((mzf_mzt_record_index == 0U) ||
        (mzf_mzt_record_index >= mzf_mzt_record_count))
    { mzf_state = MZF_PLAYBACK_FINISHED; return true; }
    if (!sdcard_file_seek(mzf_original_data_offset + mzf_original_data_length))
    { mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    mzf_mzt_record_index++;
    if (!mzf_read_header_record()) return false;
    mzf_mzt_record_loader_from_sidecar = loader_from_sidecar;
    return mzf_prepare_current_record(loader_mode);
}

static bool mzf_start_next_mzt_record(void)
{
    loader_mode_t loader_mode;
    if ((mzf_mzt_record_index == 0U) ||
        (mzf_mzt_record_index >= mzf_mzt_record_count))
    { mzf_state = MZF_PLAYBACK_FINISHED; return true; }
    if (!sdcard_file_seek(mzf_original_data_offset + mzf_original_data_length))
    { mzf_set_error_P(PSTR("MZT SEEK"), MZF_PLAYBACK_IO_ERROR); return false; }
    mzf_mzt_record_index++;
    if (!mzf_read_header_record()) return false;
    if (!mzf_resolve_mzt_loader_mode(mzf_mzt_record_index, &loader_mode)) return false;
    return mzf_prepare_current_record(loader_mode);
}

static bool mzf_advance_after_boundary(void)
{
    if (mzf_stage == MZF_STAGE_HEADER)
    {
        if (mzf_loader_is_mz700_fast3())
        {
            if (!mzf_tape_turbo_payload_prepared && !mzf_prepare_tape_turbo_payload()) return false;
            mzf_begin_normal_stage(MZF_STAGE_TAPE_TURBO_DATA);
            return true;
        }
        if (mzf_loader_is_header_only())
        {
            if (mzf_loader_is_mz700_ul())
            { mzf_stop_timer_from_foreground(false); mz_read_set_fast(true); }
            else mzf_stop_timer_from_foreground(true);
            mzf_stage = MZF_STAGE_ULTRAFAST;
            mzf_boundary_waiting = false;
            mzf_motor_low_seen = 0U;
            mzf_boundary_auto_timer_armed = false;
            mzf_boundary_auto_start_ms = 0U;
            return true;
        }
        if (mzf_loader_is_ic_turbo())
        {
            if (!mzf_tape_turbo_payload_prepared && !mzf_prepare_tape_turbo_payload()) return false;
            mzf_begin_normal_stage(MZF_STAGE_TAPE_TURBO_DATA);
            return true;
        }
        mzf_begin_normal_stage(MZF_STAGE_DATA);
        return true;
    }
    if ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_tape_turbo())
    {
        if (!mzf_prepare_tape_turbo_payload()) return false;
        mzf_begin_normal_stage(MZF_STAGE_TAPE_TURBO_DATA);
        return true;
    }
    if ((mzf_stage != MZF_STAGE_DATA) && (mzf_stage != MZF_STAGE_TAPE_TURBO_DATA))
    { mzf_set_error_P(PSTR("MZF STATE"), MZF_PLAYBACK_BAD_FILE); return false; }
    if ((mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_ul_active())
    {
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
        if (sdcard_file_position() < mzf_file_size) return mzf_start_next_mzt_record();
    }
    else if ((mzf_format != FILE_FORMAT_MZQ) &&
             (mzf_stage == MZF_STAGE_DATA) &&
             (sdcard_file_position() < mzf_file_size))
    {
        mzf_record_data_length = mzf_file_size - sdcard_file_position();
        mzf_record_data_file_end = mzf_file_size;
        mzf_record_data_read = 0UL;
        mzf_fifo_reset();
        if (!mzf_prefill_data()) return false;
        mzf_begin_normal_stage(MZF_STAGE_DATA);
        return true;
    }
    mzf_state = MZF_PLAYBACK_FINISHED;
    return true;
}

static void mzf_service_boundary_auto_continue(void)
{
    bool ul_loader_boundary;
    bool tc_turbo_boundary;
    uint16_t now;
    if (!mzf_boundary_waiting || (mzf_state != MZF_PLAYBACK_RUNNING)) return;
    ul_loader_boundary = mzf_loader_is_ul_active() &&
                         (((mzf_stage == MZF_STAGE_DATA) && !mzf_loader_is_header_only()) ||
                          ((mzf_stage == MZF_STAGE_HEADER) && mzf_loader_is_header_only()));
    tc_turbo_boundary = (mzf_stage == MZF_STAGE_DATA) && mzf_loader_is_tc_turbo();
    if (mzf_motor_control_enabled && !ul_loader_boundary && !mz_motor_get()) return;
    if (!ul_loader_boundary)
    {
        now = (uint16_t)millis();
        if (!mzf_boundary_auto_timer_armed)
        {
            mzf_boundary_auto_start_ms = now;
            mzf_boundary_auto_timer_armed = true;
            if ((mzf_motor_low_seen == 0U) || tc_turbo_boundary) return;
        }
        else if (((mzf_motor_low_seen == 0U) || tc_turbo_boundary) &&
                 ((uint16_t)(now - mzf_boundary_auto_start_ms) <
                  mzf_boundary_auto_continue_ms())) return;
    }
    if (!mzf_advance_after_boundary()) return;
    if (mzf_state == MZF_PLAYBACK_FINISHED) return;
    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        if (!mzf_start_ultrafast_output() && (mzf_state == MZF_PLAYBACK_RUNNING))
            mzf_set_error_P(PSTR("UL START"), MZF_PLAYBACK_BAD_FILE);
        return;
    }
    mz_sense_set(false);
    if (!mzf_start_normal_output() && (mzf_state == MZF_PLAYBACK_RUNNING))
        mzf_set_error_P(PSTR("MZF START"), MZF_PLAYBACK_BAD_FILE);
}

void mzf_playback_set_qd_analysis_callbacks(
    mzf_qd_analysis_progress_callback_t progress_callback,
    mzf_qd_analysis_cancel_callback_t cancel_callback)
{
    mzf_qd_progress_callback = progress_callback;
    mzf_qd_cancel_callback = cancel_callback;
}

void mzf_playback_invalidate_qd_cache(void)
{
    mzf_mzq_directory_valid = false;
    mzf_mzq_directory_kind = MZQ_CACHE_NONE;
    mzf_mzq_directory_count = 0U;
    mzf_qd_analysis_active = false;
    mzf_qd_analysis_cancelled = false;
    mzf_qd_load_active = false;
    mzf_qd_load_cancelled = false;
}

void mzf_playback_init(void)
{
    mzf_stop_timer_from_foreground(true);
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
    mzf_mzq_physical_source = false;
    mzf_mzq_track_offset = 0UL;
    mzf_mzq_track_length = 0UL;
    mzf_mzq_data_phase = 0U;
    mzf_mzq_preferred_phase = 0U;
    mzf_mzq_body_logical_offset = 0UL;
    mzf_mzq_source_position = 0UL;
    mzf_mzq_raw_buffer_length = 0U;
    mzf_playback_invalidate_qd_cache();
    mzf_tape_turbo_payload_prepared = false;
    mzf_mzq_duration_deferred = false;
    mzf_stage = MZF_STAGE_NONE;
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
    mzf_loader_set_source_io(mzf_record_source_seek, mzf_record_source_read);
}

bool mzf_playback_prepare(const char *path, file_format_t format,
                          loader_mode_t loader_mode, uint16_t mzt_start_record,
                          bool motor_control_enabled)
{
    loader_mode_t first_record_mode = loader_mode;
    mzf_playback_stop();
    mzf_error_text[0] = '\0';
    if ((path == NULL) || !file_format_is_sharp_tape(format))
    { mzf_set_error_P(PSTR("MZF ARG"), MZF_PLAYBACK_BAD_FILE); return false; }
    mzf_format = format;
    mzf_source_path = path;
    mzf_requested_loader_mode = loader_mode;
    mzf_motor_control_enabled = motor_control_enabled;
    mzf_mzt_record_index = 0U;
    mzf_mzt_record_count = 0U;
    mzf_mzt_record_loader_mode = LOADER_MODE_NORMAL_1_1;
    mzf_mzt_record_loader_from_sidecar = false;
    mzf_mzt_record_title[0] = '\0';
    mzf_mzq_physical_source = false;
    if (file_format_is_record_container(format) && (mzt_start_record == 0U))
        mzt_start_record = 1U;
    if ((format != FILE_FORMAT_MZT) && (first_record_mode == LOADER_MODE_AUTO))
        first_record_mode = LOADER_MODE_NORMAL_1_1;
    mzf_configure_normal_speed((first_record_mode == LOADER_MODE_AUTO) ?
                               LOADER_MODE_NORMAL_1_1 : first_record_mode);
    if (!sdcard_file_open_read(path))
    { mzf_set_error_P(PSTR("MZF OPEN"), MZF_PLAYBACK_IO_ERROR); return false; }
    mzf_file_size = sdcard_file_size();
    if ((format != FILE_FORMAT_MZQ) && (mzf_file_size < MZF_HEADER_BYTES))
    {
        sdcard_file_close();
        mzf_set_error_P(PSTR("MZF SHORT"), MZF_PLAYBACK_BAD_FILE);
        return false;
    }
    if (format == FILE_FORMAT_MZT)
    {
        if (!mzf_locate_mzt_record(mzt_start_record)) { sdcard_file_close(); return false; }
        if (!mzf_resolve_mzt_loader_mode(mzf_mzt_record_index, &first_record_mode))
        { sdcard_file_close(); return false; }
    }
    else if (format == FILE_FORMAT_MZQ)
    {
        if (!mzf_locate_mzq_record(mzt_start_record))
        { sdcard_file_close(); return false; }
    }
    else
    {
        if (!mzf_calculate_total_duration() || !mzf_read_header_record())
        { sdcard_file_close(); return false; }
        mzf_total_duration_ms = (mzf_exact_duration_half_ms == 0xFFFFFFFFUL) ?
            0xFFFFFFFFUL : (mzf_exact_duration_half_ms + 1UL) / 2UL;
    }
    if (!mzf_prepare_current_record(first_record_mode))
    { sdcard_file_close(); return false; }
    mzf_state = MZF_PLAYBACK_READY;
    return true;
}

bool mzf_playback_start(void)
{
    if ((mzf_state != MZF_PLAYBACK_READY) && (mzf_state != MZF_PLAYBACK_PAUSED)) return false;
    mzf_qd_load_cancelled = false;
    if (mzf_mzq_duration_deferred)
    {
        mzf_qd_load_active = true;
        mzf_qd_analysis_cancelled = false;
        if (mzf_qd_progress_callback != NULL)
            mzf_qd_progress_callback(MZF_QD_PROGRESS_LOADING);
        if (!mzf_calculate_current_mzt_normal_duration() ||
            !mzf_record_source_seek(mzf_original_data_offset + mzf_record_data_read))
        {
            mzf_qd_load_active = false;
            if (mzf_qd_load_cancelled)
            {
                mzf_error_text[0] = '\0';
                mzf_state = MZF_PLAYBACK_READY;
                return false;
            }
            mzf_set_error_P(PSTR("QD SEEK"), MZF_PLAYBACK_IO_ERROR);
            return false;
        }
        /* Duration scanning uses the work buffer, leaving the prepared FIFO
           intact. Resume SD/MFM reads after those bytes instead of refilling. */
        mzf_qd_load_active = false;
        mzf_mzq_duration_deferred = false;
    }
    mzf_state = MZF_PLAYBACK_RUNNING;
    if (mzf_stage != MZF_STAGE_ULTRAFAST) mz_sense_set(false);
    if (mzf_paused_mid_pulse)
    {
        mzf_motor_low_seen = 0U;
        mzf_pwm_resume_from_foreground();
        return true;
    }
    if (mzf_stage == MZF_STAGE_ULTRAFAST) return mzf_start_ultrafast_output();
    return mzf_start_normal_output();
}

bool mzf_playback_qd_load_was_cancelled(void)
{
    return mzf_qd_load_cancelled;
}

bool mzf_playback_pause(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING) return false;
    if (mzf_fast3_start_delay_armed) { mzf_state = MZF_PLAYBACK_PAUSED; return true; }
    if (mzf_boundary_waiting)
    {
        if (!mzf_advance_after_boundary()) return false;
        if (mzf_state == MZF_PLAYBACK_FINISHED) return true;
        mzf_state = MZF_PLAYBACK_PAUSED;
        return true;
    }
    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    { mzf_state = MZF_PLAYBACK_PAUSED; return true; }
    mzf_pwm_pause_from_foreground();
    return true;
}

bool mzf_playback_resume(void) { return mzf_playback_start(); }

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
    mzf_configure_normal_speed(LOADER_MODE_NORMAL_1_1);
    mzf_record_data_length = 0UL;
    mzf_record_data_file_end = 0UL;
    mzf_record_data_file_start = 0UL;
    mzf_record_data_read = 0UL;
    mzf_original_data_offset = 0UL;
    mzf_original_data_length = 0UL;
    mzf_tape_turbo_payload_prepared = false;
    mzf_mzq_duration_deferred = false;
    mzf_qd_load_active = false;
    mzf_qd_load_cancelled = false;
    mzf_native_copy_index = 0U;
    mzf_native_repeat_refill_requested = false;
    mzf_native_repeat_refill_ready = false;
    mzf_loader_reset();
    mz_sense_set(true);
}

void mzf_playback_service(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING) return;
    if (mzf_fast3_start_delay_armed)
    {
        if ((uint16_t)((uint16_t)millis() - mzf_fast3_start_delay_started_ms) <
            MZF_MZ700_FAST3_START_DELAY_MS) return;
        mzf_fast3_start_delay_armed = false;
        if (!mzf_start_normal_output_immediate() && (mzf_state == MZF_PLAYBACK_RUNNING))
            mzf_set_error_P(PSTR("MZF START"), MZF_PLAYBACK_BAD_FILE);
        return;
    }
    if (mzf_boundary_waiting) { mzf_service_boundary_auto_continue(); return; }
    if (mzf_stage == MZF_STAGE_ULTRAFAST)
    {
        if (!mzf_loader_pump(MZF_LOADER_PUMP_BYTES))
        { mzf_set_error(mzf_loader_get_error_text(), MZF_PLAYBACK_IO_ERROR); return; }
        if (mzf_loader_is_finished())
        {
            loader_mode_t next_loader_mode;
            bool next_loader_from_sidecar;
            mzf_boundary_waiting = false;
            mzf_boundary_auto_timer_armed = false;
            mzf_boundary_auto_start_ms = 0U;
            if ((mzf_format == FILE_FORMAT_MZT) &&
                (mzf_mzt_record_index < mzf_mzt_record_count))
            {
                if (!mzf_resolve_next_mzt_loader(&next_loader_mode,
                                                 &next_loader_from_sidecar)) return;
                if (!mzf_loader_mode_is_ul_family(next_loader_mode))
                {
                    if (!mzf_start_next_mzt_record_resolved(next_loader_mode,
                                                            next_loader_from_sidecar)) return;
                    if (mzf_state != MZF_PLAYBACK_FINISHED)
                    {
                        mz_sense_set(false);
                        if (!mzf_start_normal_output() &&
                            (mzf_state == MZF_PLAYBACK_RUNNING))
                            mzf_set_error_P(PSTR("MZF START"), MZF_PLAYBACK_BAD_FILE);
                    }
                    return;
                }
            }
            mzf_state = MZF_PLAYBACK_FINISHED;
            mz_sense_set(true);
        }
        return;
    }
    if (mzf_native_mz700 && (mzf_stage == MZF_STAGE_DATA) &&
        mzf_native_repeat_refill_requested && !mzf_prepare_native_mz700_repeat())
    {
        if (mzf_state == MZF_PLAYBACK_RUNNING)
            mzf_set_error_P(PSTR("MZ7 REFILL"), MZF_PLAYBACK_IO_ERROR);
        return;
    }
    if (((mzf_stage == MZF_STAGE_DATA) ||
         (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)) && !mzf_boundary_waiting &&
        (mzf_fifo_used_snapshot() <= MZF_REFILL_RESERVE) &&
        (mzf_record_data_read < mzf_record_data_length))
        (void)mzf_refill_data_once();
}

mzf_playback_state_t mzf_playback_get_state(void)
{
    uint8_t state;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { state = mzf_state; }
    return (mzf_playback_state_t)state;
}

const char *mzf_playback_get_error_text(void) { return mzf_error_text; }

uint8_t mzf_playback_get_buffer_fill_percent(void)
{
    uint16_t used;
    uint32_t consumed, remaining, target, percent;
    if ((mzf_stage != MZF_STAGE_HEADER) && (mzf_stage != MZF_STAGE_DATA) &&
        (mzf_stage != MZF_STAGE_TAPE_TURBO_DATA)) return 100U;
    if ((mzf_stage == MZF_STAGE_HEADER) && mzf_loader_is_header_only()) return 100U;
    if (mzf_record_data_length == 0UL) return 100U;
    used = mzf_fifo_used_snapshot();
    if (used > MZF_FIFO_CAPACITY) return 0U;
    if (mzf_record_data_read < (uint32_t)used) return 0U;
    consumed = mzf_record_data_read - (uint32_t)used;
    if (consumed >= mzf_record_data_length) return 100U;
    remaining = mzf_record_data_length - consumed;
    target = (remaining < (uint32_t)MZF_FIFO_CAPACITY) ? remaining : (uint32_t)MZF_FIFO_CAPACITY;
    if (target == 0UL) return 100U;
    percent = ((uint32_t)used * 100UL) / target;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}

uint16_t mzf_playback_get_mzt_record_index(void)
{ return file_format_is_record_container(mzf_format) ? mzf_mzt_record_index : 0U; }
uint16_t mzf_playback_get_mzt_record_count(void)
{ return file_format_is_record_container(mzf_format) ? mzf_mzt_record_count : 0U; }
const char *mzf_playback_get_mzt_record_title(void)
{ return file_format_is_record_container(mzf_format) ? mzf_mzt_record_title : ""; }
loader_mode_t mzf_playback_get_mzt_record_loader_mode(void)
{
    return mzf_mzt_record_loader_mode;
}
bool mzf_playback_get_mzt_record_loader_from_sidecar(void)
{
    return (mzf_format == FILE_FORMAT_MZT) && mzf_mzt_record_loader_from_sidecar;
}
uint32_t mzf_playback_get_total_duration_ms(void) { return mzf_total_duration_ms; }

static uint16_t mzf_progress_gap_pulses(mzf_stage_t stage)
{
    if (stage == MZF_STAGE_HEADER) return mzf_profile_header_leader;
    if (stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        switch (mzf_loader_get_variant())
        {
            case MZF_LOADER_VARIANT_TC_1_1: return MZF_TC_1_1_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_2: return MZF_TC_1_2_TURBO_GAP_SHORT_PULSES;
            case MZF_LOADER_VARIANT_TC_1_3: return MZF_TC_1_3_TURBO_GAP_SHORT_PULSES;
            default: return MZF_IC_TURBO_GAP_SHORT_PULSES;
        }
    }
    return mzf_profile_data_leader;
}

static uint16_t mzf_progress_mark_long_pulses(mzf_stage_t stage)
{
    if (stage == MZF_STAGE_HEADER) return MZF_MZ800_LONG_MARK_LONG_PULSES;
    return (stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_LONG_PULSES : MZF_MZ800_SHORT_MARK_LONG_PULSES;
}
static uint16_t mzf_progress_mark_short_pulses(mzf_stage_t stage)
{
    if (stage == MZF_STAGE_HEADER) return MZF_MZ800_LONG_MARK_SHORT_PULSES;
    return (stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_SHORT_PULSES : MZF_MZ800_SHORT_MARK_SHORT_PULSES;
}
static uint16_t mzf_progress_mark_final_pulses(mzf_stage_t stage)
{
    return (stage == MZF_STAGE_TAPE_TURBO_DATA) ?
        MZF_IC_TURBO_MARK_FINAL_LONG_PULSES : MZF_MZ800_TAPE_MARK_FINAL_LONG_PULSES;
}
static uint16_t mzf_progress_trailing_pulses(mzf_stage_t stage)
{
    return (mzf_loader_is_tc_turbo() &&
            ((stage == MZF_STAGE_DATA) || (stage == MZF_STAGE_TAPE_TURBO_DATA))) ?
        MZF_TC_LOADER_TRAILING_SHORT_PULSES : MZF_MZ800_TRAILING_LONG_PULSES;
}
static uint16_t mzf_progress_done_pulses(uint16_t total, uint16_t remaining)
{ return (remaining >= total) ? 0U : (uint16_t)(total - remaining); }

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
    if (((step == MZF_STEP_BYTE_BITS) || (step == MZF_STEP_BYTE_STOP)) &&
        (bytes_read != 0UL))
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
        case MZF_STEP_BEGIN: return 0UL;
        case MZF_STEP_GAP: return (uint32_t)mzf_progress_done_pulses(gap, loop);
        case MZF_STEP_TAPE_MARK_LONG:
            return (uint32_t)gap + (uint32_t)mzf_progress_done_pulses(mark_long, loop);
        case MZF_STEP_TAPE_MARK_SHORT:
            return (uint32_t)gap + (uint32_t)mark_long +
                   (uint32_t)mzf_progress_done_pulses(mark_short, loop);
        case MZF_STEP_TAPE_MARK_FINAL:
            return (uint32_t)gap + (uint32_t)mark_long + (uint32_t)mark_short +
                   (uint32_t)mzf_progress_done_pulses(mark_final, loop);
        case MZF_STEP_BYTE_LOAD:
        case MZF_STEP_BYTE_BITS:
        case MZF_STEP_BYTE_STOP:
            return preamble + mzf_progress_byte_units(step, bytes_read, byte_count,
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
        default: return 0UL;
    }
}

uint8_t mzf_playback_get_progress_percent(void)
{
    uint32_t percent, total, done = 0UL, header_units, loader_units = 0UL,
             data_units = 0UL, stage_bytes_read;
    uint16_t read_sequence, loop;
    uint8_t header_offset, bits_remaining, checksum_byte_index;
    mzf_stage_t stage;
    mzf_normal_step_t step;
    if (!mzf_loader_is_active()) return 0U;
    if (mzf_stage == MZF_STAGE_ULTRAFAST) return mzf_loader_get_progress_percent();
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        stage = mzf_stage; step = mzf_normal_step; loop = mzf_normal_loop;
        header_offset = mzf_header_offset; read_sequence = mzf_fifo_read_sequence;
        bits_remaining = mzf_normal_bits_remaining;
        checksum_byte_index = mzf_normal_checksum_byte_index;
    }
    header_units = mzf_progress_stage_total_units(MZF_STAGE_HEADER, MZF_HEADER_BYTES);
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
    if (total == 0UL) return 0U;
    if (stage == MZF_STAGE_HEADER)
        done = mzf_progress_stage_done_units(stage, step, loop,
                                             (uint32_t)header_offset,
                                             MZF_HEADER_BYTES, bits_remaining,
                                             checksum_byte_index);
    else if (stage == MZF_STAGE_DATA)
    {
        stage_bytes_read = (uint32_t)read_sequence;
        done = header_units + mzf_progress_stage_done_units(
            stage, step, loop, stage_bytes_read,
            (uint32_t)mzf_loader_get_loader_size(), bits_remaining,
            checksum_byte_index);
    }
    else if (stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        stage_bytes_read = (uint32_t)read_sequence;
        done = header_units + loader_units + mzf_progress_stage_done_units(
            stage, step, loop, stage_bytes_read, mzf_original_data_length,
            bits_remaining, checksum_byte_index);
    }
    else if (stage != MZF_STAGE_NONE) done = total;
    if (done > total) done = total;
    percent = (done * 100UL) / total;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}

mzf_playback_phase_t mzf_playback_get_progress_phase(void)
{
    if (!mzf_loader_is_active()) return MZF_PLAYBACK_PHASE_NORMAL;
    if (mzf_stage == MZF_STAGE_ULTRAFAST) return MZF_PLAYBACK_PHASE_ULTRAFAST_DATA;
    if (mzf_stage == MZF_STAGE_TAPE_TURBO_DATA)
    {
        if (mzf_loader_is_mz700_fast3())
            return mzf_loader_is_mz700_fast3_high() ?
                MZF_PLAYBACK_PHASE_MZ700_FAST3_HIGH : MZF_PLAYBACK_PHASE_MZ700_FAST3_LOW;
        return mzf_loader_is_tc_turbo() ?
            MZF_PLAYBACK_PHASE_TC_TURBO_DATA : MZF_PLAYBACK_PHASE_IC_TURBO_DATA;
    }
    if (mzf_loader_is_ic_turbo()) return MZF_PLAYBACK_PHASE_IC_TURBO_DATA;
    if (mzf_loader_is_tc_turbo()) return MZF_PLAYBACK_PHASE_TC_TURBO_LOADER;
    switch (mzf_loader_get_variant())
    {
        case MZF_LOADER_VARIANT_LOW: return MZF_PLAYBACK_PHASE_ULTRAFAST_LOADER_LOW;
        case MZF_LOADER_VARIANT_HIGH: return MZF_PLAYBACK_PHASE_ULTRAFAST_LOADER_HIGH;
        case MZF_LOADER_VARIANT_MZ800_HEADER: return MZF_PLAYBACK_PHASE_ULTRAFAST_HEADER;
        case MZF_LOADER_VARIANT_MZ700_UL_LOW: return MZF_PLAYBACK_PHASE_MZ700_UL_LOW;
        case MZF_LOADER_VARIANT_MZ700_UL_HIGH: return MZF_PLAYBACK_PHASE_MZ700_UL_HIGH;
        case MZF_LOADER_VARIANT_MZ700_FAST3_LOW: return MZF_PLAYBACK_PHASE_MZ700_FAST3_LOW;
        case MZF_LOADER_VARIANT_MZ700_FAST3_HIGH: return MZF_PLAYBACK_PHASE_MZ700_FAST3_HIGH;
        default: return MZF_PLAYBACK_PHASE_NORMAL;
    }
}

bool mzf_playback_is_ul_loader_active(void) { return mzf_loader_is_ul_active(); }

static void mzf_sample_motor_from_isr(void)
{
    if (mz_motor_sample_from_isr() == 0U) mzf_motor_low_seen = 1U;
}

bool mzf_playback_timer3_compb_from_isr(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING) return false;
    mzf_sample_motor_from_isr();
    if (mzf_pwm_bootstrap_pending)
    {
        TIFR3 = _BV(TOV3);
        mzf_pwm_bootstrap_pending = false;
        if (mzf_queue_next_pwm_pulse_from_isr())
        { TIMSK3 |= _BV(TOIE3); return true; }
        if (mzf_pwm_terminal_pending != MZF_PWM_TERMINAL_NONE)
        {
            mzf_pwm_arm_terminal_from_isr();
            mzf_pwm_disconnect_output_from_isr();
            return true;
        }
        mzf_stop_timer_from_isr();
        return false;
    }
    if (mzf_pwm_stop_pending) mzf_pwm_disconnect_output_from_isr();
    return true;
}

static bool mzf_playback_timer3_ovf_from_isr(void)
{
    if (mzf_state != MZF_PLAYBACK_RUNNING)
    { mzf_stop_timer_from_isr(); return false; }
    mzf_sample_motor_from_isr();
    if (mzf_pwm_stop_pending)
    { mzf_pwm_finish_terminal_from_isr(); return false; }
    if (!mzf_pwm_next_valid)
    {
        mzf_set_error_from_isr_P(PSTR("PWM PIPE"), MZF_PLAYBACK_BAD_FILE);
        mzf_stop_timer_from_isr();
        return false;
    }
    mzf_pwm_current_compare_ticks = mzf_pwm_next_compare_ticks;
    mzf_pwm_next_valid = false;
    if (mzf_queue_next_pwm_pulse_from_isr()) return true;
    if (mzf_pwm_terminal_pending != MZF_PWM_TERMINAL_NONE)
    { mzf_pwm_arm_terminal_from_isr(); return true; }
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
