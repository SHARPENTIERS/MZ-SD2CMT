#ifndef SD2CMT2_MZ_TAPE_DECODER_H
#define SD2CMT2_MZ_TAPE_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#define MZ_TAPE_HEADER_BYTES 128U

typedef enum
{
    MZ_TAPE_DECODER_EVENT_NONE = 0,
    MZ_TAPE_DECODER_EVENT_HEADER_VALID,
    MZ_TAPE_DECODER_EVENT_DATA_BYTE,
    MZ_TAPE_DECODER_EVENT_BLOCK_VALID,
    MZ_TAPE_DECODER_EVENT_BLOCK_INVALID
} mz_tape_decoder_event_type_t;

typedef struct
{
    mz_tape_decoder_event_type_t type;
    uint8_t value;
    uint32_t byte_index;
    uint16_t calculated_checksum;
    uint16_t recorded_checksum;
    uint16_t leader_pulses;
    uint8_t copy_index;
} mz_tape_decoder_event_t;

void mz_tape_decoder_begin_header(void);
void mz_tape_decoder_start_data(uint32_t byte_count, bool invert_pulse_phase);
void mz_tape_decoder_start_recovery_data(uint32_t byte_count);
void mz_tape_decoder_break_signal(void);
void mz_tape_decoder_stop(void);
/* Returns true only when this interval produced a pending decoder event.
   The foreground caller can therefore skip an empty take_event() poll on the
   leader hot path. */
bool mz_tape_decoder_feed_interval(uint16_t duration_units, uint8_t level);
bool mz_tape_decoder_take_event(mz_tape_decoder_event_t *event);
const uint8_t *mz_tape_decoder_get_header(void);
uint8_t *mz_tape_decoder_get_data_scratch(void);
uint8_t mz_tape_decoder_get_pulse_start_level(void);
uint16_t mz_tape_decoder_get_header_short_x8(void);

#endif
