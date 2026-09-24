#include "edge_record_engine.h"

#include <stdio.h>
#include <string.h>

#include "edge_record_driver.h"
#include "../drivers/sdcard.h"
#include "../streams/wav_sample_stream.h"
#include "../streams/cmt_mode_scratch.h"
#include "../streams/cmt_error_buffers.h"
#include "../drivers/flash_text.h"
#include "record_path_buffer.h"
#include "record_autoname.h"

#define EDGE_RECORD_STAGE_BYTES 512U
#define EDGE_RECORD_FINAL_PREALLOCATE_BYTES (2UL * 1024UL * 1024UL)

#define edge_stage0_buffer wav_sample_stream_get_shared_work_buffer()
#define edge_stage1_buffer cmt_mode_scratch.edge_record_stage_bytes

typedef enum
{
    EDGE_PARSE_NORMAL = 0,
    EDGE_PARSE_UNIT,
    EDGE_PARSE_LONG_TAIL
} edge_parse_state_t;

typedef enum
{
    EDGE_PATCH_DONE = 0,
    EDGE_PATCH_WAIT,
    EDGE_PATCH_ERROR
} edge_patch_result_t;

static edge_record_engine_state_t record_state = EDGE_RECORD_ENGINE_STOPPED;
static file_format_t record_format = FILE_FORMAT_UNKNOWN;
#define record_error_text cmt_record_backend_error

/* Two 512-byte standard-output sectors. Stage 1 reuses browser-only RAM. */
static uint16_t stage_count[2];
static bool stage_ready[2];
static uint8_t stage_fill_index = 0U;
static uint8_t stage_write_index = 0U;
static uint32_t stage_base_offset[2];
static uint32_t final_bytes_written = 0UL;
/* Logical output position, including staged bytes not yet transmitted to SD. */
static uint32_t output_bytes_emitted = 0UL;

/* Driver token parser. */
static edge_parse_state_t parse_state = EDGE_PARSE_NORMAL;
static uint8_t parse_pending_short_units = 0U;

/* Short 1..127-unit normal output interval. */
static uint8_t output_level = 0U;
static bool output_interval_active = false;
static uint8_t output_interval_units = 0U;
static uint8_t output_interval_level = 0U;

/*
   One long physical level is encoded progressively. The first 127-unit block
   remains held. On the second and every subsequent block a zero extension is
   guaranteed, so it can be written immediately. The first signed byte starts
   as +/-1 and is patched to the final remainder at the trailing edge.
*/
static bool output_long_active = false;
static uint8_t output_long_level = 0U;
static bool output_long_placeholder_emitted = false;
static uint32_t output_long_placeholder_offset = 0UL;
static bool output_long_start_pending = false;
static bool output_long_zero_pending = false;
static bool output_long_tail_pending = false;
static int8_t output_long_tail_units = 0;
static bool output_long_close_after_zero = false;

static uint8_t *edge_stage_buffer(uint8_t index)
{
    return (index == 0U) ? edge_stage0_buffer : edge_stage1_buffer;
}

static void edge_record_set_error(const char *text)
{
    if (text == NULL)
    {
        flash_text_copy(record_error_text, sizeof(record_error_text), PSTR("EDGE ERROR"));
        return;
    }
    strncpy(record_error_text, text, sizeof(record_error_text) - 1U);
    record_error_text[sizeof(record_error_text) - 1U] = '\0';
}

static void edge_record_set_error_P(PGM_P text)
{
    flash_text_copy(record_error_text, sizeof(record_error_text), text);
}

static bool edge_record_make_paths(const char *directory_path)
{
    uint16_t sequence;
    int length;

    if (!sdcard_next_record_sequence(directory_path, &sequence))
    {
        edge_record_set_error(sdcard_last_error());
        return false;
    }

    length = (record_format == FILE_FORMAT_L16) ?
        flash_text_snprintf(record_path_buffer, sizeof(record_path_buffer),
                            PSTR("%s/REC%04u.L16"), directory_path,
                            (unsigned int)sequence) :
        flash_text_snprintf(record_path_buffer, sizeof(record_path_buffer),
                            PSTR("%s/REC%04u.LEP"), directory_path,
                            (unsigned int)sequence);

    if ((length <= 0) || ((size_t)length >= sizeof(record_path_buffer)))
    {
        edge_record_set_error_P(PSTR("PATH TOO LONG"));
        return false;
    }

    return true;
}

/* Submit only one completed sector. SD writes never run in an ISR. */
static bool edge_record_write_one_ready_stage(void)
{
    uint8_t index = stage_write_index;
    uint16_t count;

    if (!stage_ready[index])
    {
        return true;
    }
    if (sdcard_file_is_busy())
    {
        return true;
    }

    count = stage_count[index];
    if ((count == 0U) || (count > EDGE_RECORD_STAGE_BYTES))
    {
        edge_record_set_error_P(PSTR("STAGE ERROR"));
        return false;
    }

    if (sdcard_file_write(edge_stage_buffer(index), count) != (int16_t)count)
    {
        edge_record_set_error_P(PSTR("EDGE WRITE"));
        return false;
    }

    final_bytes_written += (uint32_t)count;
    stage_count[index] = 0U;
    stage_ready[index] = false;
    stage_write_index ^= 1U;
    return true;
}

static bool edge_record_emit_byte(uint8_t value)
{
    uint8_t index = stage_fill_index;

    if (stage_ready[index])
    {
        return false;
    }

    if (stage_count[index] == 0U)
    {
        stage_base_offset[index] = output_bytes_emitted;
    }

    edge_stage_buffer(index)[stage_count[index]++] = value;
    output_bytes_emitted++;

    if (stage_count[index] == EDGE_RECORD_STAGE_BYTES)
    {
        stage_ready[index] = true;
        stage_fill_index ^= 1U;
    }
    return true;
}

static uint8_t edge_record_signed_slot(uint8_t level, uint8_t units)
{
    return level ? units : (uint8_t)(-(int16_t)units);
}

/* Start output of one short 1..127-unit level. */
static bool edge_record_begin_interval(uint8_t units)
{
    if ((units == 0U) || (units > 127U))
    {
        edge_record_set_error_P(PSTR("BAD TOKEN"));
        return false;
    }

    output_interval_units = units;
    output_interval_level = output_level;
    record_autoname_feed_level_interval(units, output_interval_level);
    output_level ^= 1U;
    output_interval_active = true;
    return true;
}

static bool edge_record_emit_active_interval(void)
{
    if (!output_interval_active)
    {
        return true;
    }

    if (!edge_record_emit_byte(edge_record_signed_slot(output_interval_level,
                                                        output_interval_units)))
    {
        return true;
    }

    output_interval_active = false;
    return true;
}

static bool edge_record_long_has_pending_output(void)
{
    return output_long_start_pending || output_long_zero_pending ||
           output_long_tail_pending;
}

static void edge_record_finish_long_interval(void)
{
    output_level ^= 1U;
    output_long_active = false;
    output_long_level = 0U;
    output_long_placeholder_emitted = false;
    output_long_placeholder_offset = 0UL;
    output_long_start_pending = false;
    output_long_zero_pending = false;
    output_long_tail_pending = false;
    output_long_tail_units = 0;
    output_long_close_after_zero = false;
}

/*
   Patch a placeholder in RAM when it has not been written yet. Otherwise do a
   single foreground seek/write/seek transaction, restoring the append point.
*/
static edge_patch_result_t edge_record_patch_output_byte(uint32_t offset,
                                                         uint8_t value)
{
    uint8_t index;

    for (index = 0U; index < 2U; ++index)
    {
        uint32_t base = stage_base_offset[index];
        uint32_t end = base + (uint32_t)stage_count[index];

        if ((stage_count[index] != 0U) && (offset >= base) && (offset < end))
        {
            edge_stage_buffer(index)[(uint16_t)(offset - base)] = value;
            return EDGE_PATCH_DONE;
        }
    }

    if (offset >= final_bytes_written)
    {
        edge_record_set_error_P(PSTR("PATCH LOST"));
        return EDGE_PATCH_ERROR;
    }

    if (sdcard_file_is_busy())
    {
        return EDGE_PATCH_WAIT;
    }

    if (!sdcard_file_seek(offset) ||
        (sdcard_file_write(&value, 1U) != 1) ||
        !sdcard_file_seek(final_bytes_written))
    {
        edge_record_set_error_P(PSTR("PATCH WRITE"));
        return EDGE_PATCH_ERROR;
    }

    return EDGE_PATCH_DONE;
}

/* Called when one exact 127-unit CTC block token is consumed. */
static bool edge_record_accept_long_block(void)
{
    if (!output_long_active)
    {
        record_autoname_break_signal();
        output_long_active = true;
        output_long_level = output_level;
        output_long_placeholder_emitted = false;
        output_long_start_pending = false;
        output_long_zero_pending = false;
        output_long_tail_pending = false;
        output_long_close_after_zero = false;
        return true;
    }

    /* The previous 127-unit block is now guaranteed to be a zero extension. */
    if (!output_long_placeholder_emitted)
    {
        output_long_start_pending = true;
    }
    else
    {
        output_long_zero_pending = true;
    }
    return true;
}

/* t belongs to -1..127 and completes the current long physical level. */
static bool edge_record_accept_long_tail(int8_t tail_units)
{
    if (!output_long_active || (tail_units < -1))
    {
        edge_record_set_error_P(PSTR("BAD TOKEN"));
        return false;
    }

    output_long_tail_units = tail_units;
    output_long_tail_pending = true;
    return true;
}

/*
   Emit any pending progressive long-level action. A false byte emission means
   that both staging sectors are temporarily occupied, so the caller yields
   until the normal SD service has released one; it is not an error.
*/
static bool edge_record_service_long_output(void)
{
    if (!output_long_active)
    {
        return true;
    }

    if (output_long_start_pending)
    {
        uint32_t offset = output_bytes_emitted;

        if (!edge_record_emit_byte(edge_record_signed_slot(output_long_level, 1U)))
        {
            return true;
        }

        output_long_placeholder_offset = offset;
        output_long_placeholder_emitted = true;
        output_long_start_pending = false;
        output_long_zero_pending = true;
        return true;
    }

    if (output_long_zero_pending)
    {
        if (!edge_record_emit_byte(0U))
        {
            return true;
        }

        output_long_zero_pending = false;
        if (output_long_close_after_zero)
        {
            edge_record_finish_long_interval();
        }
        return true;
    }

    if (output_long_tail_pending)
    {
        int8_t tail = output_long_tail_units;
        uint8_t remainder = (tail <= 0) ? (uint8_t)(127 + tail) :
            (uint8_t)tail;
        bool final_zero = (tail > 0);
        uint8_t slot = edge_record_signed_slot(output_long_level, remainder);

        if (output_long_placeholder_emitted)
        {
            edge_patch_result_t patch = edge_record_patch_output_byte(
                output_long_placeholder_offset, slot);

            if (patch == EDGE_PATCH_WAIT)
            {
                return true;
            }
            if (patch == EDGE_PATCH_ERROR)
            {
                return false;
            }
        }
        else if (!edge_record_emit_byte(slot))
        {
            return true;
        }

        output_long_tail_pending = false;
        if (final_zero)
        {
            output_long_zero_pending = true;
            output_long_close_after_zero = true;
            return true;
        }

        edge_record_finish_long_interval();
        return true;
    }

    return true;
}

/* Consume driver tokens and immediately form standard output bytes. */
static bool edge_record_pump_fifo_to_standard_output(void)
{
    while (!stage_ready[stage_fill_index])
    {
        uint8_t value;

        if (output_interval_active)
        {
            if (!edge_record_emit_active_interval())
            {
                return false;
            }
            if (output_interval_active)
            {
                return true;
            }
            continue;
        }

        if (edge_record_long_has_pending_output())
        {
            if (!edge_record_service_long_output())
            {
                return false;
            }
            if (edge_record_long_has_pending_output())
            {
                return true;
            }
            continue;
        }

        if (parse_pending_short_units != 0U)
        {
            uint8_t units = parse_pending_short_units;
            parse_pending_short_units = 0U;
            if (!edge_record_begin_interval(units))
            {
                return false;
            }
            continue;
        }

        if (!edge_record_driver_pop_byte(&value))
        {
            return true;
        }

        if (parse_state == EDGE_PARSE_UNIT)
        {
            parse_state = EDGE_PARSE_NORMAL;
            if ((value < 16U) || (value > 127U) ||
                !edge_record_begin_interval(value))
            {
                edge_record_set_error_P(PSTR("BAD TOKEN"));
                return false;
            }
            continue;
        }

        if (parse_state == EDGE_PARSE_LONG_TAIL)
        {
            parse_state = EDGE_PARSE_NORMAL;
            if (!edge_record_accept_long_tail((int8_t)value))
            {
                return false;
            }
            continue;
        }

        if (value == EDGE_RECORD_TOKEN_UNIT)
        {
            parse_state = EDGE_PARSE_UNIT;
            continue;
        }

        if (value == EDGE_RECORD_TOKEN_LONG_BLOCK)
        {
            if (!edge_record_accept_long_block())
            {
                return false;
            }
            continue;
        }

        if (value == EDGE_RECORD_TOKEN_LONG_TAIL)
        {
            parse_state = EDGE_PARSE_LONG_TAIL;
            continue;
        }

        {
            uint8_t first = (uint8_t)(value >> 4);
            uint8_t second = (uint8_t)(value & 0x0FU);

            if (first == 0U)
            {
                edge_record_set_error_P(PSTR("BAD TOKEN"));
                return false;
            }

            if (!edge_record_begin_interval(first))
            {
                return false;
            }
            if (second != 0U)
            {
                parse_pending_short_units = second;
            }
        }
    }

    return true;
}

static bool edge_record_output_drained(void)
{
    return (edge_record_driver_available_bytes() == 0U) &&
           (parse_state == EDGE_PARSE_NORMAL) &&
           (parse_pending_short_units == 0U) &&
           !output_interval_active && !output_long_active &&
           !edge_record_long_has_pending_output();
}

static void edge_record_mark_output_tail_ready(void)
{
    uint8_t index = stage_fill_index;

    if ((stage_count[index] != 0U) && !stage_ready[index])
    {
        stage_ready[index] = true;
        stage_fill_index ^= 1U;
    }
}

static void edge_record_fail_close(const char *text)
{
    /* Existing engine errors already live in this buffer; do not copy an
       overlapping C string onto itself. */
    if ((text != NULL) && (text != record_error_text))
    {
        edge_record_set_error(text);
    }

    edge_record_driver_abort();
    sdcard_file_close();
    if (record_path_buffer[0] != '\0')
    {
        (void)sdcard_file_remove(record_path_buffer);
    }
    record_state = EDGE_RECORD_ENGINE_ERROR;
}

static void edge_record_fail_close_P(PGM_P text)
{
    if (text != NULL)
    {
        edge_record_set_error_P(text);
    }

    edge_record_driver_abort();
    sdcard_file_close();
    if (record_path_buffer[0] != '\0')
    {
        (void)sdcard_file_remove(record_path_buffer);
    }
    record_state = EDGE_RECORD_ENGINE_ERROR;
}

void edge_record_engine_init(void)
{
    edge_record_driver_init();
    record_state = EDGE_RECORD_ENGINE_STOPPED;
    record_format = FILE_FORMAT_UNKNOWN;
    record_path_buffer[0] = '\0';
    record_error_text[0] = '\0';
    stage_count[0] = 0U;
    stage_count[1] = 0U;
    stage_ready[0] = false;
    stage_ready[1] = false;
    stage_fill_index = 0U;
    stage_write_index = 0U;
    stage_base_offset[0] = 0UL;
    stage_base_offset[1] = 0UL;
    final_bytes_written = 0UL;
    output_bytes_emitted = 0UL;
    parse_state = EDGE_PARSE_NORMAL;
    parse_pending_short_units = 0U;
    output_level = 0U;
    output_interval_active = false;
    output_interval_units = 0U;
    output_interval_level = 0U;
    output_long_active = false;
    output_long_level = 0U;
    output_long_placeholder_emitted = false;
    output_long_placeholder_offset = 0UL;
    output_long_start_pending = false;
    output_long_zero_pending = false;
    output_long_tail_pending = false;
    output_long_tail_units = 0;
    output_long_close_after_zero = false;
}

bool edge_record_engine_preview_filename(const char *directory_path,
                                         file_format_t format)
{
    if ((format != FILE_FORMAT_LEP) && (format != FILE_FORMAT_L16))
    {
        edge_record_set_error_P(PSTR("REC FORMAT"));
        return false;
    }

    record_format = format;
    return edge_record_make_paths(directory_path);
}

bool edge_record_engine_start(const char *directory_path, file_format_t format)
{
    uint8_t unit_us;

    edge_record_engine_init();

    if ((format != FILE_FORMAT_LEP) && (format != FILE_FORMAT_L16))
    {
        edge_record_set_error_P(PSTR("EDGE FORMAT"));
        record_state = EDGE_RECORD_ENGINE_ERROR;
        return false;
    }
    if (!sdcard_is_mounted())
    {
        edge_record_set_error_P(PSTR("SD CARD ERROR"));
        record_state = EDGE_RECORD_ENGINE_ERROR;
        return false;
    }

    record_format = format;
    unit_us = (format == FILE_FORMAT_L16) ? 16U : 50U;

    if (!edge_record_driver_prepare(unit_us) ||
        !edge_record_make_paths(directory_path))
    {
        record_state = EDGE_RECORD_ENGINE_ERROR;
        return false;
    }
    if (!sdcard_file_open_write(record_path_buffer))
    {
        edge_record_set_error_P(PSTR("EDGE CREATE"));
        record_state = EDGE_RECORD_ENGINE_ERROR;
        return false;
    }

    /* Preallocate the final standard file before PCINT capture begins. */
    if (!sdcard_file_preallocate(EDGE_RECORD_FINAL_PREALLOCATE_BYTES) ||
        !sdcard_file_seek(0UL) || !sdcard_file_sync())
    {
        edge_record_fail_close_P(PSTR("PREALLOC FAIL"));
        return false;
    }

    if (!edge_record_driver_start())
    {
        edge_record_fail_close_P(PSTR("EDGE START"));
        return false;
    }

    output_level = edge_record_driver_get_initial_level();
    record_state = EDGE_RECORD_ENGINE_RECORDING;
    return true;
}

bool edge_record_engine_pause(void)
{
    if ((record_state == EDGE_RECORD_ENGINE_RECORDING) &&
        edge_record_driver_pause())
    {
        record_state = EDGE_RECORD_ENGINE_PAUSED;
        return true;
    }
    return false;
}

bool edge_record_engine_resume(void)
{
    if ((record_state == EDGE_RECORD_ENGINE_PAUSED) &&
        edge_record_driver_resume())
    {
        record_state = EDGE_RECORD_ENGINE_RECORDING;
        return true;
    }
    return false;
}

void edge_record_engine_request_stop(void)
{
    if ((record_state != EDGE_RECORD_ENGINE_RECORDING) &&
        (record_state != EDGE_RECORD_ENGINE_PAUSED))
    {
        return;
    }

    edge_record_driver_stop();
    if (edge_record_driver_get_state() == EDGE_RECORD_DRIVER_OVERRUN)
    {
        edge_record_fail_close_P(PSTR("EDGE OVERFLOW"));
        return;
    }
    record_state = EDGE_RECORD_ENGINE_FINALIZING;
}

void edge_record_engine_cancel(void)
{
    edge_record_driver_abort();
    sdcard_file_close();
    if (record_path_buffer[0] != '\0')
    {
        (void)sdcard_file_remove(record_path_buffer);
    }
    record_state = EDGE_RECORD_ENGINE_STOPPED;
}

void edge_record_engine_service(void)
{
    edge_record_driver_state_t driver_state = edge_record_driver_get_state();

    if ((record_state == EDGE_RECORD_ENGINE_RECORDING) ||
        (record_state == EDGE_RECORD_ENGINE_PAUSED))
    {
        if (driver_state == EDGE_RECORD_DRIVER_OVERRUN)
        {
            edge_record_fail_close_P(PSTR("EDGE OVERFLOW"));
            return;
        }
        if (!edge_record_write_one_ready_stage() ||
            !edge_record_pump_fifo_to_standard_output())
        {
            edge_record_fail_close(record_error_text);
        }
        return;
    }

    if (record_state != EDGE_RECORD_ENGINE_FINALIZING)
    {
        return;
    }

    if (!edge_record_write_one_ready_stage() ||
        !edge_record_pump_fifo_to_standard_output())
    {
        edge_record_fail_close(record_error_text);
        return;
    }

    if (!edge_record_output_drained())
    {
        return;
    }

    edge_record_mark_output_tail_ready();
    if (!edge_record_write_one_ready_stage())
    {
        edge_record_fail_close(record_error_text);
        return;
    }

    if (stage_ready[0] || stage_ready[1] ||
        (stage_count[0] != 0U) || (stage_count[1] != 0U) ||
        sdcard_file_is_busy())
    {
        return;
    }

    if ((output_bytes_emitted != final_bytes_written) ||
        !sdcard_file_truncate(final_bytes_written) || !sdcard_file_sync())
    {
        edge_record_fail_close_P(PSTR("EDGE CLOSE"));
        return;
    }

    sdcard_file_close();
    record_state = EDGE_RECORD_ENGINE_FINISHED;
}

edge_record_engine_state_t edge_record_engine_get_state(void)
{
    return record_state;
}

const char *edge_record_engine_get_filename(void)
{
    return record_path_filename();
}

const char *edge_record_engine_get_full_path(void)
{
    return record_path_buffer;
}

const char *edge_record_engine_get_error_text(void)
{
    return record_error_text;
}

uint8_t edge_record_engine_get_buffer_fill_percent(void)
{
    return edge_record_driver_fill_percent();
}

uint8_t edge_record_engine_get_buffer_headroom_percent(void)
{
    const uint32_t capacity = (uint32_t)(WAV_SAMPLE_STREAM_BUFFER_BYTES - 1U) +
                              (2UL * EDGE_RECORD_STAGE_BYTES);
    uint32_t used = (uint32_t)edge_record_driver_available_bytes() +
                    (uint32_t)stage_count[0] + (uint32_t)stage_count[1];

    if (used > capacity)
    {
        used = capacity;
    }
    return (uint8_t)(((capacity - used) * 100UL) / capacity);
}
