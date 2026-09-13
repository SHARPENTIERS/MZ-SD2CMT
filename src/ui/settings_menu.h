#ifndef SD2CMT2_SETTINGS_MENU_H
#define SD2CMT2_SETTINGS_MENU_H

#include <stdbool.h>

#include "../drivers/keypad.h"

typedef enum
{
    SETTINGS_MENU_ACTION_NONE = 0,
    SETTINGS_MENU_ACTION_BACK
} settings_menu_action_t;

void settings_menu_init(void);
settings_menu_action_t settings_menu_handle_event(button_event_t event);
void settings_menu_render(void);
bool settings_menu_save_if_dirty(void);

#endif
