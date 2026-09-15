#ifndef SD2CMT2_RAM_MONITOR_H
#define SD2CMT2_RAM_MONITOR_H

#include <stdint.h>

/* Mark currently unused SRAM. Call once near the start of setup(). */
void ram_monitor_init(void);

/* Current stack-to-heap free gap in bytes. */
uint16_t ram_monitor_get_free(void);

/* Lowest free-SRAM watermark observed since ram_monitor_init().
   A small stack guard makes the reported minimum conservative. */
uint16_t ram_monitor_get_min_free(void);

#endif
