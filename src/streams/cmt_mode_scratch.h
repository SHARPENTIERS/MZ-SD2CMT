#ifndef SD2CMT2_CMT_MODE_SCRATCH_H
#define SD2CMT2_CMT_MODE_SCRATCH_H

#include <stdint.h>

/* LEP/L16 RECORD uses this as its second 512-byte SD staging sector. */
typedef struct
{
    uint8_t edge_record_stage_bytes[512];

} cmt_mode_scratch_t;

extern cmt_mode_scratch_t cmt_mode_scratch;

#endif
