#ifndef SD2CMT2_EDGE_PLAYBACK_H
#define SD2CMT2_EDGE_PLAYBACK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    EDGE_PLAYBACK_STOPPED = 0,
    EDGE_PLAYBACK_READY,
    EDGE_PLAYBACK_RUNNING,
    EDGE_PLAYBACK_PAUSED,
    EDGE_PLAYBACK_FINISHED,
    EDGE_PLAYBACK_UNDERRUN,
    EDGE_PLAYBACK_IO_ERROR,
    EDGE_PLAYBACK_BAD_FILE
} edge_playback_state_t;

void edge_playback_init(void);
bool edge_playback_prepare(const char *path, uint8_t unit_us, bool invert);
bool edge_playback_start(void);
bool edge_playback_pause(void);
bool edge_playback_resume(void);
void edge_playback_stop(void);
void edge_playback_service(void);

edge_playback_state_t edge_playback_get_state(void);
const char *edge_playback_get_error_text(void);
uint8_t edge_playback_get_buffer_fill_percent(void);

/* Byte-accurate source progress derived in foreground from bytes read minus FIFO. */
uint32_t edge_playback_get_consumed_bytes(void);
uint32_t edge_playback_get_total_bytes(void);
uint8_t edge_playback_get_progress_percent(void);

#endif
