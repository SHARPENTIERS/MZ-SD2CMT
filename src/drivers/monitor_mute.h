#ifndef SD2CMT2_MONITOR_MUTE_H
#define SD2CMT2_MONITOR_MUTE_H

#include <stdbool.h>

#define MONITOR_MUTE_PIN 22U

/*
    PAM8302A shutdown / mute input.

    SD LOW  = amplifier shutdown / MUTE
    SD HIGH = amplifier enabled

    Therefore the monitor mute control is ACTIVE LOW.
*/
#define MONITOR_MUTE_ACTIVE_LOW 1

void monitor_mute_init(void);

void monitor_enable(void);
void monitor_disable(void);

void monitor_set_tape_activity(bool active);

/* Direct PA0 write for timing-critical terminal/error paths. */
void monitor_set_tape_activity_from_isr(bool active);

bool monitor_is_enabled(void);

#endif