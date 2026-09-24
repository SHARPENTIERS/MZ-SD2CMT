#include "backlight.h"

#include <Arduino.h>
#include <avr/io.h>

static uint8_t backlight_percent = LCD_BACKLIGHT_DEFAULT_PERCENT;

void backlight_set_percent(uint8_t percent)
{
    if (percent < LCD_BACKLIGHT_MIN_PERCENT)
        percent = LCD_BACKLIGHT_MIN_PERCENT;
    if (percent > 100U) percent = 100U;
    backlight_percent = percent;

    if (percent == 100U)
    {
        TCCR2A &= (uint8_t)~(_BV(COM2A1) | _BV(COM2A0));
        PORTB |= _BV(PB4);
        return;
    }

    OCR2A = (uint8_t)(((uint16_t)percent * 255U + 50U) / 100U);
    TCCR2A = (uint8_t)(_BV(COM2A1) | _BV(WGM21) | _BV(WGM20));
}

void backlight_init(void)
{
    /* Fast PWM, TOP=0xff, no prescaler: 16 MHz / 256 = 62.5 kHz.
       Timer2 generates OC2A entirely in hardware and uses no ISR. */
    TCCR2A = 0U;
    TCCR2B = 0U;
    TIMSK2 = 0U;
    TCNT2 = 0U;
    DDRB |= _BV(DDB4);
    TCCR2A = (uint8_t)(_BV(WGM21) | _BV(WGM20));
    TCCR2B = _BV(CS20);
    backlight_set_percent(LCD_BACKLIGHT_DEFAULT_PERCENT);
}

uint8_t backlight_get_percent(void)
{
    return backlight_percent;
}
