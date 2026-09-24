#include "mzio.h"

#include <Arduino.h>
#include <avr/io.h>

static volatile uint8_t mz_read_level = 0U;
static volatile uint8_t mz_sense_level = 1U;
static volatile uint8_t mz_led_level = 0U;

void mzio_init(void)
{
    /* WRITE is driven by the MZ; do not bias it. */
    pinMode(MZIO_PIN_WRITE, INPUT);
    digitalWrite(MZIO_PIN_WRITE, LOW);

    /* MOTOR defaults HIGH when the MZ/control cable is absent. */
    pinMode(MZIO_PIN_MOTOR, INPUT_PULLUP);

    pinMode(MZIO_PIN_READ, OUTPUT);
    pinMode(MZIO_PIN_SENSE, OUTPUT);
    pinMode(MZIO_PIN_LED, OUTPUT);

    mz_read_level = 0U;
    mz_sense_level = 1U;
    mz_led_level = 0U;

    digitalWrite(MZIO_PIN_READ, LOW);
    digitalWrite(MZIO_PIN_SENSE, HIGH);
    digitalWrite(MZIO_PIN_LED, LOW);
}

void mz_read_set(bool level)
{
    mz_read_level = level ? 1U : 0U;
    digitalWrite(MZIO_PIN_READ, level ? HIGH : LOW);
}

void mz_sense_set(bool level)
{
    mz_sense_level = level ? 1U : 0U;
    digitalWrite(MZIO_PIN_SENSE, level ? HIGH : LOW);
}

void mz_read_set_fast(bool level)
{
    mz_read_set_from_isr(level ? 1U : 0U);
}

void mz_sense_set_fast(bool level)
{
    if (level)
    {
        PORTD |= _BV(PD3);
        mz_sense_level = 1U;
    }
    else
    {
        PORTD &= (uint8_t)~_BV(PD3);
        mz_sense_level = 0U;
    }
}

void mz_led_set(bool level)
{
    mz_led_level = level ? 1U : 0U;
    digitalWrite(MZIO_PIN_LED, level ? HIGH : LOW);
}

bool mz_read_get(void) { return mz_read_level != 0U; }
bool mz_sense_get(void) { return mz_sense_level != 0U; }
bool mz_led_get(void) { return mz_led_level != 0U; }
bool mz_motor_get(void) { return (PINH & _BV(PH1)) != 0U; }
bool mz_write_get(void) { return (PINJ & _BV(PJ0)) != 0U; }

void mz_read_set_from_isr(uint8_t level)
{
    if (level & 1U)
    {
        PORTE |= _BV(PE4);
        mz_read_level = 1U;
    }
    else
    {
        PORTE &= (uint8_t)~_BV(PE4);
        mz_read_level = 0U;
    }
}

void mz_read_set_fast_from_isr(uint8_t level)
{
    mz_read_set_from_isr(level);
}

uint8_t mz_write_sample_from_isr(void)
{
    return (PINJ & _BV(PJ0)) ? 1U : 0U;
}

uint8_t mz_motor_sample_from_isr(void)
{
    return (PINH & _BV(PH1)) ? 1U : 0U;
}

uint8_t mzio_read_pin(void) { return MZIO_PIN_READ; }
uint8_t mzio_write_pin(void) { return MZIO_PIN_WRITE; }
