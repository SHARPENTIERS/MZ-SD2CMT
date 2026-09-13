#include "wav_sample_stream.h"

#include <stddef.h>

#include <Arduino.h>
#include <util/atomic.h>

#if ((WAV_SAMPLE_STREAM_BUFFER_BYTES & (WAV_SAMPLE_STREAM_BUFFER_BYTES - 1U)) != 0)
#error "WAV_SAMPLE_STREAM_BUFFER_BYTES must be a power of two"
#endif

volatile uint8_t wav_sample_stream_isr_bytes[WAV_SAMPLE_STREAM_BUFFER_BYTES];
volatile uint16_t wav_sample_stream_isr_read_byte_sequence = 0;
volatile uint16_t wav_sample_stream_isr_write_byte_sequence = 0;
volatile uint8_t wav_sample_stream_isr_source_finished = 1;
volatile uint8_t wav_sample_stream_isr_tail_valid_bits = 8;

static uint8_t wav_raw_buffer[WAV_SAMPLE_STREAM_REFILL_BLOCK];

/* Unpublished samples accumulated while converting foreground raw WAV bytes. */
static uint8_t pack_byte = 0;
static uint8_t pack_bit_count = 0;

static bool input_level = false;

static wav_sample_stream_config_t stream_config =
{
    100,
    155,
    false
};

static wav_reader_info_t stream_info;
static wav_reader_status_t stream_status = WAV_READER_STATUS_BAD_ARGUMENT;

static uint16_t wav_sample_stream_available_bytes_snapshot(void)
{
    uint16_t read_snapshot;
    uint16_t write_snapshot;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        read_snapshot = wav_sample_stream_isr_read_byte_sequence;
        write_snapshot = wav_sample_stream_isr_write_byte_sequence;
    }

    return (uint16_t)(write_snapshot - read_snapshot);
}

static uint8_t wav_sample_stream_decode(uint8_t sample)
{
    /* Samples inside the hysteresis window preserve the preceding level. */
    if (!input_level)
    {
        if (sample >= stream_config.high_threshold)
        {
            input_level = true;
        }
    }
    else if (sample <= stream_config.low_threshold)
    {
        input_level = false;
    }

    return (uint8_t)((input_level ^ stream_config.invert_signal) ? 1U : 0U);
}

static void wav_sample_stream_publish(uint16_t write_local,
                                      bool final_publish,
                                      uint8_t tail_bits)
{
    /*
        All payload bytes were written before this short atomic block. The
        consumer cannot see them until the write sequence is published.
    */
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        asm volatile("" ::: "memory");
        wav_sample_stream_isr_write_byte_sequence = write_local;

        if (final_publish)
        {
            wav_sample_stream_isr_tail_valid_bits = tail_bits;
            wav_sample_stream_isr_source_finished = 1U;
        }
    }
}

bool wav_sample_stream_open(const char *path,
                            const wav_sample_stream_config_t *config)
{
    if ((path == NULL) ||
        (config == NULL) ||
        (config->low_threshold >= config->high_threshold))
    {
        stream_status = WAV_READER_STATUS_BAD_ARGUMENT;
        return false;
    }

    wav_sample_stream_close();

    stream_config = *config;
    input_level = false;
    pack_byte = 0;
    pack_bit_count = 0;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        wav_sample_stream_isr_read_byte_sequence = 0;
        wav_sample_stream_isr_write_byte_sequence = 0;
        wav_sample_stream_isr_tail_valid_bits = 8U;
        wav_sample_stream_isr_source_finished = 0U;
    }

    if (!wav_reader_open(path, &stream_info))
    {
        stream_status = wav_reader_last_status();

        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            wav_sample_stream_isr_source_finished = 1U;
        }

        return false;
    }

    stream_status = WAV_READER_STATUS_OK;
    return wav_sample_stream_prefill();
}

void wav_sample_stream_close(void)
{
    wav_reader_close();

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        wav_sample_stream_isr_read_byte_sequence = 0;
        wav_sample_stream_isr_write_byte_sequence = 0;
        wav_sample_stream_isr_tail_valid_bits = 8U;
        wav_sample_stream_isr_source_finished = 1U;
    }

    pack_byte = 0;
    pack_bit_count = 0;
    input_level = false;
}

bool wav_sample_stream_refill_block(void)
{
    uint16_t used_bytes;
    uint16_t free_bytes;
    uint16_t raw_capacity;
    uint16_t request;
    uint16_t write_local;
    int16_t received;
    const wav_reader_info_t *reader_info;
    bool final_publish = false;
    uint8_t tail_bits = 8U;

    if (wav_sample_stream_isr_source_finished != 0U)
    {
        return true;
    }

    if (!wav_reader_is_open())
    {
        stream_status = wav_reader_last_status();
        return false;
    }

    used_bytes = wav_sample_stream_available_bytes_snapshot();

    if (used_bytes > WAV_SAMPLE_STREAM_CAPACITY_BYTES)
    {
        stream_status = WAV_READER_STATUS_IO_ERROR;
        return false;
    }

    free_bytes = (uint16_t)(WAV_SAMPLE_STREAM_CAPACITY_BYTES - used_bytes);

    if (free_bytes == 0U)
    {
        return true;
    }

    /* A partially assembled byte uses pack_bit_count positions already. */
    raw_capacity = (uint16_t)((uint32_t)free_bytes * 8U - pack_bit_count);

    if (raw_capacity == 0U)
    {
        return true;
    }

    request = raw_capacity;

    if (request > WAV_SAMPLE_STREAM_REFILL_BLOCK)
    {
        request = WAV_SAMPLE_STREAM_REFILL_BLOCK;
    }

    received = wav_reader_read_samples(wav_raw_buffer, request);

    if (received < 0)
    {
        stream_status = wav_reader_last_status();
        return false;
    }

    if (received == 0)
    {
        /* Empty input is only legal before playback starts; caller handles it. */
        wav_sample_stream_publish(
            wav_sample_stream_isr_write_byte_sequence,
            true,
            8U
        );
        return true;
    }

    write_local = wav_sample_stream_isr_write_byte_sequence;

    for (int16_t i = 0; i < received; i++)
    {
        if (wav_sample_stream_decode(wav_raw_buffer[i]))
        {
            pack_byte |= (uint8_t)(1U << pack_bit_count);
        }

        pack_bit_count++;

        if (pack_bit_count == 8U)
        {
            wav_sample_stream_isr_bytes[
                write_local & WAV_SAMPLE_STREAM_BYTE_MASK
            ] = pack_byte;

            write_local = (uint16_t)(write_local + 1U);
            pack_byte = 0U;
            pack_bit_count = 0U;
        }
    }

    reader_info = wav_reader_get_info();

    if ((reader_info != NULL) && (reader_info->samples_remaining == 0UL))
    {
        final_publish = true;

        if (pack_bit_count != 0U)
        {
            wav_sample_stream_isr_bytes[
                write_local & WAV_SAMPLE_STREAM_BYTE_MASK
            ] = pack_byte;

            write_local = (uint16_t)(write_local + 1U);
            tail_bits = pack_bit_count;
            pack_byte = 0U;
            pack_bit_count = 0U;
        }
    }

    wav_sample_stream_publish(write_local, final_publish, tail_bits);
    return true;
}

bool wav_sample_stream_prefill(void)
{
    while (wav_sample_stream_isr_source_finished == 0U)
    {
        uint16_t before = wav_sample_stream_available_bytes_snapshot();

        if (!wav_sample_stream_refill_block())
        {
            return false;
        }

        if (wav_sample_stream_available_bytes_snapshot() == before)
        {
            break;
        }
    }

    return true;
}

bool wav_sample_stream_pop(uint8_t *level)
{
    static uint8_t foreground_byte = 0;
    static uint8_t foreground_bits = 0;
    uint8_t valid_bits;

    if (level == NULL)
    {
        return false;
    }

    if (foreground_bits == 0U)
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            if (!wav_sample_stream_pop_byte_fast_from_isr(
                    &foreground_byte, &valid_bits))
            {
                foreground_bits = 0U;
            }
            else
            {
                foreground_bits = valid_bits;
            }
        }
    }

    if (foreground_bits == 0U)
    {
        return false;
    }

    *level = (uint8_t)(foreground_byte & 1U);
    foreground_byte >>= 1U;
    foreground_bits--;
    return true;
}

uint16_t wav_sample_stream_available(void)
{
    uint32_t samples = (uint32_t)wav_sample_stream_available_bytes_snapshot() * 8UL;

    if (samples > WAV_SAMPLE_STREAM_CAPACITY_SAMPLES)
    {
        samples = WAV_SAMPLE_STREAM_CAPACITY_SAMPLES;
    }

    return (uint16_t)samples;
}

bool wav_sample_stream_source_finished(void)
{
    return wav_sample_stream_isr_source_finished != 0U;
}

bool wav_sample_stream_finished(void)
{
    return wav_sample_stream_finished_fast_from_isr();
}

const wav_reader_info_t *wav_sample_stream_info(void)
{
    return wav_reader_is_open() ? &stream_info : NULL;
}

wav_reader_status_t wav_sample_stream_last_status(void)
{
    return stream_status;
}

uint8_t *wav_sample_stream_get_shared_work_buffer(void)
{
    return wav_raw_buffer;
}
