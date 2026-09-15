#pragma once

#include <Arduino.h>
#include "../drivers/keypad.h"
#include "../play/play_controller.h"

typedef enum
{
    PLAY_SCREEN_ACTION_NONE = 0,
    PLAY_SCREEN_ACTION_TOGGLE_PLAY,
    PLAY_SCREEN_ACTION_BACK
} play_screen_action_t;

play_screen_action_t play_screen_handle_event(button_event_t event);
void play_screen_render(const play_controller_view_t *view);
