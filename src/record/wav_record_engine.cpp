#include "wav_record_engine.h"

#include <stdio.h>
#include <string.h>

#include "../drivers/sdcard.h"
#include "../drivers/wav_record_driver.h"
#include "../streams/record_sample_stream.h"
#include "../streams/wav_sample_stream.h"
#include "../streams/cmt_error_buffers.h"
#include "../streams/cmt_mode_scratch.h"
#include "../drivers/flash_text.h"
#include "record_path_buffer.h"
#include "record_autoname.h"

/* 64 packed bytes expand exactly to one 512-byte PCM sector. */
#define WAV_RECORD_RAW_BLOCK_BYTES 64U
#define WAV_RECORD_PCM_BLOCK_BYTES 512U

/*
    Experimental WAV AutoName queue.

    WAV RECORD does not use the second 512-byte LEP/L16/MZF staging sector, so
    split that idle scratch area into eight chronological 64-byte packed-sample
    slots.  A captured block is copied here before its working buffer is
    expanded to PCM and written to SD.  AutoName then consumes only a few
    packed bytes per foreground service pass.

    This deliberately preserves every AutoName byte.  If analysis cannot keep
    up on average, the queue eventually fills and normal FIFO back-pressure is
    allowed to occur; no detector is disabled and no analysis is dropped.  That
    makes this change a clean test of whether decoupling SD writes from analysis
    alone removes the RECORD overflow.
*/
#define WAV_RECORD_ANALYSIS_SLOT_BYTES WAV_RECORD_RAW_BLOCK_BYTES
#define WAV_RECORD_ANALYSIS_SLOT_COUNT 8U
#define WAV_RECORD_ANALYSIS_SLOT_MASK  7U
#define WAV_RECORD_AUTONAME_BYTES_PER_SERVICE 4U
#define WAV_RECORD_ANALYSIS_PARTIAL_NONE 0xFFU

static_assert((WAV_RECORD_ANALYSIS_SLOT_COUNT &
               (WAV_RECORD_ANALYSIS_SLOT_COUNT - 1U)) == 0U,
              "WAV analysis slot count must be a power of two");
static_assert(sizeof(cmt_mode_scratch.edge_record_stage_bytes) >=
                  (WAV_RECORD_ANALYSIS_SLOT_COUNT *
                   WAV_RECORD_ANALYSIS_SLOT_BYTES),
              "WAV analysis queue does not fit CMT scratch buffer");

/* RIFF + fmt + JUNK + data header. Data begins aligned at byte 512. */
#define WAV_RECORD_HEADER_BYTES 512U
#define WAV_RECORD_JUNK_PAYLOAD_BYTES 460U

/*
    2 MiB = about 47.6 s at 44.1 kHz, 8-bit mono. Reservation happens before
    Timer1 starts, so the filesystem never needs to find a cluster mid-record.
*/
#define WAV_RECORD_PREALLOCATE_PCM_BYTES (2UL * 1024UL * 1024UL)

static wav_record_engine_state_t record_state = WAV_RECORD_ENGINE_STOPPED;

#define record_error_text cmt_record_backend_error

static uint32_t record_sample_rate = 0UL;
static uint32_t data_bytes_written = 0UL;
static uint8_t final_tail_byte = 0U;
static uint8_t final_tail_bits = 0U;
static bool final_tail_taken = false;

/*
    AUTO WAV activity tracking.  The realtime Timer1 ISR already samples WRITE
    at the WAV sample rate, so AUTO must not add a second pin-change interrupt
    on the same signal.  Foreground code observes the packed bytes which are
    already being removed from the realtime FIFO.  Tracking is enabled only
    for AUTO. Consecutive packed bytes without a WRITE transition form the
    idle counter used by the five-second auto-stop.
*/
static bool activity_tracking_enabled = false;
static uint16_t activity_idle_packed_bytes = 0U;
static uint8_t activity_last_level = 0U;
static bool activity_level_valid = false;

#define wav_record_work_block wav_sample_stream_get_shared_work_buffer()
#define wav_record_analysis_storage cmt_mode_scratch.edge_record_stage_bytes

/* Queue metadata costs only six bytes of dedicated SRAM.  All 512 data bytes
   live in the mode-shared scratch buffer.  Every slot is 64 bytes except the
   optional final partial slot created after Timer1 has stopped. */
static uint8_t analysis_read_slot = 0U;
static uint8_t analysis_write_slot = 0U;
static uint8_t analysis_queued_slots = 0U;
static uint8_t analysis_byte_offset = 0U;
static uint8_t analysis_partial_slot = WAV_RECORD_ANALYSIS_PARTIAL_NONE;
static uint8_t analysis_partial_length = 0U;

static void wav_record_analysis_reset(void)
{
    analysis_read_slot = 0U;
    analysis_write_slot = 0U;
    analysis_queued_slots = 0U;
    analysis_byte_offset = 0U;
    analysis_partial_slot = WAV_RECORD_ANALYSIS_PARTIAL_NONE;
    analysis_partial_length = 0U;
}

static void wav_record_activity_reset(void)
{
    activity_idle_packed_bytes = 0U;
    activity_last_level = 0U;
    activity_level_valid = false;
}

static void wav_record_activity_observe(const uint8_t *source, uint8_t count)
{
    uint8_t previous = activity_last_level;
    bool previous_valid = activity_level_valid;
    bool transition = false;

    if ((source == NULL) || (count == 0U)) return;

    for (uint8_t i = 0U; i < count; ++i)
    {
        uint8_t packed = source[i];
        uint8_t first = (uint8_t)(packed & 1U);

        if ((previous_valid && (first != previous)) ||
            ((packed != 0x00U) && (packed != 0xFFU)))
        {
            transition = true;
            break;
        }

        previous = (uint8_t)((packed >> 7U) & 1U);
        previous_valid = true;
    }

    /* We need only the final level for the next block boundary.  Once an edge
       is found there is no reason to scan the rest of this active block. */
    activity_last_level =
        (uint8_t)((source[(uint8_t)(count - 1U)] >> 7U) & 1U);
    activity_level_valid = true;
    if (transition)
    {
        activity_idle_packed_bytes = 0U;
    }
    else
    {
        uint16_t room = (uint16_t)(0xFFFFU - activity_idle_packed_bytes);
        activity_idle_packed_bytes =
            ((uint16_t)count > room) ? 0xFFFFU :
            (uint16_t)(activity_idle_packed_bytes + count);
    }
}

static uint8_t *wav_record_analysis_slot_ptr(uint8_t slot)
{
    return &wav_record_analysis_storage[
        (uint16_t)slot * WAV_RECORD_ANALYSIS_SLOT_BYTES
    ];
}

static uint8_t wav_record_analysis_slot_length(uint8_t slot)
{
    if (slot == analysis_partial_slot)
    {
        return analysis_partial_length;
    }
    return WAV_RECORD_ANALYSIS_SLOT_BYTES;
}

static bool wav_record_analysis_queue_block(const uint8_t *source,
                                            uint8_t count)
{
    uint8_t slot;

    if ((source == NULL) || (count == 0U) ||
        (count > WAV_RECORD_ANALYSIS_SLOT_BYTES) ||
        (analysis_queued_slots >= WAV_RECORD_ANALYSIS_SLOT_COUNT))
    {
        return false;
    }

    slot = analysis_write_slot;
    memcpy(wav_record_analysis_slot_ptr(slot), source, count);

    if (count < WAV_RECORD_ANALYSIS_SLOT_BYTES)
    {
        /* A partial packed block can occur only after capture has stopped. */
        analysis_partial_slot = slot;
        analysis_partial_length = count;
    }

    analysis_write_slot =
        (uint8_t)((analysis_write_slot + 1U) & WAV_RECORD_ANALYSIS_SLOT_MASK);
    analysis_queued_slots++;
    return true;
}

static void wav_record_analysis_service(uint8_t byte_budget)
{
    while ((byte_budget != 0U) && (analysis_queued_slots != 0U))
    {
        uint8_t slot_length =
            wav_record_analysis_slot_length(analysis_read_slot);
        uint8_t *slot = wav_record_analysis_slot_ptr(analysis_read_slot);

        if ((slot_length == 0U) ||
            (analysis_byte_offset >= slot_length))
        {
            /* Defensive recovery; valid queue state never enters here. */
            analysis_byte_offset = 0U;
            if (analysis_read_slot == analysis_partial_slot)
            {
                analysis_partial_slot = WAV_RECORD_ANALYSIS_PARTIAL_NONE;
                analysis_partial_length = 0U;
            }
            analysis_read_slot =
                (uint8_t)((analysis_read_slot + 1U) &
                          WAV_RECORD_ANALYSIS_SLOT_MASK);
            analysis_queued_slots--;
            continue;
        }

        record_autoname_feed_packed_samples(slot[analysis_byte_offset], 8U);
        analysis_byte_offset++;
        byte_budget--;

        if (analysis_byte_offset >= slot_length)
        {
            analysis_byte_offset = 0U;
            if (analysis_read_slot == analysis_partial_slot)
            {
                analysis_partial_slot = WAV_RECORD_ANALYSIS_PARTIAL_NONE;
                analysis_partial_length = 0U;
            }
            analysis_read_slot =
                (uint8_t)((analysis_read_slot + 1U) &
                          WAV_RECORD_ANALYSIS_SLOT_MASK);
            analysis_queued_slots--;
        }
    }
}

static void wav_record_set_error(const char *text)
{
    if (text == NULL)
    {
        record_error_text[0] = '\0';
        return;
    }

    strncpy(record_error_text, text, sizeof(record_error_text) - 1U);
    record_error_text[sizeof(record_error_text) - 1U] = '\0';
}

static void wav_record_set_error_P(PGM_P text)
{
    flash_text_copy(record_error_text, sizeof(record_error_text), text);
}

static void wav_record_put_le16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value & 0xFFU);
    target[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void wav_record_put_le32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value & 0xFFUL);
    target[1] = (uint8_t)((value >> 8) & 0xFFUL);
    target[2] = (uint8_t)((value >> 16) & 0xFFUL);
    target[3] = (uint8_t)((value >> 24) & 0xFFUL);
}

static bool wav_record_make_paths(const char *directory_path)
{
    uint16_t sequence;
    int length;

    if (!sdcard_next_record_sequence(directory_path, &sequence))
    {
        wav_record_set_error(sdcard_last_error());
        return false;
    }

    length = flash_text_snprintf(record_path_buffer, sizeof(record_path_buffer),
                                 PSTR("%s/REC%04u.WAV"), directory_path,
                                 (unsigned int)sequence);

    if ((length <= 0) || ((size_t)length >= sizeof(record_path_buffer)))
    {
        wav_record_set_error_P(PSTR("PATH TOO LONG"));
        return false;
    }

    return true;
}

static void wav_record_stop_close_error(const char *text)
{
    /* Existing engine errors already live in this buffer; do not copy an
       overlapping C string onto itself. */
    if ((text != NULL) && (text != record_error_text))
    {
        wav_record_set_error(text);
    }

    wav_record_driver_stop();
    sdcard_file_close();
    record_state = WAV_RECORD_ENGINE_ERROR;
}

static void wav_record_stop_close_error_P(PGM_P text)
{
    if (text != NULL)
    {
        wav_record_set_error_P(text);
    }

    wav_record_driver_stop();
    sdcard_file_close();
    record_state = WAV_RECORD_ENGINE_ERROR;
}

static bool wav_record_write_header(uint32_t data_bytes)
{
    uint32_t padded_data_bytes = data_bytes + (data_bytes & 1UL);
    uint32_t file_bytes = WAV_RECORD_HEADER_BYTES + padded_data_bytes;

    if (file_bytes < WAV_RECORD_HEADER_BYTES)
    {
        wav_record_set_error_P(PSTR("WAV TOO BIG"));
        return false;
    }

    memset(wav_record_work_block, 0, WAV_RECORD_HEADER_BYTES);

    wav_record_work_block[0U] = 'R';
    wav_record_work_block[1U] = 'I';
    wav_record_work_block[2U] = 'F';
    wav_record_work_block[3U] = 'F';
    wav_record_put_le32(wav_record_work_block + 4U, file_bytes - 8UL);
    wav_record_work_block[8U] = 'W';
    wav_record_work_block[9U] = 'A';
    wav_record_work_block[10U] = 'V';
    wav_record_work_block[11U] = 'E';

    wav_record_work_block[12U] = 'f';
    wav_record_work_block[13U] = 'm';
    wav_record_work_block[14U] = 't';
    wav_record_work_block[15U] = ' ';
    wav_record_put_le32(wav_record_work_block + 16U, 16UL);
    wav_record_put_le16(wav_record_work_block + 20U, 1U);
    wav_record_put_le16(wav_record_work_block + 22U, 1U);
    wav_record_put_le32(wav_record_work_block + 24U, record_sample_rate);
    wav_record_put_le32(wav_record_work_block + 28U, record_sample_rate);
    wav_record_put_le16(wav_record_work_block + 32U, 1U);
    wav_record_put_le16(wav_record_work_block + 34U, 8U);

    /* JUNK pads the standard data header so audio starts at sector 512. */
    wav_record_work_block[36U] = 'J';
    wav_record_work_block[37U] = 'U';
    wav_record_work_block[38U] = 'N';
    wav_record_work_block[39U] = 'K';
    wav_record_put_le32(wav_record_work_block + 40U,
                        WAV_RECORD_JUNK_PAYLOAD_BYTES);
    wav_record_work_block[504U] = 'd';
    wav_record_work_block[505U] = 'a';
    wav_record_work_block[506U] = 't';
    wav_record_work_block[507U] = 'a';
    wav_record_put_le32(wav_record_work_block + 508U, data_bytes);

    if (!sdcard_file_seek(0UL) ||
        (sdcard_file_write(wav_record_work_block, WAV_RECORD_HEADER_BYTES) !=
         (int16_t)WAV_RECORD_HEADER_BYTES))
    {
        wav_record_set_error_P(PSTR("WAV HEADER ERR"));
        return false;
    }

    return true;
}

/*
    work[0..raw_count-1] contains packed samples.  The packed source has already
    been copied to the analysis queue, so this realtime-priority path only
    expands backwards in the 512-byte work block and writes PCM to SD.
*/
static bool wav_record_expand_and_write(uint16_t raw_count)
{
    uint16_t pcm_count;

    if ((raw_count == 0U) || (raw_count > WAV_RECORD_RAW_BLOCK_BYTES))
    {
        wav_record_set_error_P(PSTR("RAW BLOCK ERR"));
        return false;
    }

    /* Expand in place from the end so unread packed source bytes are never
       overwritten.  The eight assignments are deliberately unrolled: on the
       8-bit AVR this removes the inner loop, the variable 1<<bit operation and
       the per-byte input_index*8 calculation.  Bit order and PCM levels are
       exactly the same as the original conversion. */
    {
        uint8_t remaining = (uint8_t)raw_count;
        uint8_t *source = wav_record_work_block + remaining;
        uint8_t *destination =
            wav_record_work_block + ((uint16_t)remaining << 3U);

        while (remaining != 0U)
        {
            uint8_t packed = *--source;
            destination -= 8U;

            destination[0] = (packed & 0x01U) ? 235U : 20U;
            destination[1] = (packed & 0x02U) ? 235U : 20U;
            destination[2] = (packed & 0x04U) ? 235U : 20U;
            destination[3] = (packed & 0x08U) ? 235U : 20U;
            destination[4] = (packed & 0x10U) ? 235U : 20U;
            destination[5] = (packed & 0x20U) ? 235U : 20U;
            destination[6] = (packed & 0x40U) ? 235U : 20U;
            destination[7] = (packed & 0x80U) ? 235U : 20U;

            remaining--;
        }
    }

    pcm_count = (uint16_t)(raw_count << 3U);

    if (sdcard_file_write(wav_record_work_block, pcm_count) !=
        (int16_t)pcm_count)
    {
        wav_record_set_error_P(PSTR("WAV WRITE ERR"));
        return false;
    }

    data_bytes_written += (uint32_t)pcm_count;
    return true;
}

static bool wav_record_drain_one_block_if_ready(bool allow_partial)
{
    uint16_t available = record_sample_stream_available_bytes();
    uint16_t request;
    uint16_t got;

    if (available == 0U)
    {
        return true;
    }
    if (!allow_partial && (available < WAV_RECORD_RAW_BLOCK_BYTES))
    {
        return true;
    }

    /* Do not remove packed samples from the realtime FIFO unless their exact
       chronological copy can be retained for later AutoName analysis. */
    if (analysis_queued_slots >= WAV_RECORD_ANALYSIS_SLOT_COUNT)
    {
        return true;
    }

    if (sdcard_file_is_busy())
    {
        return true;
    }

    request = available;
    if (request > WAV_RECORD_RAW_BLOCK_BYTES)
    {
        request = WAV_RECORD_RAW_BLOCK_BYTES;
    }

    got = record_sample_stream_pop_bytes(wav_record_work_block, request);
    if (got == 0U)
    {
        return false;
    }

    /* AUTO idle detection reuses these already captured samples.  This keeps
       PCINT1 disabled during WAV RECORD and adds no work to the Timer1 ISR. */
    if (activity_tracking_enabled)
        wav_record_activity_observe(wav_record_work_block, (uint8_t)got);

    if (!wav_record_analysis_queue_block(wav_record_work_block, (uint8_t)got))
    {
        wav_record_set_error_P(PSTR("ANALYSIS QUEUE"));
        return false;
    }

    if (!wav_record_expand_and_write(got))
    {
        return false;
    }
    return true;
}

static bool wav_record_write_final_tail(void)
{
    if (final_tail_taken)
    {
        return true;
    }

    final_tail_taken = true;
    final_tail_byte = 0U;
    final_tail_bits = 0U;
    (void)wav_record_driver_take_tail(&final_tail_byte, &final_tail_bits);

    if (final_tail_bits == 0U)
    {
        return true;
    }

    /* All queued whole-byte samples are drained before this function runs, so
       feeding the sub-byte tail here preserves the original AutoName order. */
    record_autoname_feed_packed_samples(final_tail_byte, final_tail_bits);

    for (uint8_t bit = 0U; bit < final_tail_bits; bit++)
    {
        wav_record_work_block[bit] =
            (final_tail_byte & (uint8_t)(1U << bit)) ? 235U : 20U;
    }

    if (sdcard_file_write(wav_record_work_block, final_tail_bits) !=
        (int16_t)final_tail_bits)
    {
        wav_record_set_error_P(PSTR("TAIL WRITE ERR"));
        return false;
    }

    data_bytes_written += (uint32_t)final_tail_bits;
    return true;
}

static bool wav_record_finalize_file(void)
{
    uint32_t final_file_bytes = WAV_RECORD_HEADER_BYTES + data_bytes_written;

    if ((data_bytes_written & 1UL) != 0UL)
    {
        wav_record_work_block[0] = 0U;

        if (sdcard_file_write(wav_record_work_block, 1U) != 1)
        {
            wav_record_set_error_P(PSTR("WAV PAD ERR"));
            return false;
        }

        final_file_bytes++;
    }

    if (!wav_record_write_header(data_bytes_written) ||
        !sdcard_file_truncate(final_file_bytes) ||
        !sdcard_file_sync())
    {
        if (record_error_text[0] == '\0')
        {
            wav_record_set_error_P(PSTR("WAV CLOSE ERR"));
        }
        return false;
    }

    sdcard_file_close();
    record_state = WAV_RECORD_ENGINE_FINISHED;
    return true;
}

void wav_record_engine_init(void)
{
    wav_record_driver_init();

    record_state = WAV_RECORD_ENGINE_STOPPED;
    record_path_buffer[0] = '\0';
    record_error_text[0] = '\0';
    record_sample_rate = 0UL;
    data_bytes_written = 0UL;
    final_tail_byte = 0U;
    final_tail_bits = 0U;
    final_tail_taken = false;
    wav_record_analysis_reset();
    activity_tracking_enabled = false;
    wav_record_activity_reset();
}

bool wav_record_engine_preview_filename(const char *directory_path)
{
    /* This only scans for the next free name. It does not create/open a file. */
    return wav_record_make_paths(directory_path);
}

bool wav_record_engine_start(const char *directory_path,
                             uint32_t sample_rate,
                             bool track_auto_activity)
{
    wav_record_engine_init();
    activity_tracking_enabled = track_auto_activity;

    if (!sdcard_is_mounted())
    {
        wav_record_set_error_P(PSTR("SD CARD ERROR"));
        record_state = WAV_RECORD_ENGINE_ERROR;
        return false;
    }

    /* PLAY and RECORD cannot overlap. Reset old PLAY stream ownership. */
    wav_sample_stream_close();

    if (!wav_record_driver_prepare(sample_rate))
    {
        wav_record_set_error_P(PSTR("BAD REC RATE"));
        record_state = WAV_RECORD_ENGINE_ERROR;
        return false;
    }

    if (!wav_record_make_paths(directory_path))
    {
        record_state = WAV_RECORD_ENGINE_ERROR;
        return false;
    }

    record_sample_rate = sample_rate;

    if (!sdcard_file_open_write(record_path_buffer))
    {
        wav_record_set_error_P(PSTR("WAV CREATE ERR"));
        record_state = WAV_RECORD_ENGINE_ERROR;
        return false;
    }

    /*
        Contiguous allocation must complete before Timer1 starts. Starting a
        realtime direct-PCM record without it would allow a FAT allocation
        pause to overflow the 371 ms packed FIFO.
    */
    if (!sdcard_file_preallocate(WAV_RECORD_PREALLOCATE_PCM_BYTES))
    {
        wav_record_stop_close_error_P(PSTR("PREALLOC FAIL"));
        return false;
    }

    if (!sdcard_file_seek(0UL) ||
        !wav_record_write_header(0UL) ||
        !sdcard_file_seek(WAV_RECORD_HEADER_BYTES) ||
        !sdcard_file_sync())
    {
        wav_record_stop_close_error(record_error_text);
        return false;
    }

    if (!wav_record_driver_start())
    {
        wav_record_stop_close_error_P(PSTR("REC TIMER ERR"));
        return false;
    }

    record_state = WAV_RECORD_ENGINE_RECORDING;
    return true;
}

bool wav_record_engine_pause(void)
{
    if ((record_state == WAV_RECORD_ENGINE_RECORDING) && wav_record_driver_pause())
    {
        record_state = WAV_RECORD_ENGINE_PAUSED;
        return true;
    }
    return false;
}

bool wav_record_engine_resume(void)
{
    if ((record_state == WAV_RECORD_ENGINE_PAUSED) && wav_record_driver_resume())
    {
        record_state = WAV_RECORD_ENGINE_RECORDING;
        return true;
    }
    return false;
}

void wav_record_engine_cancel(void)
{
    wav_record_driver_stop();
    sdcard_file_close();
    if (record_path_buffer[0] != '\0')
    {
        (void)sdcard_file_remove(record_path_buffer);
    }
    record_state = WAV_RECORD_ENGINE_STOPPED;
    data_bytes_written = 0UL;
    final_tail_taken = false;
    wav_record_analysis_reset();
    wav_record_activity_reset();
}

void wav_record_engine_request_stop(void)
{
    if ((record_state != WAV_RECORD_ENGINE_RECORDING) &&
        (record_state != WAV_RECORD_ENGINE_PAUSED))
    {
        return;
    }

    wav_record_driver_stop();
    record_state = WAV_RECORD_ENGINE_FINALIZING;
}

void wav_record_engine_service(void)
{
    if ((record_state == WAV_RECORD_ENGINE_RECORDING) ||
        (record_state == WAV_RECORD_ENGINE_PAUSED))
    {
        if (wav_record_driver_get_state() == WAV_RECORD_DRIVER_OVERRUN)
        {
            wav_record_stop_close_error_P(PSTR("REC OVERFLOW"));
            return;
        }

        /* Recording priority: first move/write one complete PCM sector if
           possible, then spend only a small bounded amount of foreground time
           on AutoName.  While SD is busy the first step returns immediately,
           so the otherwise idle foreground time is naturally used by analysis. */
        if (!wav_record_drain_one_block_if_ready(false))
        {
            wav_record_stop_close_error(record_error_text);
            return;
        }

        wav_record_analysis_service(WAV_RECORD_AUTONAME_BYTES_PER_SERVICE);
        return;
    }

    if (record_state == WAV_RECORD_ENGINE_FINALIZING)
    {
        /* Capture is stopped, but preserve exact byte order for AutoName.
           Continue to write at most one WAV data block per service pass while
           also draining a bounded amount of the analysis queue. */
        if (record_sample_stream_available_bytes() != 0U)
        {
            if (!wav_record_drain_one_block_if_ready(true))
            {
                wav_record_stop_close_error(record_error_text);
                return;
            }
            wav_record_analysis_service(WAV_RECORD_AUTONAME_BYTES_PER_SERVICE);
            return;
        }

        /* The sub-byte Timer1 tail belongs after every queued whole byte. */
        if (analysis_queued_slots != 0U)
        {
            wav_record_analysis_service(WAV_RECORD_AUTONAME_BYTES_PER_SERVICE);
            return;
        }

        if (!final_tail_taken)
        {
            if (sdcard_file_is_busy())
            {
                return;
            }
            if (!wav_record_write_final_tail())
            {
                wav_record_stop_close_error(record_error_text);
            }
            return;
        }

        if (sdcard_file_is_busy())
        {
            return;
        }
        if (!wav_record_finalize_file())
        {
            wav_record_stop_close_error(record_error_text);
        }
    }
}

wav_record_engine_state_t wav_record_engine_get_state(void)
{
    return record_state;
}

const char *wav_record_engine_get_filename(void)
{
    return record_path_filename();
}

const char *wav_record_engine_get_full_path(void)
{
    return record_path_buffer;
}

const char *wav_record_engine_get_error_text(void)
{
    return record_error_text;
}

uint32_t wav_record_engine_get_sample_rate(void)
{
    return record_sample_rate;
}

uint16_t wav_record_engine_get_idle_packed_bytes(void)
{
    return activity_idle_packed_bytes;
}

void wav_record_engine_reset_idle_activity(void)
{
    wav_record_activity_reset();
}

uint32_t wav_record_engine_get_captured_samples(void)
{
    uint32_t samples = data_bytes_written;

    if ((record_state == WAV_RECORD_ENGINE_RECORDING) ||
        (record_state == WAV_RECORD_ENGINE_PAUSED))
    {
        samples += (uint32_t)record_sample_stream_available_bytes() * 8UL;
        samples += (uint32_t)wav_record_driver_get_pending_sample_count();
    }

    return samples;
}

uint8_t wav_record_engine_get_buffer_fill_percent(void)
{
    return record_sample_stream_fill_percent();
}

uint8_t wav_record_engine_get_buffer_headroom_percent(void)
{
    return (uint8_t)(100U - record_sample_stream_fill_percent());
}

uint8_t wav_record_engine_get_write_pin(void)
{
    return wav_record_driver_get_write_pin();
}
