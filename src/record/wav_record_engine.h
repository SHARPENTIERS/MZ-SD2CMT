#ifndef SD2CMT2_WAV_RECORD_ENGINE_H
#define SD2CMT2_WAV_RECORD_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    WAV_RECORD_ENGINE_STOPPED = 0,
    WAV_RECORD_ENGINE_RECORDING,
    WAV_RECORD_ENGINE_PAUSED,
    WAV_RECORD_ENGINE_FINALIZING,
    WAV_RECORD_ENGINE_FINISHED,
    WAV_RECORD_ENGINE_ERROR

} wav_record_engine_state_t;

void wav_record_engine_init(void);

/*
    Starts the next shared RECxxxx.WAV in directory_path. The sequence is shared
    with RECxxxx.LEP and RECxxxx.L16.

    During RECORD the file is already a normal PCM WAV stream. Timer1 packs
    D15 samples only inside the realtime FIFO; foreground code immediately
    expands every packed 64-byte group to a 512-byte PCM sector (LOW=20,
    HIGH=235) and appends it to the WAV file. No TMP file and no post-stop
    conversion is used.
*/
/* Finds the next RECxxxx.WAV name without creating a file. */
bool wav_record_engine_preview_filename(const char *directory_path);

bool wav_record_engine_start(const char *directory_path,
                             uint32_t sample_rate,
                             bool track_auto_activity);

bool wav_record_engine_pause(void);
bool wav_record_engine_resume(void);
void wav_record_engine_request_stop(void);
void wav_record_engine_cancel(void);

/* Foreground only. Call frequently while RECORD is active. */
void wav_record_engine_service(void);

wav_record_engine_state_t wav_record_engine_get_state(void);

const char *wav_record_engine_get_filename(void);
const char *wav_record_engine_get_full_path(void);
const char *wav_record_engine_get_error_text(void);

uint32_t wav_record_engine_get_sample_rate(void);
/* AUTO WAV idle tracking is derived from packed samples in foreground. */
uint16_t wav_record_engine_get_idle_packed_bytes(void);
void wav_record_engine_reset_idle_activity(void);
uint32_t wav_record_engine_get_captured_samples(void);
uint8_t wav_record_engine_get_buffer_fill_percent(void);
uint8_t wav_record_engine_get_buffer_headroom_percent(void);
uint8_t wav_record_engine_get_write_pin(void);

#endif
