#ifndef SD2CMT2_WAV_RECORD_DRIVER_H
#define SD2CMT2_WAV_RECORD_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

/*
    Fixed-rate direct sampling of MZ WRITE on Arduino Mega 2560 D15 / PJ0.

    Timer1 provides the sample clock. Each ISR reads exactly one digital WRITE
    level and packs eight chronological levels into one byte for the recording
    FIFO. No ADC, SD access, WAV conversion or filesystem action happens in
    the ISR.
*/
typedef enum
{
    WAV_RECORD_DRIVER_STOPPED = 0,
    WAV_RECORD_DRIVER_READY,
    WAV_RECORD_DRIVER_RUNNING,
    WAV_RECORD_DRIVER_PAUSED,
    WAV_RECORD_DRIVER_OVERRUN,
    WAV_RECORD_DRIVER_BAD_RATE,
    WAV_RECORD_DRIVER_BAD_ARGUMENT

} wav_record_driver_state_t;

void wav_record_driver_init(void);

bool wav_record_driver_prepare(uint32_t sample_rate);

bool wav_record_driver_start(void);
bool wav_record_driver_pause(void);
bool wav_record_driver_resume(void);

/*
    Stops Timer1 and preserves a final 1..7 sample partial byte for the
    foreground writer. Call before WAV header finalization.
*/
void wav_record_driver_stop(void);

wav_record_driver_state_t wav_record_driver_get_state(void);

uint32_t wav_record_driver_get_sample_rate(void);

uint8_t wav_record_driver_get_write_pin(void);

/* Samples accumulated in the not-yet-published byte. */
uint8_t wav_record_driver_get_pending_sample_count(void);

/*
    Returns the final partial packed byte exactly once after stop.
    valid_bits is 1..7. A full final byte has already entered the FIFO.
*/
bool wav_record_driver_take_tail(uint8_t *packed_byte,
                                 uint8_t *valid_bits);

#endif
