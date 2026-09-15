#include "tap_speed.h"

#include <stddef.h>

static tap_speed_t selected_tap_speed = TAP_SPEED_1400;

tap_speed_t tap_speed_get(void)
{
    return selected_tap_speed;
}

bool tap_speed_set(tap_speed_t speed)
{
    if ((uint8_t)speed >= (uint8_t)TAP_SPEED_COUNT) return false;
    selected_tap_speed = speed;
    return true;
}

void tap_speed_cycle(void)
{
    uint8_t next = (uint8_t)selected_tap_speed + 1U;
    selected_tap_speed = (next < (uint8_t)TAP_SPEED_COUNT) ?
        (tap_speed_t)next : TAP_SPEED_1400;
}

uint16_t tap_speed_get_baud(tap_speed_t speed)
{
    switch (speed)
    {
        case TAP_SPEED_1800: return 1800U;
        case TAP_SPEED_2100: return 2100U;
        case TAP_SPEED_2500: return 2500U;
        case TAP_SPEED_1400:
        default: return 1400U;
    }
}

bool tap_speed_get_timing(tap_speed_t speed, tap_timing_t *timing)
{
    if (timing == NULL) return false;

    switch (speed)
    {
        case TAP_SPEED_1400:
            *timing = { 9798U, 2321U, 2979U, 7749U, 3874U };
            return true;
        case TAP_SPEED_1800:
            *timing = { 7621U, 1805U, 2317U, 6027U, 3013U };
            return true;
        case TAP_SPEED_2100:
            *timing = { 6532U, 1547U, 1986U, 5166U, 2583U };
            return true;
        case TAP_SPEED_2500:
            *timing = { 5487U, 1300U, 1668U, 4340U, 2170U };
            return true;
        default:
            return false;
    }
}
