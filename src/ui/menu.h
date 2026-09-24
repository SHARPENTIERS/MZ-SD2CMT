#ifndef SD2CMT2_MENU_H
#define SD2CMT2_MENU_H

#include <stdbool.h>
#include <avr/pgmspace.h>
#include "../drivers/keypad.h"
#include "../play/loader_mode.h"

/* MOTOR follows the MZ MOTOR line; MANUAL uses SELECT only. */
typedef enum
{
    PLAY_CONTROL_MOTOR = 0,
    PLAY_CONTROL_MANUAL
} play_control_mode_t;

typedef enum
{
    MENU_ACTION_NONE = 0,
    MENU_ACTION_BACK
} menu_action_t;

void menu_init(void);
menu_action_t menu_handle_event(button_event_t event);
void menu_render(void);
bool menu_save_if_dirty(void);
bool menu_get_invert_signal(void);
loader_mode_t menu_get_loader_mode(void);
play_control_mode_t menu_get_play_control_mode(void);
PGM_P play_control_mode_label_P(play_control_mode_t mode);

#endif
