#include "lcd.h"

#include <Arduino.h>
#include <LiquidCrystal.h>
#include <avr/pgmspace.h>

#define LCD_RS 8
#define LCD_EN 9

#define LCD_D4 4
#define LCD_D5 5
#define LCD_D6 6
#define LCD_D7 7

static LiquidCrystal lcd(
    LCD_RS,
    LCD_EN,
    LCD_D4,
    LCD_D5,
    LCD_D6,
    LCD_D7
);

static uint8_t vertical_bar_level = 0xFFU;

void lcd_init(void)
{
    lcd.begin(16, 2);
    lcd.clear();
    vertical_bar_level = 0xFFU;
}

void lcd_clear(void)
{
    lcd.clear();
}

void lcd_home(void)
{
    lcd.home();
}

void lcd_set_cursor(uint8_t col, uint8_t row)
{
    lcd.setCursor(col, row);
}

void lcd_print(const char *text)
{
    lcd.print(text);
}

void lcd_print_P(PGM_P text)
{
    if (text == NULL) return;
    for (;;)
    {
        char character = (char)pgm_read_byte(text++);
        if (character == '\0') break;
        lcd.write((uint8_t)character);
    }
}

void lcd_set_vertical_bar(uint8_t percent)
{
    uint8_t level;
    uint8_t bitmap[8];

    if (percent > 100U) percent = 100U;
    level = (uint8_t)(((uint16_t)percent * 8U + 50U) / 100U);
    if (level == vertical_bar_level) return;

    for (uint8_t row = 0U; row < 8U; ++row)
        bitmap[row] = (row >= (uint8_t)(8U - level)) ? 0x1FU : 0x00U;
    lcd.createChar(LCD_VERTICAL_BAR_CHAR, bitmap);
    vertical_bar_level = level;
}
