#ifndef SD2CMT2_KEYPAD_H
#define SD2CMT2_KEYPAD_H

#include <stdint.h>

#define KEYPAD_CALIBRATION_MIN_SEPARATION 20U

typedef enum
{
    BUTTON_NONE = 0,
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_SELECT

} button_t;

typedef enum
{
    BUTTON_EVENT_NONE = 0,

    BUTTON_EVENT_UP_PRESS,
    BUTTON_EVENT_UP_REPEAT,

    BUTTON_EVENT_DOWN_PRESS,
    BUTTON_EVENT_DOWN_REPEAT,

    BUTTON_EVENT_LEFT_SHORT,
    BUTTON_EVENT_LEFT_LONG,

    BUTTON_EVENT_RIGHT_SHORT,
    BUTTON_EVENT_RIGHT_LONG,

    BUTTON_EVENT_SELECT_SHORT,
    BUTTON_EVENT_SELECT_LONG

} button_event_t;

typedef struct
{
    uint16_t none;

    uint16_t up;
    uint16_t down;
    uint16_t left;
    uint16_t right;
    uint16_t select;

} keypad_calibration_t;

void keypad_init(void);

uint16_t keypad_read_raw(void);

button_t keypad_get_button(void);

button_event_t keypad_get_event(void);

void keypad_set_calibration(const keypad_calibration_t *calibration);

void keypad_get_default_calibration(keypad_calibration_t *calibration);

bool keypad_calibration_is_valid(const keypad_calibration_t *calibration);


#endif
