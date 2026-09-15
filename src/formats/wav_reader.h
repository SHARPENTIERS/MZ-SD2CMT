#ifndef SD2CMT2_WAV_READER_H
#define SD2CMT2_WAV_READER_H

#include <stdbool.h>
#include <stdint.h>
#include <avr/pgmspace.h>

typedef enum
{
    WAV_READER_STATUS_OK = 0,
    WAV_READER_STATUS_BAD_ARGUMENT,
    WAV_READER_STATUS_OPEN_ERROR,
    WAV_READER_STATUS_IO_ERROR,
    WAV_READER_STATUS_NOT_RIFF,
    WAV_READER_STATUS_NOT_WAVE,
    WAV_READER_STATUS_BAD_RIFF_SIZE,
    WAV_READER_STATUS_NO_FMT_CHUNK,
    WAV_READER_STATUS_NO_DATA_CHUNK,
    WAV_READER_STATUS_UNSUPPORTED_FORMAT,
    WAV_READER_STATUS_UNSUPPORTED_CHANNELS,
    WAV_READER_STATUS_UNSUPPORTED_BITS,
    WAV_READER_STATUS_UNSUPPORTED_RATE,
    WAV_READER_STATUS_BAD_LAYOUT
} wav_reader_status_t;

typedef struct
{
    uint32_t sample_rate;
    uint32_t byte_rate;

    uint32_t data_offset;
    uint32_t data_size;
    uint32_t samples_remaining;

    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;

} wav_reader_info_t;

/*
    Supported WAV profile:
    - RIFF/WAVE
    - PCM format tag 1, or WAVE_FORMAT_EXTENSIBLE carrying PCM subformat
    - mono
    - 8-bit unsigned samples
    - 22050, 44100 or 48000 Hz

    The parser tolerates broken RIFF-size fields found in some recorder/exporter
    files; every chunk and data range is still checked against physical file size.
*/
bool wav_reader_open(const char *path, wav_reader_info_t *info);

int16_t wav_reader_read_samples(uint8_t *buffer, uint16_t max_samples);

bool wav_reader_seek_to_start(void);

void wav_reader_close(void);

bool wav_reader_is_open(void);

const wav_reader_info_t *wav_reader_get_info(void);

wav_reader_status_t wav_reader_last_status(void);

PGM_P wav_reader_status_text_P(wav_reader_status_t status);

#endif
