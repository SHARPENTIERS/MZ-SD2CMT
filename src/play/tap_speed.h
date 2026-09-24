#ifndef SD2CMT2_TAP_SPEED_H
#define SD2CMT2_TAP_SPEED_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    TAP_SPEED_1400 = 0U,
    TAP_SPEED_1800,
    TAP_SPEED_2100,
    TAP_SPEED_2500,
    TAP_SPEED_COUNT
} tap_speed_t;

typedef struct
{
    uint16_t pilot_half_ticks;
    uint16_t sync_low_ticks;
    uint16_t sync_high_ticks;
    uint16_t long_half_ticks;
    uint16_t short_half_ticks;
} tap_timing_t;

tap_speed_t tap_speed_get(void);
bool tap_speed_set(tap_speed_t speed);
void tap_speed_cycle(void);
uint16_t tap_speed_get_baud(tap_speed_t speed);
bool tap_speed_get_timing(tap_speed_t speed, tap_timing_t *timing);

#endif
