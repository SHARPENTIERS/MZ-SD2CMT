#ifndef SD2CMT2_EDGE_RECORD_DRIVER_H
#define SD2CMT2_EDGE_RECORD_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    EDGE_RECORD_DRIVER_STOPPED = 0,
    EDGE_RECORD_DRIVER_READY,
    EDGE_RECORD_DRIVER_RUNNING,
    EDGE_RECORD_DRIVER_PAUSED,
    EDGE_RECORD_DRIVER_OVERRUN,
    EDGE_RECORD_DRIVER_BAD_ARGUMENT
} edge_record_driver_state_t;

/*
   Internal ISR-to-foreground stream. It is never written directly to a
   .LEP/.L16 file. The foreground expands it to normal signed LEP/L16 slots.

   0x10..0xFF: high nibble = one 1..15-unit interval, low nibble = optional
               second 1..15-unit interval. Polarity is implicit because each
               accepted WRITE transition changes the signal polarity.
   0x01,u:     one 16..127-unit interval.
   0x02:       one elapsed block of exactly 127 output units in the current
               unchanged WRITE level.
   0x03,t:     end of that long level. t is signed -1..127 residual output
               units after the complete 127-unit blocks.

   Timer5 is CTC-ticked every 127 format units. This keeps only one pending
   127-unit block, not a whole no-edge duration: the foreground emits standard
   zero extensions as the level continues and adjusts the first signed slot
   after the trailing edge. No Timer5 overflow accumulator is used.
*/
#define EDGE_RECORD_TOKEN_UNIT 0x01U
#define EDGE_RECORD_TOKEN_LONG_BLOCK 0x02U
#define EDGE_RECORD_TOKEN_LONG_TAIL 0x03U
#define EDGE_RECORD_TOKEN_LONG_TAIL_BYTES 2U

void edge_record_driver_init(void);
bool edge_record_driver_prepare(uint8_t unit_us);
bool edge_record_driver_start(void);
bool edge_record_driver_pause(void);
bool edge_record_driver_resume(void);
void edge_record_driver_stop(void);
void edge_record_driver_abort(void);

/* Called only by write_edge_monitor PCINT1 ISR. */
void edge_record_driver_on_write_edge_from_isr(uint8_t new_level);

edge_record_driver_state_t edge_record_driver_get_state(void);
uint16_t edge_record_driver_available_bytes(void);
bool edge_record_driver_pop_byte(uint8_t *value);
uint8_t edge_record_driver_fill_percent(void);
uint8_t edge_record_driver_get_initial_level(void);

#endif
