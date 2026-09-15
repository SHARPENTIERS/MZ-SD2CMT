#ifndef SD2CMT2_WAV_SAMPLE_STREAM_H
#define SD2CMT2_WAV_SAMPLE_STREAM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "../formats/wav_reader.h"

/*
    Prepared binary samples, packed one byte per eight WAV samples.

    Storage remains 2048 SRAM bytes but the Timer1 ISR no longer calculates a
    bit address, reads a 16-bit sequence pointer and writes it back for every
    single sample. The ISR keeps the current byte in registers/state and
    accesses this FIFO only once per eight output samples.

    2047 usable bytes hold 16376 samples:
    - 742.7 ms at 22050 Hz
    - 371.3 ms at 44100 Hz
    - 341.2 ms at 48000 Hz
*/
#define WAV_SAMPLE_STREAM_BUFFER_BYTES 2048U
#define WAV_SAMPLE_STREAM_CAPACITY_BYTES \
    (WAV_SAMPLE_STREAM_BUFFER_BYTES - 1U)
#define WAV_SAMPLE_STREAM_CAPACITY_SAMPLES \
    (WAV_SAMPLE_STREAM_CAPACITY_BYTES * 8U)
#define WAV_SAMPLE_STREAM_BYTE_MASK \
    (WAV_SAMPLE_STREAM_BUFFER_BYTES - 1U)

/* Raw WAV bytes decoded per foreground SD refill. */
#define WAV_SAMPLE_STREAM_REFILL_BLOCK 512U

/*
    SPSC byte-ring ABI used by the Timer1 sample driver.

    Producer: foreground WAV reader/threshold converter.
    Consumer: Timer1 ISR.

    Each published byte contains sample bits bit0..bit7 in chronological
    order. Only the final byte may contain fewer than eight valid sample bits.
*/
extern volatile uint8_t wav_sample_stream_isr_bytes[WAV_SAMPLE_STREAM_BUFFER_BYTES];
extern volatile uint16_t wav_sample_stream_isr_read_byte_sequence;
extern volatile uint16_t wav_sample_stream_isr_write_byte_sequence;
extern volatile uint8_t wav_sample_stream_isr_source_finished;
extern volatile uint8_t wav_sample_stream_isr_tail_valid_bits;

static inline __attribute__((always_inline))
bool wav_sample_stream_pop_byte_fast_from_isr(uint8_t *packed_byte,
                                              uint8_t *valid_bits)
{
    uint16_t read_local = wav_sample_stream_isr_read_byte_sequence;
    uint16_t write_local = wav_sample_stream_isr_write_byte_sequence;
    uint8_t bits = 8U;

    if ((packed_byte == NULL) || (valid_bits == NULL) ||
        (read_local == write_local))
    {
        return false;
    }

    *packed_byte = wav_sample_stream_isr_bytes[
        read_local & WAV_SAMPLE_STREAM_BYTE_MASK
    ];

    /* The final partial byte is identifiable only after producer EOF. */
    if ((wav_sample_stream_isr_source_finished != 0U) &&
        ((uint16_t)(read_local + 1U) == write_local))
    {
        bits = wav_sample_stream_isr_tail_valid_bits;
    }

    wav_sample_stream_isr_read_byte_sequence =
        (uint16_t)(read_local + 1U);

    *valid_bits = bits;
    return bits != 0U;
}

static inline __attribute__((always_inline))
bool wav_sample_stream_finished_fast_from_isr(void)
{
    return (wav_sample_stream_isr_source_finished != 0U) &&
           (wav_sample_stream_isr_read_byte_sequence ==
            wav_sample_stream_isr_write_byte_sequence);
}

typedef struct
{
    uint8_t low_threshold;
    uint8_t high_threshold;
    bool invert_signal;

} wav_sample_stream_config_t;

bool wav_sample_stream_open(const char *path,
                            const wav_sample_stream_config_t *config);

void wav_sample_stream_close(void);

/* Foreground only. Decodes at most 512 raw WAV samples per call. */
bool wav_sample_stream_refill_block(void);

/* Fill all available byte-ring space before Timer1 starts. */
bool wav_sample_stream_prefill(void);

/* Foreground dequeue, used only by tests and diagnostics. */
bool wav_sample_stream_pop(uint8_t *level);

/* Number of already published, consumable source samples. */
uint16_t wav_sample_stream_available(void);

bool wav_sample_stream_source_finished(void);
bool wav_sample_stream_finished(void);

const wav_reader_info_t *wav_sample_stream_info(void);
wav_reader_status_t wav_sample_stream_last_status(void);

/*
    512-byte foreground work block shared with RECORD.
    RECORD may use it only after wav_sample_stream_close(): PLAY and RECORD
    are mutually exclusive modes.
*/
uint8_t *wav_sample_stream_get_shared_work_buffer(void);

#endif
