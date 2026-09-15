#ifndef SD2CMT2_BACKLIGHT_H
#define SD2CMT2_BACKLIGHT_H

#include <stdint.h>

/* ATmega2560 physical pin 23 = PB4/OC2A = Arduino Mega D10. */
#define LCD_BACKLIGHT_PIN 10U
#define LCD_BACKLIGHT_MIN_PERCENT 10U
#define LCD_BACKLIGHT_DEFAULT_PERCENT 80U

void backlight_init(void);
void backlight_set_percent(uint8_t percent);
uint8_t backlight_get_percent(void);

#endif
