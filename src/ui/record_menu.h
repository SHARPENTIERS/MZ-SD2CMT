#ifndef SD2CMT2_RECORD_MENU_H
#define SD2CMT2_RECORD_MENU_H

#include <stdbool.h>
#include <stdint.h>
#include <avr/pgmspace.h>
#include "../drivers/keypad.h"
#include "../formats/file_format.h"

typedef enum
{
    RECORD_CONTROL_MOTOR = 0,
    RECORD_CONTROL_AUTO,
    RECORD_CONTROL_MANUAL
} record_control_mode_t;

typedef enum
{
    RECORD_MENU_ACTION_NONE = 0,
    RECORD_MENU_ACTION_BACK
} record_menu_action_t;

void record_menu_init(void);
record_menu_action_t record_menu_handle_event(button_event_t event);
void record_menu_render(void);
bool record_menu_save_if_dirty(void);

file_format_t record_menu_get_format(void);
uint32_t record_menu_get_wav_sample_rate(void);
record_control_mode_t record_menu_get_control_mode(void);
bool record_menu_get_autoname(void);
PGM_P record_control_mode_label_P(record_control_mode_t mode);

#endif
