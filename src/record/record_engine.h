#ifndef SD2CMT2_RECORD_ENGINE_H
#define SD2CMT2_RECORD_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include "../formats/file_format.h"
#include "../play/loader_mode.h"
#include "record_autoname.h"
#include "../ui/record_menu.h"

typedef enum
{
    RECORD_ENGINE_STOPPED = 0,
    RECORD_ENGINE_ARMED,
    RECORD_ENGINE_RECORDING,
    RECORD_ENGINE_PAUSED,
    RECORD_ENGINE_FINALIZING,
    RECORD_ENGINE_FINISHED,
    RECORD_ENGINE_CANCELLED,
    RECORD_ENGINE_ERROR
} record_engine_state_t;

typedef struct
{
    file_format_t format;
    uint32_t wav_sample_rate;
    record_control_mode_t control_mode;
    bool autoname;
} record_engine_config_t;

void record_engine_init(void);
/* directory_path must remain valid while an armed/active session exists. */
bool record_engine_start(const char *directory_path, const record_engine_config_t *config);
void record_engine_service(void);
void record_engine_toggle_pause(void);
void record_engine_request_stop(void);
void record_engine_cancel(void);

record_engine_state_t record_engine_get_state(void);
file_format_t record_engine_get_format(void);
record_control_mode_t record_engine_get_control_mode(void);
const char *record_engine_get_filename(void);
bool record_engine_get_detected_loader_mode(loader_mode_t *loader_mode);
bool record_engine_get_detected_aux_profile(record_autoname_aux_profile_t *profile);
bool record_engine_get_payload_progress_percent(uint8_t *percent);
/* Sanitized decoded MZ title while AUTONAME is enabled, otherwise NULL. */
const char *record_engine_get_live_name(void);
const char *record_engine_get_error_text(void);
bool record_engine_cancelled_file_removed(void);
uint8_t record_engine_get_buffer_fill_percent(void);
uint32_t record_engine_get_elapsed_seconds(void);

/* Returns the configured WAV sample rate, or 0 for LEP/L16/MZF recording. */
uint32_t record_engine_get_wav_sample_rate(void);

/* Returns 'M' for MOTOR pause, 'U' for user pause, or '\0' when not paused. */
char record_engine_get_pause_indicator(void);

#endif
