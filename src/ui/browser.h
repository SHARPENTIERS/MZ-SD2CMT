#pragma once

#include <Arduino.h>
#include "../drivers/keypad.h"

typedef enum
{
    BROWSER_ACTION_NONE = 0,
    BROWSER_ACTION_FILE_SELECTED,
    BROWSER_ACTION_RECORD_REQUESTED,
    BROWSER_ACTION_PLAY_MENU_REQUESTED,
    BROWSER_ACTION_RECORD_MENU_REQUESTED,
    BROWSER_ACTION_SETTINGS_REQUESTED
} browser_action_t;

void browser_init(bool initial_sd_ok);
browser_action_t browser_handle_event(button_event_t event);
void browser_service(void);
void browser_render(void);
const char* browser_get_selected_name(void);
bool browser_selected_is_directory(void);
void browser_save_position(void);
void browser_restore_saved_position(void);
void browser_clear_status(void);

/* Current browser directory is used as the target for RECxxxx.WAV files. */
const char* browser_get_current_path(void);

/* Rebuild current directory after a completed recording. */
void browser_refresh(void);
