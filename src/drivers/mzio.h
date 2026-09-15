#ifndef SD2CMT2_MZIO_H
#define SD2CMT2_MZIO_H

#include <stdbool.h>
#include <stdint.h>

#define MZIO_PIN_READ   2U
#define MZIO_PIN_WRITE  15U
#define MZIO_PIN_MOTOR  16U
#define MZIO_PIN_SENSE  18U
#define MZIO_PIN_LED    19U

void mzio_init(void);

void mz_read_set(bool level);
void mz_sense_set(bool level);
void mz_led_set(bool level);

/* Foreground direct helpers for the CMT transports. */
void mz_read_set_fast(bool level);
void mz_sense_set_fast(bool level);

bool mz_read_get(void);
bool mz_sense_get(void);
bool mz_led_get(void);
bool mz_motor_get(void);
bool mz_write_get(void);

/* Fast, ISR-only direct port primitives. */
void mz_read_set_from_isr(uint8_t level);
void mz_read_set_fast_from_isr(uint8_t level);
uint8_t mz_write_sample_from_isr(void);
uint8_t mz_motor_sample_from_isr(void);

uint8_t mzio_read_pin(void);
uint8_t mzio_write_pin(void);

#endif
