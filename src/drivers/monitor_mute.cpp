#include "monitor_mute.h"

#include <Arduino.h>
#include <avr/io.h>

static volatile uint8_t monitor_enabled = 0U;


/*
    Set the audio monitor state.

    enabled = true:
        PAM8302A enabled
        D22 / PA0 = HIGH when mute is active LOW

    enabled = false:
        PAM8302A shutdown / muted
        D22 / PA0 = LOW when mute is active LOW
*/
static inline void monitor_write_from_isr(bool enabled)
{
#if MONITOR_MUTE_ACTIVE_LOW

    if (enabled)
    {
        /* HIGH = PAM8302A enabled */
        PORTA |= _BV(PA0);
    }
    else
    {
        /* LOW = PAM8302A shutdown / mute */
        PORTA &= (uint8_t)~_BV(PA0);
    }

#else

    if (enabled)
    {
        PORTA &= (uint8_t)~_BV(PA0);
    }
    else
    {
        PORTA |= _BV(PA0);
    }

#endif

    monitor_enabled = enabled ? 1U : 0U;
}


void monitor_mute_init(void)
{
    /*
        Load the safe MUTE level into the output latch BEFORE
        enabling D22 / PA0 as an output.

        PAM8302A:
            LOW = shutdown / mute
            HIGH = enabled
    */

#if MONITOR_MUTE_ACTIVE_LOW

    /* Start muted. */
    PORTA &= (uint8_t)~_BV(PA0);

#else

    PORTA |= _BV(PA0);

#endif

    DDRA |= _BV(DDA0);

    monitor_enabled = 0U;
}


void monitor_enable(void)
{
    monitor_write_from_isr(true);
}


void monitor_disable(void)
{
    monitor_write_from_isr(false);
}


void monitor_set_tape_activity(bool active)
{
    monitor_write_from_isr(active);
}


void monitor_set_tape_activity_from_isr(bool active)
{
    monitor_write_from_isr(active);
}


bool monitor_is_enabled(void)
{
    return monitor_enabled != 0U;
}