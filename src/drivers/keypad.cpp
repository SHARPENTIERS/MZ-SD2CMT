#include "keypad.h"

#include <Arduino.h>
#include <stdlib.h>

#define KEYPAD_ADC_PIN A0

#define KEYPAD_DEBOUNCE_MS 20
#define KEYPAD_LONG_PRESS_MS 700
#define KEYPAD_REPEAT_START_MS 700
#define KEYPAD_REPEAT_MS 120

static keypad_calibration_t keypad_calibration =
{
    1015, // none
    309,  // up (REWIND)
    134,  // down (FFWD)
    479,  // left
    0,    // right
    719   // select
};

static button_t last_raw_button = BUTTON_NONE;
static button_t stable_button = BUTTON_NONE;

static uint16_t last_raw_change_ms = 0U;

static button_t active_button = BUTTON_NONE;
static uint16_t press_start_ms = 0U;
static uint16_t last_repeat_ms = 0U;

static bool long_event_sent = false;
static bool repeat_started = false;

static uint16_t keypad_abs_diff(uint16_t a, uint16_t b)
{
    if (a > b)
    {
        return (uint16_t)(a - b);
    }

    return (uint16_t)(b - a);
}

static button_t keypad_decode_adc(uint16_t adc)
{
    button_t best_button = BUTTON_NONE;
    uint16_t best_diff = keypad_abs_diff(adc, keypad_calibration.none);

    uint16_t diff_right = keypad_abs_diff(adc, keypad_calibration.right);

    if (diff_right < best_diff)
    {
        best_diff = diff_right;
        best_button = BUTTON_RIGHT;
    }

    uint16_t diff_up = keypad_abs_diff(adc, keypad_calibration.up);

    if (diff_up < best_diff)
    {
        best_diff = diff_up;
        best_button = BUTTON_UP;
    }

    uint16_t diff_down = keypad_abs_diff(adc, keypad_calibration.down);

    if (diff_down < best_diff)
    {
        best_diff = diff_down;
        best_button = BUTTON_DOWN;
    }

    uint16_t diff_left = keypad_abs_diff(adc, keypad_calibration.left);

    if (diff_left < best_diff)
    {
        best_diff = diff_left;
        best_button = BUTTON_LEFT;
    }

    uint16_t diff_select = keypad_abs_diff(adc, keypad_calibration.select);

    if (diff_select < best_diff)
    {
        best_diff = diff_select;
        best_button = BUTTON_SELECT;
    }

    return best_button;
}

static bool keypad_button_uses_press_repeat(button_t button)
{
    return (button == BUTTON_UP || button == BUTTON_DOWN);
}

static bool keypad_button_uses_short_long(button_t button)
{
    return (
        button == BUTTON_LEFT ||
        button == BUTTON_RIGHT ||
        button == BUTTON_SELECT
    );
}

static button_event_t keypad_press_event(button_t button)
{
    switch (button)
    {
        case BUTTON_UP:
            return BUTTON_EVENT_UP_PRESS;

        case BUTTON_DOWN:
            return BUTTON_EVENT_DOWN_PRESS;

        default:
            return BUTTON_EVENT_NONE;
    }
}

static button_event_t keypad_repeat_event(button_t button)
{
    switch (button)
    {
        case BUTTON_UP:
            return BUTTON_EVENT_UP_REPEAT;

        case BUTTON_DOWN:
            return BUTTON_EVENT_DOWN_REPEAT;

        default:
            return BUTTON_EVENT_NONE;
    }
}

static button_event_t keypad_short_event(button_t button)
{
    switch (button)
    {
        case BUTTON_LEFT:
            return BUTTON_EVENT_LEFT_SHORT;

        case BUTTON_RIGHT:
            return BUTTON_EVENT_RIGHT_SHORT;

        case BUTTON_SELECT:
            return BUTTON_EVENT_SELECT_SHORT;

        default:
            return BUTTON_EVENT_NONE;
    }
}

static button_event_t keypad_long_event(button_t button)
{
    switch (button)
    {
        case BUTTON_LEFT:
            return BUTTON_EVENT_LEFT_LONG;

        case BUTTON_RIGHT:
            return BUTTON_EVENT_RIGHT_LONG;

        case BUTTON_SELECT:
            return BUTTON_EVENT_SELECT_LONG;

        default:
            return BUTTON_EVENT_NONE;
    }
}

static void keypad_start_press(button_t button, uint16_t now)
{
    active_button = button;
    press_start_ms = now;
    last_repeat_ms = now;

    long_event_sent = false;
    repeat_started = false;
}

static void keypad_clear_press(void)
{
    active_button = BUTTON_NONE;
    press_start_ms = 0U;
    last_repeat_ms = 0U;

    long_event_sent = false;
    repeat_started = false;
}

void keypad_init(void)
{
    pinMode(KEYPAD_ADC_PIN, INPUT);

    last_raw_button = BUTTON_NONE;
    stable_button = BUTTON_NONE;

    last_raw_change_ms = (uint16_t)millis();

    keypad_clear_press();
}

uint16_t keypad_read_raw(void)
{
    return (uint16_t)analogRead(KEYPAD_ADC_PIN);
}

button_t keypad_get_button(void)
{
    uint16_t adc = keypad_read_raw();

    return keypad_decode_adc(adc);
}

button_event_t keypad_get_event(void)
{
    uint16_t now = (uint16_t)millis();

    button_t raw_button = keypad_get_button();

    if (raw_button != last_raw_button)
    {
        last_raw_button = raw_button;
        last_raw_change_ms = now;
    }

    if ((now - last_raw_change_ms) >= KEYPAD_DEBOUNCE_MS)
    {
        if (stable_button != raw_button)
        {
            button_t previous_stable_button = stable_button;

            stable_button = raw_button;

            if (previous_stable_button == BUTTON_NONE &&
                stable_button != BUTTON_NONE)
            {
                keypad_start_press(stable_button, now);

                if (keypad_button_uses_press_repeat(stable_button))
                {
                    return keypad_press_event(stable_button);
                }

                return BUTTON_EVENT_NONE;
            }

            if (previous_stable_button != BUTTON_NONE &&
                stable_button == BUTTON_NONE)
            {
                button_event_t event = BUTTON_EVENT_NONE;

                if (keypad_button_uses_short_long(previous_stable_button) &&
                    !long_event_sent)
                {
                    event = keypad_short_event(previous_stable_button);
                }

                keypad_clear_press();

                return event;
            }

            if (previous_stable_button != BUTTON_NONE &&
                stable_button != BUTTON_NONE)
            {
                keypad_start_press(stable_button, now);

                if (keypad_button_uses_press_repeat(stable_button))
                {
                    return keypad_press_event(stable_button);
                }

                return BUTTON_EVENT_NONE;
            }
        }
    }

    if (active_button != BUTTON_NONE &&
        stable_button == active_button)
    {
        uint16_t held_ms = (uint16_t)(now - press_start_ms);

        if (keypad_button_uses_press_repeat(active_button))
        {
            if (!repeat_started)
            {
                if (held_ms >= KEYPAD_REPEAT_START_MS)
                {
                    repeat_started = true;
                    last_repeat_ms = now;

                    return keypad_repeat_event(active_button);
                }
            }
            else
            {
                if ((now - last_repeat_ms) >= KEYPAD_REPEAT_MS)
                {
                    last_repeat_ms = now;

                    return keypad_repeat_event(active_button);
                }
            }
        }

        if (keypad_button_uses_short_long(active_button))
        {
            if (!long_event_sent &&
                held_ms >= KEYPAD_LONG_PRESS_MS)
            {
                long_event_sent = true;

                return keypad_long_event(active_button);
            }
        }
    }

    return BUTTON_EVENT_NONE;
}

void keypad_set_calibration(const keypad_calibration_t *calibration)
{
    if (calibration == NULL)
    {
        return;
    }

    keypad_calibration = *calibration;
}

void keypad_get_default_calibration(keypad_calibration_t *calibration)
{
    if (calibration == NULL)
    {
        return;
    }

    calibration->none = 1015;

    calibration->up = 309;
    calibration->down = 134;
    calibration->left = 479;
    calibration->right = 0;
    calibration->select = 719;
}

bool keypad_calibration_is_valid(const keypad_calibration_t *calibration)
{
    uint16_t values[6];

    if (calibration == NULL)
    {
        return false;
    }

    if (calibration->none > 1023 ||
        calibration->up > 1023 ||
        calibration->down > 1023 ||
        calibration->left > 1023 ||
        calibration->right > 1023 ||
        calibration->select > 1023)
    {
        return false;
    }

    /* Do not assume a particular resistor-ladder order. Calibration is also
       allowed to remap physical keys (for example physical LEFT as DOWN).
       It is valid when all six measured ADC levels are distinguishable. */
    values[0] = calibration->none;
    values[1] = calibration->up;
    values[2] = calibration->down;
    values[3] = calibration->left;
    values[4] = calibration->right;
    values[5] = calibration->select;

    for (uint8_t left = 0U; left < 6U; ++left)
    {
        for (uint8_t right = (uint8_t)(left + 1U); right < 6U; ++right)
        {
            if (keypad_abs_diff(values[left], values[right]) <
                KEYPAD_CALIBRATION_MIN_SEPARATION)
            {
                return false;
            }
        }
    }

    return true;
}
