#ifndef SD2CMT2_WAV_PLAYBACK_DRIVER_H
#define SD2CMT2_WAV_PLAYBACK_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

/*
    Fixed-rate WAV playback for Arduino Mega 2560.

    READ is Arduino D2 = PE4. Timer3 Compare-A provides the sample clock;
    the ISR writes PE4 directly. This is intentionally a reliability fallback
    for the OC3B hardware-output path: Timer1 remains entirely free for
    WAV RECORD, and the source sample rate is preserved sample-by-sample.

    There is no timing marker in this version. D2 is the only playback signal.
*/
#ifndef WAV_PLAYBACK_TIMING_MARKER_ENABLE
#define WAV_PLAYBACK_TIMING_MARKER_ENABLE 0
#endif

#define WAV_PLAYBACK_TIMING_MARKER_PIN 0U

typedef enum
{
    WAV_PLAYBACK_DRIVER_STOPPED = 0,
    WAV_PLAYBACK_DRIVER_READY,
    WAV_PLAYBACK_DRIVER_RUNNING,
    WAV_PLAYBACK_DRIVER_PAUSED,
    WAV_PLAYBACK_DRIVER_FINISHED,
    WAV_PLAYBACK_DRIVER_UNDERRUN,
    WAV_PLAYBACK_DRIVER_BAD_RATE,
    WAV_PLAYBACK_DRIVER_BAD_ARGUMENT
} wav_playback_driver_state_t;

void wav_playback_driver_init(void);

bool wav_playback_driver_prepare(uint32_t sample_rate);

bool wav_playback_driver_start(void);
bool wav_playback_driver_pause(void);
bool wav_playback_driver_resume(void);

void wav_playback_driver_stop(void);

wav_playback_driver_state_t wav_playback_driver_get_state(void);
uint32_t wav_playback_driver_get_sample_rate(void);
uint8_t wav_playback_driver_get_read_pin(void);


/* No ISR-latency measurement is performed in this reliability version. */
uint16_t wav_playback_driver_get_jitter_ticks(void);

#endif
