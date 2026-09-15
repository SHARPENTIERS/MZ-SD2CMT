#ifndef SD2CMT2_TIMER3B_OWNER_H
#define SD2CMT2_TIMER3B_OWNER_H

#include <stdint.h>

typedef enum
{
    TIMER3B_OWNER_NONE = 0U,
    TIMER3B_OWNER_EDGE = 1U,
    TIMER3B_OWNER_MZF = 2U,
    TIMER3B_OWNER_TAP = 3U
} timer3b_owner_t;

extern volatile uint8_t timer3b_owner;

static inline void timer3b_owner_set_from_isr(timer3b_owner_t owner)
{
    timer3b_owner = (uint8_t)owner;
}

static inline timer3b_owner_t timer3b_owner_get_from_isr(void)
{
    return (timer3b_owner_t)timer3b_owner;
}

#endif
