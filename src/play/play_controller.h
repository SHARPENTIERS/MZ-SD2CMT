#pragma once

#include <Arduino.h>
#include "../formats/file_format.h"
#include "../ui/menu.h"
#include "play_engine.h"

#define PLAY_CONTROLLER_PATH_MAX 160

typedef enum
{
    PLAY_CONTROLLER_STATE_READY = 0,
    PLAY_CONTROLLER_STATE_PLAYING,
    PLAY_CONTROLLER_STATE_PAUSED,
    PLAY_CONTROLLER_STATE_ERROR
} play_controller_state_t;

typedef struct
{
    const char *filename;
    const char *full_path;
    file_format_t format;
    bool invert_signal;
    loader_mode_t loader_mode;
    play_control_mode_t control_mode;
    bool waiting_for_motor;

    /* These are mutually exclusive while state == PAUSED. */
    bool paused_by_motor;
    bool paused_by_user;

    play_controller_state_t state;
    const char *error_text;

    /* Current logical record time. MZF/M12 have one record; MZT resets these
       when the next internal record becomes active. */
    uint32_t elapsed_ms;
    uint32_t total_duration_ms;

    /* LEP/L16/TAP and handshaked loader phases use percentage progress. */
    bool progress_is_percent;
    play_progress_phase_t progress_phase;
    uint8_t progress_percent;

    uint8_t buffer_fill_percent;

    /* Sidecar / MZT browser information. */
    bool has_info_sidecar;
    /* True only when AUTO successfully resolved the active MFI/MTI profile. */
    bool loader_from_info_sidecar;
    bool mzt_selection_pending;
    uint16_t mzt_record_index;
    uint16_t mzt_record_count;
    const char *mzt_record_title;
    loader_mode_t mzt_record_loader_mode;
} play_controller_view_t;

void play_controller_init(void);
void play_controller_start_session(const char *filename,
                                   const char *directory_path,
                                   bool invert_signal,
                                   loader_mode_t loader_mode,
                                   play_control_mode_t control_mode);
void play_controller_toggle_play_pause(void);
void play_controller_stop(void);
/* Active MZT: stop transport and return to MZT mini-browser. Returns false when normal BACK should exit. */
bool play_controller_stop_to_mzt_selector(void);

/* READY MZT selector. direction <0 = previous, >0 = next, with wrap. */
bool play_controller_select_mzt_record(int8_t direction);

/* Synchronizes UI state with asynchronous transport completion/error. */
void play_controller_service(void);

void play_controller_get_view(play_controller_view_t *view);
const char* play_controller_get_filename(void);
const char* play_controller_get_full_path(void);
file_format_t play_controller_get_format(void);
bool play_controller_get_invert_signal(void);
loader_mode_t play_controller_get_loader_mode(void);
play_control_mode_t play_controller_get_control_mode(void);
play_controller_state_t play_controller_get_state(void);
