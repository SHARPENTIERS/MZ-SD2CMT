#ifndef SD2CMT2_RECORD_SAMPLE_STREAM_H
#define SD2CMT2_RECORD_SAMPLE_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "wav_sample_stream.h"

/*
    RECORD reuses the proven 2 KiB WAV PLAY byte FIFO. The modes are mutually
    exclusive. Only sequence counters are RECORD-specific.

    One stored byte contains eight chronological samples from WRITE/D15,
    bit0 first. 2047 usable bytes provide 16,376 samples of protection:
      742.7 ms at 22,050 Hz
      371.3 ms at 44,100 Hz
*/
#define RECORD_SAMPLE_STREAM_BUFFER_BYTES WAV_SAMPLE_STREAM_BUFFER_BYTES
#define RECORD_SAMPLE_STREAM_CAPACITY_BYTES WAV_SAMPLE_STREAM_CAPACITY_BYTES
#define RECORD_SAMPLE_STREAM_BYTE_MASK WAV_SAMPLE_STREAM_BYTE_MASK
#define record_sample_stream_bytes wav_sample_stream_isr_bytes

void record_sample_stream_reset(void);

/* Producer: Timer1 ISR. Returns false only if the byte FIFO is full. */
bool record_sample_stream_push_byte_from_isr(uint8_t packed_byte);

/*
    Consumer: foreground only. Copies at most max_bytes while Timer1 remains
    enabled. The ISR only appends and cannot overwrite unread bytes.
*/
uint16_t record_sample_stream_pop_bytes(uint8_t *destination,
                                        uint16_t max_bytes);

uint16_t record_sample_stream_available_bytes(void);
uint8_t record_sample_stream_fill_percent(void);

#endif
