#ifndef SD2CMT2_EDGE_RECORD_ENGINE_H
#define SD2CMT2_EDGE_RECORD_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include "../formats/file_format.h"

typedef enum
{
    EDGE_RECORD_ENGINE_STOPPED = 0,
    EDGE_RECORD_ENGINE_RECORDING,
    EDGE_RECORD_ENGINE_PAUSED,
    EDGE_RECORD_ENGINE_FINALIZING,
    EDGE_RECORD_ENGINE_FINISHED,
    EDGE_RECORD_ENGINE_ERROR
} edge_record_engine_state_t;

void edge_record_engine_init(void);
/* Finds the next shared RECxxxx.LEP/L16 name without creating a file. */
bool edge_record_engine_preview_filename(const char *directory_path, file_format_t format);
bool edge_record_engine_start(const char *directory_path, file_format_t format);
bool edge_record_engine_pause(void);
bool edge_record_engine_resume(void);
void edge_record_engine_request_stop(void);
void edge_record_engine_cancel(void);
void edge_record_engine_service(void);

edge_record_engine_state_t edge_record_engine_get_state(void);
const char *edge_record_engine_get_filename(void);
const char *edge_record_engine_get_full_path(void);
const char *edge_record_engine_get_error_text(void);
uint8_t edge_record_engine_get_buffer_fill_percent(void);
/* Free capacity of capture FIFO plus 512-byte foreground staging block. */
uint8_t edge_record_engine_get_buffer_headroom_percent(void);

#endif
