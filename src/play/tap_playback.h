#ifndef SD2CMT2_TAP_PLAYBACK_H
#define SD2CMT2_TAP_PLAYBACK_H

#include <stdbool.h>
#include <stdint.h>

#include "tap_speed.h"

typedef enum
{
    TAP_PLAYBACK_STOPPED = 0,
    TAP_PLAYBACK_READY,
    TAP_PLAYBACK_RUNNING,
    TAP_PLAYBACK_PAUSED,
    TAP_PLAYBACK_FINISHED,
    TAP_PLAYBACK_UNDERRUN,
    TAP_PLAYBACK_IO_ERROR,
    TAP_PLAYBACK_BAD_FILE
} tap_playback_state_t;

void tap_playback_init(void);
bool tap_playback_prepare(const char *path, tap_speed_t speed);
bool tap_playback_start(void);
bool tap_playback_pause(void);
bool tap_playback_resume(void);
void tap_playback_stop(void);
void tap_playback_service(void);

tap_playback_state_t tap_playback_get_state(void);
const char *tap_playback_get_error_text(void);
uint8_t tap_playback_get_progress_percent(void);
uint8_t tap_playback_get_buffer_fill_percent(void);

/* Called only by the shared Timer3 compare-B ISR dispatcher. */
void tap_playback_timer3_compb_from_isr(void);

#endif
