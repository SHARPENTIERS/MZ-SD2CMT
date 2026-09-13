#ifndef SD2CMT2_WRITE_EDGE_MONITOR_H
#define SD2CMT2_WRITE_EDGE_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

/*
   Single owner of PCINT1 / D15 (PJ0).  It can either:
   - arm AUTO RECORD and latch the first real WRITE transition,
   - count WRITE transitions during AUTO WAV record,
   - forward transitions to the LEP/L16 edge recorder.
*/
void write_edge_monitor_init(void);
void write_edge_monitor_arm_auto_trigger(void);
bool write_edge_monitor_take_auto_trigger(void);
void write_edge_monitor_begin_watch(void);
void write_edge_monitor_begin_edge_capture(void);
void write_edge_monitor_stop(void);
uint16_t write_edge_monitor_get_edge_count(void);

#endif
