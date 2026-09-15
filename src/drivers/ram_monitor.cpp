#include "ram_monitor.h"

#include <Arduino.h>
#include <avr/io.h>
#include <stddef.h>
#include <stdint.h>

#define RAM_MONITOR_FILL_BYTE 0xA5U
#define RAM_MONITOR_STACK_GUARD_BYTES 16U

extern char __heap_start;
extern char *__brkval;

static uintptr_t watermark_start = 0U;
static uintptr_t watermark_end = 0U;
static bool watermark_ready = false;

static uintptr_t ram_monitor_heap_end(void)
{
    return (__brkval == NULL) ?
        (uintptr_t)&__heap_start : (uintptr_t)__brkval;
}

uint16_t ram_monitor_get_free(void)
{
    uintptr_t heap_end = ram_monitor_heap_end();
    uintptr_t stack_pointer = (uintptr_t)SP;

    return (stack_pointer > heap_end) ?
        (uint16_t)(stack_pointer - heap_end) : 0U;
}

void ram_monitor_init(void)
{
    uintptr_t heap_end = ram_monitor_heap_end();
    uintptr_t stack_pointer = (uintptr_t)SP;
    uintptr_t cursor;

    watermark_start = heap_end;
    watermark_end = heap_end;
    watermark_ready = true;

    /* Never paint into the active setup()/interrupt stack frame. */
    if (stack_pointer <=
        (heap_end + (uintptr_t)RAM_MONITOR_STACK_GUARD_BYTES))
    {
        return;
    }

    watermark_end =
        stack_pointer - (uintptr_t)RAM_MONITOR_STACK_GUARD_BYTES;

    for (cursor = watermark_start; cursor < watermark_end; ++cursor)
    {
        *((volatile uint8_t *)cursor) = RAM_MONITOR_FILL_BYTE;
    }
}

uint16_t ram_monitor_get_min_free(void)
{
    uintptr_t current_heap_end;
    uintptr_t cursor;

    if (!watermark_ready)
    {
        return ram_monitor_get_free();
    }

    current_heap_end = ram_monitor_heap_end();
    cursor = (current_heap_end > watermark_start) ?
        current_heap_end : watermark_start;

    if (cursor >= watermark_end)
    {
        return 0U;
    }

    /* Stack grows downward on AVR. The first overwritten byte, scanning up
       from the heap, is therefore the deepest stack watermark reached. */
    while ((cursor < watermark_end) &&
           (*((volatile uint8_t *)cursor) == RAM_MONITOR_FILL_BYTE))
    {
        ++cursor;
    }

    return (uint16_t)(cursor -
        ((current_heap_end > watermark_start) ?
         current_heap_end : watermark_start));
}
