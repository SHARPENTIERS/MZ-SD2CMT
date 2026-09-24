#pragma once

#include <Arduino.h>
#include "../formats/file_format.h"
#include "../ui/menu.h"

typedef enum
{
    PLAY_ENGINE_STATE_STOPPED = 0,
    PLAY_ENGINE_STATE_READY,
    PLAY_ENGINE_STATE_RUNNING,
    PLAY_ENGINE_STATE_PAUSED,
    PLAY_ENGINE_STATE_ERROR
} play_engine_state_t;

typedef enum
{
    PLAY_PROGRESS_PHASE_NORMAL = 0,
    PLAY_PROGRESS_PHASE_ULTRAFAST_LOADER_LOW,
    PLAY_PROGRESS_PHASE_ULTRAFAST_LOADER_HIGH,
    PLAY_PROGRESS_PHASE_ULTRAFAST_HEADER,
    PLAY_PROGRESS_PHASE_MZ700_UL_LOW,
    PLAY_PROGRESS_PHASE_MZ700_UL_HIGH,
    PLAY_PROGRESS_PHASE_IC_TURBO_LOADER,
    PLAY_PROGRESS_PHASE_IC_TURBO_DATA,
    PLAY_PROGRESS_PHASE_TC_TURBO_LOADER,
    PLAY_PROGRESS_PHASE_TC_TURBO_DATA,
    PLAY_PROGRESS_PHASE_MZ700_FAST3_LOW,
    PLAY_PROGRESS_PHASE_MZ700_FAST3_HIGH,
    PLAY_PROGRESS_PHASE_ULTRAFAST_DATA
} play_progress_phase_t;

typedef struct
{
    /* Must remain valid and unchanged for the prepared Play session. */
    const char *full_path;
    file_format_t format;
    bool invert_signal;
    loader_mode_t loader_mode;
    /* MOTOR lets the Sharp backend honor physical MOTOR boundaries.
       MANUAL keeps boundary continuation independent of the MOTOR input. */
    play_control_mode_t control_mode;
    /* 1-based MZT record to prepare; ignored for other formats. */
    uint16_t mzt_record_index;
} play_engine_config_t;

void play_engine_init(void);
bool play_engine_prepare(const play_engine_config_t *config);
bool play_engine_start(void);
bool play_engine_pause(void);
bool play_engine_resume(void);
void play_engine_stop(void);
/* Stop all PLAY backends immediately after a physical SD-card removal. */
void play_engine_abort_media_removed(void);

/* Foreground only: one bounded SD refill and normal EOF/error synchronization. */
void play_engine_service(void);

play_engine_state_t play_engine_get_state(void);
const char *play_engine_get_error_text(void);
uint8_t play_engine_get_output_pin(void);

/* Active transport time of the current logical record excludes pauses. */
uint32_t play_engine_get_elapsed_ms(void);
uint32_t play_engine_get_total_duration_ms(void);

/* Byte-based progress for LEP/L16/TAP and handshaked loader phases. */
uint8_t play_engine_get_progress_percent(void);
play_progress_phase_t play_engine_get_progress_phase(void);
bool play_engine_is_ul_loader_active(void);

uint16_t play_engine_get_mzt_record_index(void);
/* Last prepared MZT record survives backend stop/eject; no extra SRAM copy. */
uint16_t play_engine_get_prepared_mzt_record_index(void);
uint16_t play_engine_get_mzt_record_count(void);
const char *play_engine_get_mzt_record_title(void);
loader_mode_t play_engine_get_mzt_record_loader_mode(void);
/* True only when the active MFI/MTI record was successfully parsed and used. */
bool play_engine_get_loader_from_info_sidecar(void);

/* Current prepared FIFO fill, as 0..100 %. */
uint8_t play_engine_get_buffer_fill_percent(void);

/* Software jitter statistics are disabled in the sample ISR. */
uint16_t play_engine_get_jitter_ticks(void);
