#ifndef SD2CMT2_MZF_PLAYBACK_H
#define SD2CMT2_MZF_PLAYBACK_H

#include <stdbool.h>
#include <stdint.h>

#include "../formats/file_format.h"
#include "loader_mode.h"

typedef enum
{
    MZF_PLAYBACK_STOPPED = 0,
    MZF_PLAYBACK_READY,
    MZF_PLAYBACK_RUNNING,
    MZF_PLAYBACK_PAUSED,
    MZF_PLAYBACK_FINISHED,
    MZF_PLAYBACK_UNDERRUN,
    MZF_PLAYBACK_IO_ERROR,
    MZF_PLAYBACK_BAD_FILE
} mzf_playback_state_t;

typedef enum
{
    MZF_PLAYBACK_PHASE_NORMAL = 0,
    MZF_PLAYBACK_PHASE_ULTRAFAST_LOADER_LOW,
    MZF_PLAYBACK_PHASE_ULTRAFAST_LOADER_HIGH,
    MZF_PLAYBACK_PHASE_ULTRAFAST_HEADER,
    MZF_PLAYBACK_PHASE_MZ700_UL_LOW,
    MZF_PLAYBACK_PHASE_MZ700_UL_HIGH,
    MZF_PLAYBACK_PHASE_IC_TURBO_LOADER,
    MZF_PLAYBACK_PHASE_IC_TURBO_DATA,
    MZF_PLAYBACK_PHASE_TC_TURBO_LOADER,
    MZF_PLAYBACK_PHASE_TC_TURBO_DATA,
    MZF_PLAYBACK_PHASE_MZ700_FAST3_LOW,
    MZF_PLAYBACK_PHASE_MZ700_FAST3_HIGH,
    MZF_PLAYBACK_PHASE_ULTRAFAST_DATA
} mzf_playback_phase_t;

typedef void (*mzf_qd_analysis_progress_callback_t)(uint8_t percent);
typedef bool (*mzf_qd_analysis_cancel_callback_t)(void);

/* Progress callbacks receive this value while a selected physical QD record
   is scanned for duration before playback. Percent values remain 0..100. */
#define MZF_QD_PROGRESS_LOADING 0xFFU

/* Streams Sharp tape images with native framing and selected loader timing.
   mzt_start_record is 1-based and ignored for MZF/M12. */
void mzf_playback_init(void);
void mzf_playback_set_qd_analysis_callbacks(
    mzf_qd_analysis_progress_callback_t progress_callback,
    mzf_qd_analysis_cancel_callback_t cancel_callback);
/* Drop the physical-QD directory retained for fast mini-browser navigation. */
void mzf_playback_invalidate_qd_cache(void);
bool mzf_playback_prepare(const char *path, file_format_t format,
                          loader_mode_t loader_mode,
                          uint16_t mzt_start_record,
                          bool motor_control_enabled);
bool mzf_playback_start(void);
bool mzf_playback_qd_load_was_cancelled(void);
bool mzf_playback_pause(void);
bool mzf_playback_resume(void);
void mzf_playback_stop(void);
void mzf_playback_service(void);

mzf_playback_state_t mzf_playback_get_state(void);
const char *mzf_playback_get_error_text(void);
uint8_t mzf_playback_get_buffer_fill_percent(void);
uint8_t mzf_playback_get_progress_percent(void);
mzf_playback_phase_t mzf_playback_get_progress_phase(void);
bool mzf_playback_is_ul_loader_active(void);

/* MZT record navigation / metadata.  Index is 1-based, zero for non-MZT. */
uint16_t mzf_playback_get_mzt_record_index(void);
uint16_t mzf_playback_get_mzt_record_count(void);
const char *mzf_playback_get_mzt_record_title(void);
/* Effective profile for the current Sharp record, including single MZF/M12. */
loader_mode_t mzf_playback_get_mzt_record_loader_mode(void);
/* True only when the current MZT RECORD=n resolved a valid loader from MTI. */
bool mzf_playback_get_mzt_record_loader_from_sidecar(void);

/* Nominal duration of the current logical record. For MZF/M12 this remains
   the file duration. Handshaked UL MZT records return zero (unknown). */
uint32_t mzf_playback_get_total_duration_ms(void);

/* Internal OC3B PWM phase hook, called only when MZF owns Timer3. */
bool mzf_playback_timer3_compb_from_isr(void);

#endif
