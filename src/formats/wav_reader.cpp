#include "wav_reader.h"

#include <string.h>
#include <avr/pgmspace.h>
#include <avr/pgmspace.h>

#include "../drivers/sdcard.h"

#define WAV_FORMAT_PCM        0x0001U
#define WAV_FORMAT_EXTENSIBLE 0xFFFEU

static wav_reader_info_t wav_info;
static wav_reader_status_t wav_status = WAV_READER_STATUS_BAD_ARGUMENT;
static bool wav_open = false;

static uint16_t wav_read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
           ((uint16_t)bytes[1] << 8);
}

static uint32_t wav_read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bool wav_read_exact(void *buffer, uint16_t size)
{
    return sdcard_file_read(buffer, size) == (int16_t)size;
}

static bool wav_add_u32(uint32_t a, uint32_t b, uint32_t *result)
{
    if (result == NULL)
    {
        return false;
    }

    if (a > (0xFFFFFFFFUL - b))
    {
        return false;
    }

    *result = a + b;
    return true;
}

static bool wav_skip_to(uint32_t target_position)
{
    if (target_position > sdcard_file_size())
    {
        return false;
    }

    return sdcard_file_seek(target_position);
}

static bool wav_skip_chunk(uint32_t chunk_data_position,
                           uint32_t chunk_size,
                           uint32_t scan_end_position)
{
    uint32_t padded_size = chunk_size;
    uint32_t next_position;

    if (chunk_size & 1U)
    {
        if (padded_size == 0xFFFFFFFFUL)
        {
            return false;
        }

        padded_size++;
    }

    if (!wav_add_u32(chunk_data_position, padded_size, &next_position))
    {
        return false;
    }

    if (next_position > scan_end_position)
    {
        return false;
    }

    return wav_skip_to(next_position);
}

static bool wav_fourcc_equals(const uint8_t *value,
                              uint8_t c0, uint8_t c1,
                              uint8_t c2, uint8_t c3)
{
    return (value != NULL) &&
           (value[0] == c0) && (value[1] == c1) &&
           (value[2] == c2) && (value[3] == c3);
}

static bool wav_is_pcm_subformat(const uint8_t *guid)
{
    static const uint8_t pcm_tail[14] PROGMEM =
    {
        0x00, 0x00, 0x00, 0x00,
        0x10, 0x00,
        0x80, 0x00,
        0x00, 0xAA,
        0x00, 0x38, 0x9B, 0x71
    };

    if (guid == NULL)
    {
        return false;
    }

    return (wav_read_le16(guid) == WAV_FORMAT_PCM) &&
           (memcmp_P(guid + 2, pcm_tail, sizeof(pcm_tail)) == 0);
}

static void wav_close_with_status(wav_reader_status_t status)
{
    wav_status = status;
    wav_open = false;
    sdcard_file_close();
}

bool wav_reader_open(const char *path, wav_reader_info_t *info)
{
    uint8_t riff_header[12];
    uint8_t chunk_header[8];
    uint8_t fmt_data[16];

    uint32_t file_size;
    uint32_t riff_size;
    uint32_t declared_riff_end;
    uint32_t scan_end_position;

    uint16_t audio_format = 0;

    bool fmt_found = false;
    bool data_found = false;

    memset(&wav_info, 0, sizeof(wav_info));
    wav_open = false;
    wav_status = WAV_READER_STATUS_BAD_ARGUMENT;

    if ((path == NULL) || (info == NULL))
    {
        return false;
    }

    if (!sdcard_file_open_read(path))
    {
        wav_status = WAV_READER_STATUS_OPEN_ERROR;
        return false;
    }

    file_size = sdcard_file_size();

    if (file_size < sizeof(riff_header))
    {
        wav_close_with_status(WAV_READER_STATUS_BAD_RIFF_SIZE);
        return false;
    }

    if (!wav_read_exact(riff_header, sizeof(riff_header)))
    {
        wav_close_with_status(WAV_READER_STATUS_IO_ERROR);
        return false;
    }

    if (!wav_fourcc_equals(riff_header, 'R', 'I', 'F', 'F'))
    {
        wav_close_with_status(WAV_READER_STATUS_NOT_RIFF);
        return false;
    }

    if (!wav_fourcc_equals(riff_header + 8, 'W', 'A', 'V', 'E'))
    {
        wav_close_with_status(WAV_READER_STATUS_NOT_WAVE);
        return false;
    }

    riff_size = wav_read_le32(riff_header + 4);

    /*
        Standard RIFF has file_size = riff_size + 8. Some recorders leave the
        size as zero or 0xFFFFFFFF, and some exporters do not update it after
        writing metadata. Parse to physical EOF in those cases.
    */
    if (!wav_add_u32(riff_size, 8U, &declared_riff_end) ||
        (declared_riff_end < sizeof(riff_header)) ||
        (declared_riff_end > file_size))
    {
        scan_end_position = file_size;
    }
    else
    {
        scan_end_position = declared_riff_end;
    }

    while (sdcard_file_position() < scan_end_position)
    {
        uint32_t chunk_size;
        uint32_t chunk_data_position;

        if ((scan_end_position - sdcard_file_position()) < sizeof(chunk_header))
        {
            /* Tolerate an incomplete trailing pad/metadata tail only. */
            break;
        }

        if (!wav_read_exact(chunk_header, sizeof(chunk_header)))
        {
            wav_close_with_status(WAV_READER_STATUS_IO_ERROR);
            return false;
        }

        chunk_size = wav_read_le32(chunk_header + 4);
        chunk_data_position = sdcard_file_position();

        if (wav_fourcc_equals(chunk_header, 'f', 'm', 't', ' '))
        {
            if (chunk_size < sizeof(fmt_data))
            {
                wav_close_with_status(WAV_READER_STATUS_BAD_LAYOUT);
                return false;
            }

            if (!wav_read_exact(fmt_data, sizeof(fmt_data)))
            {
                wav_close_with_status(WAV_READER_STATUS_IO_ERROR);
                return false;
            }

            audio_format = wav_read_le16(fmt_data + 0);
            wav_info.channels = wav_read_le16(fmt_data + 2);
            wav_info.sample_rate = wav_read_le32(fmt_data + 4);
            wav_info.byte_rate = wav_read_le32(fmt_data + 8);
            wav_info.block_align = wav_read_le16(fmt_data + 12);
            wav_info.bits_per_sample = wav_read_le16(fmt_data + 14);

            if (audio_format == WAV_FORMAT_EXTENSIBLE)
            {
                uint8_t extension[24];

                if ((chunk_size < 40U) || !wav_read_exact(extension, sizeof(extension)))
                {
                    wav_close_with_status(WAV_READER_STATUS_BAD_LAYOUT);
                    return false;
                }

                /* cbSize must cover valid-bits, mask and 16-byte subformat. */
                if ((wav_read_le16(extension) < 22U) ||
                    !wav_is_pcm_subformat(extension + 8))
                {
                    wav_close_with_status(WAV_READER_STATUS_UNSUPPORTED_FORMAT);
                    return false;
                }

                audio_format = WAV_FORMAT_PCM;
            }

            fmt_found = true;

            if (!wav_skip_chunk(chunk_data_position, chunk_size, scan_end_position))
            {
                wav_close_with_status(WAV_READER_STATUS_BAD_LAYOUT);
                return false;
            }
        }
        else if (wav_fourcc_equals(chunk_header, 'd', 'a', 't', 'a'))
        {
            uint32_t data_end_position;

            if (!wav_add_u32(chunk_data_position, chunk_size, &data_end_position) ||
                (data_end_position > file_size))
            {
                wav_close_with_status(WAV_READER_STATUS_BAD_LAYOUT);
                return false;
            }

            if (!data_found)
            {
                wav_info.data_offset = chunk_data_position;
                wav_info.data_size = chunk_size;
                data_found = true;
            }

            if (fmt_found)
            {
                break;
            }

            if (!wav_skip_chunk(chunk_data_position, chunk_size, scan_end_position))
            {
                wav_close_with_status(WAV_READER_STATUS_BAD_LAYOUT);
                return false;
            }
        }
        else
        {
            if (!wav_skip_chunk(chunk_data_position, chunk_size, scan_end_position))
            {
                wav_close_with_status(WAV_READER_STATUS_BAD_LAYOUT);
                return false;
            }
        }
    }

    if (!fmt_found)
    {
        wav_close_with_status(WAV_READER_STATUS_NO_FMT_CHUNK);
        return false;
    }

    if (!data_found)
    {
        wav_close_with_status(WAV_READER_STATUS_NO_DATA_CHUNK);
        return false;
    }

    if (audio_format != WAV_FORMAT_PCM)
    {
        wav_close_with_status(WAV_READER_STATUS_UNSUPPORTED_FORMAT);
        return false;
    }

    if (wav_info.channels != 1)
    {
        wav_close_with_status(WAV_READER_STATUS_UNSUPPORTED_CHANNELS);
        return false;
    }

    if (wav_info.bits_per_sample != 8)
    {
        wav_close_with_status(WAV_READER_STATUS_UNSUPPORTED_BITS);
        return false;
    }

    if ((wav_info.sample_rate != 22050UL) &&
        (wav_info.sample_rate != 44100UL) &&
        (wav_info.sample_rate != 48000UL))
    {
        wav_close_with_status(WAV_READER_STATUS_UNSUPPORTED_RATE);
        return false;
    }

    if ((wav_info.block_align != 1U) ||
        (wav_info.byte_rate != wav_info.sample_rate))
    {
        wav_close_with_status(WAV_READER_STATUS_BAD_LAYOUT);
        return false;
    }

    if (!sdcard_file_seek(wav_info.data_offset))
    {
        wav_close_with_status(WAV_READER_STATUS_IO_ERROR);
        return false;
    }

    wav_info.samples_remaining = wav_info.data_size;

    wav_open = true;
    wav_status = WAV_READER_STATUS_OK;

    *info = wav_info;
    return true;
}

int16_t wav_reader_read_samples(uint8_t *buffer, uint16_t max_samples)
{
    uint16_t request;
    int16_t read_count;

    if (!wav_open || (buffer == NULL) || (max_samples == 0))
    {
        wav_status = WAV_READER_STATUS_BAD_ARGUMENT;
        return -1;
    }

    if (wav_info.samples_remaining == 0)
    {
        return 0;
    }

    request = max_samples;

    if (wav_info.samples_remaining < request)
    {
        request = (uint16_t)wav_info.samples_remaining;
    }

    read_count = sdcard_file_read(buffer, request);

    if (read_count < 0)
    {
        wav_close_with_status(WAV_READER_STATUS_IO_ERROR);
        return -1;
    }

    if (read_count == 0)
    {
        /* Data chunk claims more bytes than the physical file provides. */
        wav_close_with_status(WAV_READER_STATUS_IO_ERROR);
        return -1;
    }

    wav_info.samples_remaining -= (uint16_t)read_count;

    return read_count;
}

bool wav_reader_seek_to_start(void)
{
    if (!wav_open)
    {
        return false;
    }

    if (!sdcard_file_seek(wav_info.data_offset))
    {
        wav_close_with_status(WAV_READER_STATUS_IO_ERROR);
        return false;
    }

    wav_info.samples_remaining = wav_info.data_size;
    return true;
}

void wav_reader_close(void)
{
    sdcard_file_close();
    wav_open = false;
}

bool wav_reader_is_open(void)
{
    return wav_open;
}

const wav_reader_info_t *wav_reader_get_info(void)
{
    return wav_open ? &wav_info : NULL;
}

wav_reader_status_t wav_reader_last_status(void)
{
    return wav_status;
}

PGM_P wav_reader_status_text_P(wav_reader_status_t status)
{
    static const char ok[] PROGMEM = "OK";
    static const char bad_argument[] PROGMEM = "BAD ARG";
    static const char open_error[] PROGMEM = "OPEN ERROR";
    static const char io_error[] PROGMEM = "IO ERROR";
    static const char not_riff[] PROGMEM = "NOT RIFF";
    static const char not_wave[] PROGMEM = "NOT WAVE";
    static const char bad_riff_size[] PROGMEM = "BAD RIFF";
    static const char no_fmt_chunk[] PROGMEM = "NO FMT";
    static const char no_data_chunk[] PROGMEM = "NO DATA";
    static const char unsupported_format[] PROGMEM = "PCM ONLY";
    static const char unsupported_channels[] PROGMEM = "MONO ONLY";
    static const char unsupported_bits[] PROGMEM = "8BIT ONLY";
    static const char unsupported_rate[] PROGMEM = "BAD RATE";
    static const char bad_layout[] PROGMEM = "BAD WAV";
    static const char unknown[] PROGMEM = "WAV ERROR";
    static const char * const texts[] PROGMEM =
    {
        ok, bad_argument, open_error, io_error, not_riff, not_wave,
        bad_riff_size, no_fmt_chunk, no_data_chunk, unsupported_format,
        unsupported_channels, unsupported_bits, unsupported_rate, bad_layout
    };

    if ((uint8_t)status > (uint8_t)WAV_READER_STATUS_BAD_LAYOUT)
        return unknown;
    return (PGM_P)pgm_read_word(&texts[(uint8_t)status]);
}
