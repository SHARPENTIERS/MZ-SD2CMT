#include "external_switch.h"

#include <Arduino.h>

#include "monitor_mute.h"
#include "mzio.h"

static cmt_source_t selected_source = CMT_SOURCE_INTERNAL;

static void external_switch_write_relay(bool external)
{
#if EXTERNAL_SWITCH_EXTERNAL_ACTIVE_HIGH
    digitalWrite(EXTERNAL_SWITCH_RELAY_PIN, external ? HIGH : LOW);
#else
    digitalWrite(EXTERNAL_SWITCH_RELAY_PIN, external ? LOW : HIGH);
#endif
}

void external_switch_init(void)
{
    /* Load INTERNAL into the output latch before enabling the relay GPIO. */
#if EXTERNAL_SWITCH_EXTERNAL_ACTIVE_HIGH
    digitalWrite(EXTERNAL_SWITCH_RELAY_PIN, LOW);
#else
    digitalWrite(EXTERNAL_SWITCH_RELAY_PIN, HIGH);
#endif
    pinMode(EXTERNAL_SWITCH_RELAY_PIN, OUTPUT);
    selected_source = CMT_SOURCE_INTERNAL;
}

void external_switch_set_source(cmt_source_t source)
{
    if (source != CMT_SOURCE_EXTERNAL) source = CMT_SOURCE_INTERNAL;

    if (source == CMT_SOURCE_EXTERNAL)
    {
        /* Settings are reachable only while transport is idle. Keep the
           internal interface benign before handing the connector over. */
        monitor_disable();
        mz_read_set(false);
        mz_sense_set(true);
    }

    external_switch_write_relay(source == CMT_SOURCE_EXTERNAL);
    selected_source = source;
}

cmt_source_t external_switch_get_source(void)
{
    return selected_source;
}
