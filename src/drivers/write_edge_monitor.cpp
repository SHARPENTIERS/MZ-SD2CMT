#include "write_edge_monitor.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "mzio.h"
#include "../record/edge_record_driver.h"

#if !defined(PCINT1_vect)
#error "WRITE edge monitor requires PCINT1 on ATmega2560"
#endif

typedef enum
{
    WRITE_EDGE_MONITOR_OFF = 0,
    WRITE_EDGE_MONITOR_AUTO_ARMED,
    WRITE_EDGE_MONITOR_WATCH,
    WRITE_EDGE_MONITOR_EDGE_CAPTURE
} write_edge_monitor_mode_t;

static volatile uint8_t monitor_mode = WRITE_EDGE_MONITOR_OFF;
static volatile uint8_t previous_level = 0U;
static volatile uint8_t auto_triggered = 0U;
static volatile uint16_t edge_count = 0U;

static void monitor_enable_from_isr(void)
{
    previous_level = mz_write_sample_from_isr();
    PCIFR = _BV(PCIF1);
    PCMSK1 |= _BV(PCINT9);
    PCICR |= _BV(PCIE1);
}

static void monitor_disable_from_isr(void)
{
    PCMSK1 &= (uint8_t)~_BV(PCINT9);
    PCICR &= (uint8_t)~_BV(PCIE1);
    PCIFR = _BV(PCIF1);
}

void write_edge_monitor_init(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        monitor_disable_from_isr();
        monitor_mode = WRITE_EDGE_MONITOR_OFF;
        previous_level = mz_write_sample_from_isr();
        auto_triggered = 0U;
        edge_count = 0U;
    }
}

void write_edge_monitor_arm_auto_trigger(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        auto_triggered = 0U;
        edge_count = 0U;
        monitor_mode = WRITE_EDGE_MONITOR_AUTO_ARMED;
        monitor_enable_from_isr();
    }
}

bool write_edge_monitor_take_auto_trigger(void)
{
    bool result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        result = (auto_triggered != 0U);
        auto_triggered = 0U;
    }
    return result;
}

void write_edge_monitor_begin_watch(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        auto_triggered = 0U;
        monitor_mode = WRITE_EDGE_MONITOR_WATCH;
        monitor_enable_from_isr();
    }
}

void write_edge_monitor_begin_edge_capture(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        auto_triggered = 0U;
        monitor_mode = WRITE_EDGE_MONITOR_EDGE_CAPTURE;
        monitor_enable_from_isr();
    }
}

void write_edge_monitor_stop(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        monitor_disable_from_isr();
        monitor_mode = WRITE_EDGE_MONITOR_OFF;
        auto_triggered = 0U;
    }
}

uint16_t write_edge_monitor_get_edge_count(void)
{
    uint16_t result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        result = edge_count;
    }
    return result;
}

ISR(PCINT1_vect)
{
    uint8_t level = mz_write_sample_from_isr();
    uint8_t mode = monitor_mode;

    if (level == previous_level)
    {
        return;
    }

    previous_level = level;
    edge_count++;

    if (mode == WRITE_EDGE_MONITOR_AUTO_ARMED)
    {
        auto_triggered = 1U;
    }
    else if (mode == WRITE_EDGE_MONITOR_EDGE_CAPTURE)
    {
        edge_record_driver_on_write_edge_from_isr(level);
    }
}
