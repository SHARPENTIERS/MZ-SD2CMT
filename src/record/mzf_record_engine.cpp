#include "mzf_record_engine.h"

#include <Arduino.h>
#include <string.h>

#include "edge_record_driver.h"
#include "record_autoname.h"
#include "record_path_buffer.h"
#include "../drivers/flash_text.h"
#include "../drivers/sdcard.h"
#include "../formats/mzi_sidecar.h"
#include "../formats/mz_tape_decoder.h"
#include "../formats/mz_loader_profiles.h"
#include "../streams/cmt_mode_scratch.h"
#include "../streams/cmt_error_buffers.h"
#include "../streams/wav_sample_stream.h"

#define MZF_STAGE_BYTES 512U
#define MZF_PREALLOCATE_BYTES (128UL + 65535UL)

#define mzf_stage0_buffer wav_sample_stream_get_shared_work_buffer()
#define mzf_stage1_buffer cmt_mode_scratch.edge_record_stage_bytes

typedef enum
{
    MZF_PARSE_NORMAL = 0,
    MZF_PARSE_UNIT,
    MZF_PARSE_LONG_TAIL
} mzf_parse_state_t;

static mzf_record_engine_state_t record_state = MZF_RECORD_ENGINE_STOPPED;
#define record_error_text cmt_record_backend_error
static uint16_t stage_count[2];
static bool stage_ready[2];
static uint8_t stage_fill_index;
static uint8_t stage_write_index;
static uint32_t bytes_staged;
static uint32_t bytes_written;
static uint32_t expected_file_bytes;
static uint32_t expected_data_bytes;
static bool header_valid;
static bool block_valid;
static bool user_stop_requested;
static bool duplicate_data_active;
static bool duplicate_recovery_pending;
static bool tc_loader_pending;
static mz_copier_profile_t copier_profile;
static mzi_record_profile_t mzi_profile;
static mzf_parse_state_t parse_state;
static uint8_t pending_short_units;
static uint8_t signal_level;

static uint8_t *stage_buffer(uint8_t index)
{
    return (index == 0U) ? mzf_stage0_buffer : mzf_stage1_buffer;
}

static void set_error_P(PGM_P text)
{
    flash_text_copy(record_error_text, sizeof(record_error_text), text);
}

static uint16_t difference_u16(uint16_t left, uint16_t right)
{
    return (left >= right) ? (uint16_t)(left - right) :
                             (uint16_t)(right - left);
}

/* The neutral decoder learns the complete short pulse in 16 us units x8.
   QDTool-compatible profiles center close to 248, 126 and 106.  Metadata-free
   NORMAL 1:4 samples center close to 97 and use an approximately
   11,000-pulse header pilot.  Requiring both properties prevents a fast or
   speed-shifted NORMAL pilot from being classified from pulse width alone. */
static mzi_record_profile_t profile_from_header_tone(uint16_t short_x8,
                                                     uint16_t leader_pulses)
{
    uint16_t d1;
    uint16_t d2;
    uint16_t d3;
    uint16_t d4;

    if (short_x8 == 0U) return MZI_RECORD_PROFILE_NORMAL_1_1;
    d1 = difference_u16(short_x8, 248U);
    d2 = difference_u16(short_x8, 126U);
    d3 = difference_u16(short_x8, 106U);
    d4 = difference_u16(short_x8, 97U);
    if ((leader_pulses >= 8000U) && (leader_pulses <= 13000U) &&
        (d4 < d3) && (d4 < d2) && (d4 < d1))
    {
        return MZI_RECORD_PROFILE_NORMAL_1_4;
    }
    if ((d2 < d1) && (d2 <= d3)) return MZI_RECORD_PROFILE_NORMAL_1_2;
    if ((d3 < d1) && (d3 < d2)) return MZI_RECORD_PROFILE_NORMAL_1_3;
    return MZI_RECORD_PROFILE_NORMAL_1_1;
}

static mzi_record_profile_t profile_from_copier(mz_copier_profile_t profile)
{
    switch (profile)
    {
        case MZ_COPIER_IC_1_4: return MZI_RECORD_PROFILE_IC_1_4;
        case MZ_COPIER_IC_1_3: return MZI_RECORD_PROFILE_IC_1_3;
        case MZ_COPIER_IC_1_2: return MZI_RECORD_PROFILE_IC_1_2;
        case MZ_COPIER_TC_1_3: return MZI_RECORD_PROFILE_TC_1_3;
        case MZ_COPIER_TC_1_2: return MZI_RECORD_PROFILE_TC_1_2;
        case MZ_COPIER_NONE:
        default: return MZI_RECORD_PROFILE_NORMAL_1_1;
    }
}

static bool make_path(const char *directory_path)
{
    uint16_t sequence;
    int length;

    if (!sdcard_next_record_sequence(directory_path, &sequence))
    {
        strncpy(record_error_text, sdcard_last_error(),
                sizeof(record_error_text) - 1U);
        record_error_text[sizeof(record_error_text) - 1U] = '\0';
        return false;
    }
    length = flash_text_snprintf(record_path_buffer,
                                 sizeof(record_path_buffer),
                                 PSTR("%s/REC%04u.MZF"), directory_path,
                                 (unsigned int)sequence);
    if ((length <= 0) || ((size_t)length >= sizeof(record_path_buffer)))
    {
        set_error_P(PSTR("PATH TOO LONG"));
        return false;
    }
    return true;
}

static void fail_close_P(PGM_P text)
{
    if (text != NULL) set_error_P(text);
    edge_record_driver_abort();
    mz_tape_decoder_stop();
    sdcard_file_close();
    if (record_path_buffer[0] != '\0')
        (void)sdcard_file_remove(record_path_buffer);
    record_state = MZF_RECORD_ENGINE_ERROR;
}

static bool emit_byte(uint8_t value)
{
    uint8_t index = stage_fill_index;
    if (stage_ready[index]) return false;
    stage_buffer(index)[stage_count[index]++] = value;
    bytes_staged++;
    if (stage_count[index] == MZF_STAGE_BYTES)
    {
        stage_ready[index] = true;
        stage_fill_index ^= 1U;
    }
    return true;
}

static bool emit_header(const uint8_t *header)
{
    if (header == NULL) return false;
    for (uint8_t index = 0U; index < MZ_TAPE_HEADER_BYTES; ++index)
    {
        if (!emit_byte(header[index])) return false;
    }
    return true;
}

static bool write_one_stage(void)
{
    uint8_t index = stage_write_index;
    uint16_t count;
    if (!stage_ready[index] || sdcard_file_is_busy()) return true;
    count = stage_count[index];
    if ((count == 0U) || (count > MZF_STAGE_BYTES) ||
        (sdcard_file_write(stage_buffer(index), count) != (int16_t)count))
    {
        set_error_P(PSTR("MZF WRITE"));
        return false;
    }
    bytes_written += count;
    stage_count[index] = 0U;
    stage_ready[index] = false;
    stage_write_index ^= 1U;
    return true;
}

static void mark_tail_ready(void)
{
    uint8_t index = stage_fill_index;
    if ((stage_count[index] != 0U) && !stage_ready[index])
    {
        stage_ready[index] = true;
        stage_fill_index ^= 1U;
    }
}

static bool accept_complete_payload(void)
{
    block_valid = header_valid && (bytes_staged == expected_file_bytes);
    if (!block_valid) return false;
    edge_record_driver_stop();
    mark_tail_ready();
    record_state = MZF_RECORD_ENGINE_FINALIZING;
    return true;
}

static bool accept_decoder_events(void)
{
    mz_tape_decoder_event_t event;
    while (mz_tape_decoder_take_event(&event))
    {
        if (event.type == MZ_TAPE_DECODER_EVENT_HEADER_VALID)
        {
            const uint8_t *header = mz_tape_decoder_get_header();
            const uint8_t *logical_header = header;
            uint16_t header_short_x8 = mz_tape_decoder_get_header_short_x8();
            if (header_valid || tc_loader_pending) return false;

            if (mz_loader_profile_recognize_ic(
                    header, mzf_stage0_buffer, &copier_profile))
            {
                logical_header = mzf_stage0_buffer;
                mzi_profile = profile_from_copier(copier_profile);
            }
            else if (mz_loader_profile_recognize_tc_header(header))
            {
                tc_loader_pending = true;
                record_autoname_accept_header(header);
                mz_tape_decoder_start_data(MZ_TC_LOADER_BYTES, false);
                continue;
            }
            else
            {
                mzi_profile = profile_from_header_tone(
                    header_short_x8, event.leader_pulses);
            }

            if (!emit_header(logical_header)) return false;
            expected_data_bytes = (uint32_t)logical_header[0x12U] |
                                  ((uint32_t)logical_header[0x13U] << 8U);
            expected_file_bytes = 128UL + expected_data_bytes;
            header_valid = true;
            record_autoname_accept_header(logical_header);
            mz_tape_decoder_start_data(expected_data_bytes, false);
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_DATA_BYTE)
        {
            if (tc_loader_pending)
            {
                if (event.byte_index >= MZ_TC_LOADER_BYTES) return false;
                mzf_stage0_buffer[event.byte_index] = event.value;
                continue;
            }
            if (!header_valid || (event.byte_index >= expected_data_bytes))
                return false;
            if (!emit_byte(event.value)) return false;
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_BLOCK_VALID)
        {
            if (tc_loader_pending)
            {
                const uint8_t *loader_header = mz_tape_decoder_get_header();
                if (!mz_loader_profile_normalize_tc(
                        loader_header, mzf_stage0_buffer,
                        mzf_stage1_buffer, &copier_profile))
                {
                    set_error_P(PSTR("TC LOADER"));
                    return false;
                }
                mzi_profile = profile_from_copier(copier_profile);
                stage_count[0] = stage_count[1] = 0U;
                stage_ready[0] = stage_ready[1] = false;
                stage_fill_index = stage_write_index = 0U;
                bytes_staged = bytes_written = 0UL;
                if (!emit_header(mzf_stage1_buffer)) return false;
                expected_data_bytes =
                    (uint32_t)mzf_stage1_buffer[0x12U] |
                    ((uint32_t)mzf_stage1_buffer[0x13U] << 8U);
                expected_file_bytes = 128UL + expected_data_bytes;
                header_valid = true;
                tc_loader_pending = false;
                record_autoname_accept_header(mzf_stage1_buffer);
                /* TC PLAY inverts the complete generated tape, not the
                   payload relative to its loader.  Real TC/Intercopy SAVE
                   therefore keeps the physical pulse-start phase here. */
                mz_tape_decoder_start_data(expected_data_bytes, false);
                continue;
            }
            /* NORMAL MZ700 and MZ800 both finish on the first valid copy.
               A second copy is requested only from the INVALID path below,
               so single-copy saves do not end as MZF INCOMPLETE. */
            if (!accept_complete_payload()) return false;
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_BLOCK_INVALID)
        {
            if (tc_loader_pending)
            {
                set_error_P(PSTR("TC CHECKSUM"));
                return false;
            }
            if (duplicate_data_active)
            {
                set_error_P(PSTR("MZF CHECKSUM"));
                return false;
            }
            else
            {
                if (copier_profile == MZ_COPIER_NONE)
                {
                    /* First NORMAL copy failed. Re-arm for the optional
                       second copy for both MZ700 and MZ800. */
                    duplicate_recovery_pending = true;
                }
                else
                {
                    set_error_P(PSTR("MZF CHECKSUM"));
                    return false;
                }
            }
        }
    }
    return true;
}

static bool feed_interval(uint8_t units)
{
    if ((units == 0U) || (units > 127U)) return false;

    if (!mz_tape_decoder_feed_interval(units, signal_level))
    {
        signal_level ^= 1U;
        return true;
    }

    signal_level ^= 1U;

    /* Poll events only when the shared decoder says this interval produced
       one.  Long leaders therefore avoid an empty take_event() call on every
       pulse, matching the optimized WAV/LEP/L16 path. */
    return accept_decoder_events();
}

static bool pump_fifo(void)
{
    while (!stage_ready[stage_fill_index] &&
           !duplicate_recovery_pending &&
           (record_state != MZF_RECORD_ENGINE_FINALIZING || !block_valid))
    {
        uint8_t value;
        if (pending_short_units != 0U)
        {
            uint8_t units = pending_short_units;
            pending_short_units = 0U;
            if (!feed_interval(units)) return false;
            continue;
        }
        if (!edge_record_driver_pop_byte(&value)) return true;
        if (parse_state == MZF_PARSE_UNIT)
        {
            parse_state = MZF_PARSE_NORMAL;
            if ((value < 16U) || !feed_interval(value)) return false;
            continue;
        }
        if (parse_state == MZF_PARSE_LONG_TAIL)
        {
            parse_state = MZF_PARSE_NORMAL;
            mz_tape_decoder_break_signal();
            signal_level ^= 1U;
            continue;
        }
        if (value == EDGE_RECORD_TOKEN_UNIT)
        {
            parse_state = MZF_PARSE_UNIT;
            continue;
        }
        if (value == EDGE_RECORD_TOKEN_LONG_BLOCK)
        {
            mz_tape_decoder_break_signal();
            continue;
        }
        if (value == EDGE_RECORD_TOKEN_LONG_TAIL)
        {
            parse_state = MZF_PARSE_LONG_TAIL;
            continue;
        }
        {
            uint8_t first = (uint8_t)(value >> 4U);
            uint8_t second = (uint8_t)(value & 0x0FU);
            if ((first == 0U) || !feed_interval(first)) return false;
            pending_short_units = second;
        }
    }
    return true;
}

void mzf_record_engine_init(void)
{
    record_state = MZF_RECORD_ENGINE_STOPPED;
    record_error_text[0] = '\0';
    stage_count[0] = stage_count[1] = 0U;
    stage_ready[0] = stage_ready[1] = false;
    stage_fill_index = stage_write_index = 0U;
    bytes_staged = bytes_written = 0UL;
    expected_file_bytes = expected_data_bytes = 0UL;
    header_valid = block_valid = user_stop_requested = false;
    duplicate_data_active = duplicate_recovery_pending = false;
    tc_loader_pending = false;
    copier_profile = MZ_COPIER_NONE;
    mzi_profile = MZI_RECORD_PROFILE_NORMAL_1_1;
    parse_state = MZF_PARSE_NORMAL;
    pending_short_units = 0U;
    signal_level = 0U;
}

bool mzf_record_engine_preview_filename(const char *directory_path)
{
    return make_path(directory_path);
}

bool mzf_record_engine_start(const char *directory_path)
{
    mzf_record_engine_init();
    if (!sdcard_is_mounted())
    {
        set_error_P(PSTR("SD CARD ERROR"));
        record_state = MZF_RECORD_ENGINE_ERROR;
        return false;
    }
    if (!edge_record_driver_prepare(16U) || !make_path(directory_path))
    {
        record_state = MZF_RECORD_ENGINE_ERROR;
        return false;
    }
    if (!sdcard_file_open_write(record_path_buffer))
    {
        set_error_P(PSTR("MZF CREATE"));
        record_state = MZF_RECORD_ENGINE_ERROR;
        return false;
    }
    if (!sdcard_file_preallocate(MZF_PREALLOCATE_BYTES) ||
        !sdcard_file_seek(0UL) || !sdcard_file_sync())
    {
        fail_close_P(PSTR("PREALLOC FAIL"));
        return false;
    }
    mz_tape_decoder_begin_header();
    if (!edge_record_driver_start())
    {
        fail_close_P(PSTR("MZF START"));
        return false;
    }
    signal_level = edge_record_driver_get_initial_level();
    record_state = MZF_RECORD_ENGINE_RECORDING;
    return true;
}

bool mzf_record_engine_pause(void)
{
    if ((record_state == MZF_RECORD_ENGINE_RECORDING) &&
        edge_record_driver_pause())
    {
        record_state = MZF_RECORD_ENGINE_PAUSED;
        return true;
    }
    return false;
}

bool mzf_record_engine_resume(void)
{
    if ((record_state == MZF_RECORD_ENGINE_PAUSED) &&
        edge_record_driver_resume())
    {
        record_state = MZF_RECORD_ENGINE_RECORDING;
        return true;
    }
    return false;
}

void mzf_record_engine_request_stop(void)
{
    if ((record_state != MZF_RECORD_ENGINE_RECORDING) &&
        (record_state != MZF_RECORD_ENGINE_PAUSED)) return;
    edge_record_driver_stop();
    user_stop_requested = true;
    record_state = MZF_RECORD_ENGINE_FINALIZING;
}

void mzf_record_engine_cancel(void)
{
    edge_record_driver_abort();
    mz_tape_decoder_stop();
    sdcard_file_close();
    if (record_path_buffer[0] != '\0')
        (void)sdcard_file_remove(record_path_buffer);
    record_state = MZF_RECORD_ENGINE_STOPPED;
}

void mzf_record_engine_service(void)
{
    edge_record_driver_state_t driver_state = edge_record_driver_get_state();
    if ((record_state == MZF_RECORD_ENGINE_RECORDING) ||
        (record_state == MZF_RECORD_ENGINE_PAUSED) ||
        (record_state == MZF_RECORD_ENGINE_FINALIZING))
    {
        if (driver_state == EDGE_RECORD_DRIVER_OVERRUN)
        {
            fail_close_P(PSTR("MZF OVERFLOW"));
            return;
        }
        if (duplicate_recovery_pending)
        {
            if (sdcard_file_is_busy()) return;
            /* Discard the failed first NORMAL copy and overwrite the same
               provisional file from byte zero.  The duplicate decoder
               accepts the native MZ700 separator; after a signal break or
               continued leader it also reacquires a full MZ800 block. */
            stage_count[0] = stage_count[1] = 0U;
            stage_ready[0] = stage_ready[1] = false;
            stage_fill_index = stage_write_index = 0U;
            bytes_staged = bytes_written = 0UL;
            if (!sdcard_file_seek(0UL) ||
                !emit_header(mz_tape_decoder_get_header()))
            {
                fail_close_P(PSTR("MZF SEEK"));
                return;
            }
            duplicate_recovery_pending = false;
            duplicate_data_active = true;
            mz_tape_decoder_start_recovery_data(expected_data_bytes);
        }
        if (!write_one_stage() || (!block_valid && !pump_fifo()))
        {
            fail_close_P((record_error_text[0] != '\0') ?
                         (PGM_P)NULL : PSTR("MZF DECODE"));
            return;
        }
    }
    if (record_state != MZF_RECORD_ENGINE_FINALIZING) return;
    if (!block_valid)
    {
        if (user_stop_requested &&
            (edge_record_driver_available_bytes() == 0U) &&
            (parse_state == MZF_PARSE_NORMAL) &&
            (pending_short_units == 0U))
        {
            fail_close_P(PSTR("MZF INCOMPLETE"));
        }
        return;
    }
    mark_tail_ready();
    if (!write_one_stage())
    {
        fail_close_P(PSTR("MZF WRITE"));
        return;
    }
    if (stage_ready[0] || stage_ready[1] ||
        (stage_count[0] != 0U) || (stage_count[1] != 0U) ||
        sdcard_file_is_busy()) return;
    if ((bytes_staged != expected_file_bytes) ||
        (bytes_written != expected_file_bytes) ||
        !sdcard_file_truncate(expected_file_bytes) || !sdcard_file_sync())
    {
        fail_close_P(PSTR("MZF CLOSE"));
        return;
    }
    sdcard_file_close();
    mz_tape_decoder_stop();
    /* Sidecar failure is intentionally nonfatal: the canonical MZF is already
       closed and valid. O_TRUNC replaces an existing RECxxxx.MZI. */
    (void)mzi_sidecar_write_for_mzf(record_path_buffer, mzi_profile);
    record_state = MZF_RECORD_ENGINE_FINISHED;
}

mzf_record_engine_state_t mzf_record_engine_get_state(void)
{
    return record_state;
}

const char *mzf_record_engine_get_filename(void)
{
    return record_path_filename();
}

const char *mzf_record_engine_get_error_text(void)
{
    return record_error_text;
}

uint8_t mzf_record_engine_get_buffer_headroom_percent(void)
{
    const uint32_t capacity = (uint32_t)(WAV_SAMPLE_STREAM_BUFFER_BYTES - 1U) +
                              (2UL * MZF_STAGE_BYTES);
    uint32_t used = edge_record_driver_available_bytes() +
                    (uint32_t)stage_count[0] + stage_count[1];
    if (used > capacity) used = capacity;
    return (uint8_t)(100UL - ((used * 100UL) / capacity));
}

bool mzf_record_engine_get_detected_loader_mode(loader_mode_t *loader_mode)
{
    return header_valid &&
           mzi_record_profile_get_loader_mode(mzi_profile, loader_mode);
}

bool mzf_record_engine_get_payload_progress_percent(uint8_t *percent)
{
    uint32_t payload_staged;
    uint32_t value;

    if (!header_valid || (percent == NULL)) return false;
    if (expected_data_bytes == 0UL)
    {
        *percent = 100U;
        return true;
    }
    payload_staged = (bytes_staged > MZ_TAPE_HEADER_BYTES) ?
        (bytes_staged - MZ_TAPE_HEADER_BYTES) : 0UL;
    if (payload_staged > expected_data_bytes)
        payload_staged = expected_data_bytes;
    value = (payload_staged * 100UL) / expected_data_bytes;
    *percent = (uint8_t)value;
    return true;
}
