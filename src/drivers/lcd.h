#ifndef SD2CMT2_LCD_H
#define SD2CMT2_LCD_H

#include <stdint.h>
#include <avr/pgmspace.h>

#define LCD_VERTICAL_BAR_CHAR 1U

void lcd_init(void);
void lcd_clear(void);
void lcd_home(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_print(const char *text);
void lcd_print_P(PGM_P text);
/* Redefines one CGRAM cell as an eight-level bottom-up vertical bar. */
void lcd_set_vertical_bar(uint8_t percent);

#endif
