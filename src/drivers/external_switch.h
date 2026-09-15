#ifndef SD2CMT2_EXTERNAL_SWITCH_H
#define SD2CMT2_EXTERNAL_SWITCH_H

#include <stdbool.h>

#define EXTERNAL_SWITCH_RELAY_PIN 23U
#define EXTERNAL_SWITCH_EXTERNAL_ACTIVE_HIGH 1

typedef enum
{
    CMT_SOURCE_INTERNAL = 0,
    CMT_SOURCE_EXTERNAL
} cmt_source_t;

void external_switch_init(void);
void external_switch_set_source(cmt_source_t source);
cmt_source_t external_switch_get_source(void);

#endif
