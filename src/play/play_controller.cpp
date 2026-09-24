#include <Arduino.h>
#include <string.h>

#include "play_controller.h"
#include "../record/record_path_buffer.h"
#include "../formats/mzi_sidecar.h"
#include "play_engine.h"
#include "../drivers/mzio.h"
#include "../drivers/sdcard.h"
#include "../drivers/wav_playback_driver.h"

#if (PLAY_CONTROLLER_PATH_MAX != CMT_SESSION_PATH_BUFFER_MAX)
#error "PLAY and RECORD shared path sizes must match"
#endif
#define session_full_path cmt_session_path_buffer
static file_format_t session_format = FILE_FORMAT_UNKNOWN;
static bool session_invert_signal = false;
static loader_mode_t session_loader_mode = LOADER_MODE_NORMAL_1_1;
static play_control_mode_t session_control_mode = PLAY_CONTROL_MOTOR;
static play_controller_state_t session_state = PLAY_CONTROLLER_STATE_READY;

/* MOTOR mode is armed only once when a file is selected. EOF must not loop. */
static bool waiting_for_motor = false;
static bool motor_control_released = false;

/* Front-panel PLAY may explicitly resume a MOTOR pause while MOTOR is still
   LOW.  Keep that override only until the MZ raises MOTOR again; the next LOW
   then belongs to ordinary MOTOR control. */
static bool manual_motor_override = false;

/* Manual pause has priority over an automatic MOTOR release. */
static bool paused_by_motor = false;
static bool paused_by_user = false;

/* MZT opens in a lightweight record selector.  No record list is retained in
   SRAM; every UP/DOWN re-prepares one requested 1-based record. */
static bool mzt_selection_pending = false;
static bool session_has_info_sidecar = false;
static bool session_media_removed = false;

static const char *play_controller_session_filename(void)
{
    const char *last_slash = strrchr(session_full_path, '/');
    return (last_slash == NULL) ? session_full_path : last_slash + 1U;
}

static bool play_controller_build_full_path(const char *directory_path,
                                            const char *filename)
{
    size_t directory_length;
    size_t filename_length;
    bool add_separator;
    size_t output_offset;

    session_full_path[0] = '\0';
    if ((directory_path == NULL) || (filename == NULL) ||
        (directory_path[0] == '\0') || (filename[0] == '\0'))
    {
        return false;
    }

    directory_length = strlen(directory_path);
    filename_length = strlen(filename);
    add_separator = directory_path[directory_length - 1U] != '/';

    if ((directory_length + (add_separator ? 1U : 0U) + filename_length) >=
        sizeof(session_full_path))
    {
        return false;
    }

    memcpy(session_full_path, directory_path, directory_length);
    output_offset = directory_length;
    if (add_separator)
    {
        session_full_path[output_offset++] = '/';
    }
    memcpy(session_full_path + output_offset, filename, filename_length + 1U);
    return true;
}

static bool play_controller_can_start(void)
{
    return (session_format != FILE_FORMAT_UNKNOWN) && !session_media_removed;
}

static void play_controller_refresh_info_sidecar(void)
{
    session_has_info_sidecar = false;
    if (session_format == FILE_FORMAT_MZF)
    {
        session_has_info_sidecar =
            mzi_sidecar_exists_for_mzf(session_full_path);
    }
    else if (session_format == FILE_FORMAT_MZT)
    {
        session_has_info_sidecar =
            mzi_sidecar_exists_for_mzt(session_full_path);
    }
}

static void play_controller_clear_pause_reason(void)
{
    paused_by_motor = false;
    paused_by_user = false;
}


static bool play_controller_prepare_engine(uint16_t mzt_record_index)
{
    play_engine_config_t config;

    config.full_path = session_full_path;
    config.format = session_format;
    config.invert_signal = session_invert_signal;
    config.loader_mode = session_loader_mode;
    config.control_mode = session_control_mode;
    config.mzt_record_index = mzt_record_index;

    if (!play_engine_prepare(&config))
    {
        session_state = PLAY_CONTROLLER_STATE_ERROR;
        return false;
    }

    return true;
}

static bool play_controller_start_engine(void)
{
    if (!play_engine_start())
    {
        session_state = PLAY_CONTROLLER_STATE_ERROR;
        return false;
    }
    session_state = PLAY_CONTROLLER_STATE_PLAYING;
    manual_motor_override = false;
    play_controller_clear_pause_reason();
    motor_control_released = false;
    if ((session_control_mode == PLAY_CONTROL_MOTOR) &&
        play_engine_is_ul_loader_active())
    {
        motor_control_released = true;
    }
    return true;
}

static bool play_controller_pause_engine(void)
{
    if (!play_engine_pause())
    {
        session_state = PLAY_CONTROLLER_STATE_ERROR;
        return false;
    }
    if (play_engine_get_state() == PLAY_ENGINE_STATE_READY)
    {
        session_state = PLAY_CONTROLLER_STATE_READY;
        waiting_for_motor = false;
        manual_motor_override = false;
        play_controller_clear_pause_reason();
        return true;
    }

    session_state = PLAY_CONTROLLER_STATE_PAUSED;
    return true;
}

static bool play_controller_resume_engine(void)
{
    if (!play_engine_resume())
    {
        session_state = PLAY_CONTROLLER_STATE_ERROR;
        return false;
    }
    session_state = PLAY_CONTROLLER_STATE_PLAYING;
    play_controller_clear_pause_reason();
    motor_control_released = false;
    if ((session_control_mode == PLAY_CONTROL_MOTOR) &&
        play_engine_is_ul_loader_active())
    {
        motor_control_released = true;
    }
    return true;
}

static void play_controller_pause_for_motor(void)
{
    manual_motor_override = false;
    if (play_controller_pause_engine())
    {
        paused_by_user = false;
        paused_by_motor = (session_state == PLAY_CONTROLLER_STATE_PAUSED);
    }
}

static void play_controller_pause_for_user(void)
{
    if (play_controller_pause_engine())
    {
        paused_by_motor = false;
        paused_by_user = (session_state == PLAY_CONTROLLER_STATE_PAUSED);
    }
}

/* Front-panel PLAY is an explicit user override.  If the pause came from
   MOTOR LOW, resume immediately even while MOTOR remains LOW.  The override
   lasts only until the first observed MOTOR HIGH; after that, the next LOW
   again pauses through the normal MOTOR path. */
static void play_controller_resume_from_user(void)
{
    bool motor_override_requested = false;

    paused_by_user = false;

    if ((session_control_mode == PLAY_CONTROL_MOTOR) &&
        !motor_control_released && !mz_motor_get())
    {
        manual_motor_override = true;
        motor_override_requested = true;
    }

    paused_by_motor = false;
    if (!play_controller_resume_engine() && motor_override_requested)
    {
        manual_motor_override = false;
    }
}

void play_controller_init(void)
{
    session_full_path[0] = '\0';
    session_format = FILE_FORMAT_UNKNOWN;
    session_invert_signal = false;
    session_loader_mode = LOADER_MODE_NORMAL_1_1;
    session_control_mode = PLAY_CONTROL_MOTOR;
    session_state = PLAY_CONTROLLER_STATE_READY;
    waiting_for_motor = false;
    motor_control_released = false;
    manual_motor_override = false;
    mzt_selection_pending = false;
    session_has_info_sidecar = false;
    session_media_removed = false;
    play_controller_clear_pause_reason();
    play_engine_init();
}

void play_controller_start_session(const char *filename,
                                   const char *directory_path,
                                   bool invert_signal,
                                   loader_mode_t loader_mode,
                                   play_control_mode_t control_mode)
{
    if (!play_controller_build_full_path(directory_path, filename))
    {
        session_format = FILE_FORMAT_UNKNOWN;
        session_state = PLAY_CONTROLLER_STATE_READY;
        waiting_for_motor = false;
        motor_control_released = false;
        manual_motor_override = false;
        mzt_selection_pending = false;
        session_has_info_sidecar = false;
        session_media_removed = false;
        play_controller_clear_pause_reason();
        play_engine_stop();
        return;
    }

    session_format = file_format_detect_from_name(filename);
    session_invert_signal = invert_signal;
    session_loader_mode = loader_mode;
    session_control_mode = control_mode;
    session_state = PLAY_CONTROLLER_STATE_READY;
    waiting_for_motor = false;
    motor_control_released = false;
    manual_motor_override = false;
    mzt_selection_pending = false;
    session_has_info_sidecar = false;
    session_media_removed = false;
    play_controller_clear_pause_reason();

    if (session_format == FILE_FORMAT_UNKNOWN)
    {
        play_engine_stop();
        return;
    }

    play_controller_refresh_info_sidecar();

    if (!play_controller_prepare_engine(1U))
    {
        return;
    }

    if (session_format == FILE_FORMAT_MZT)
    {
        /* Give the user a chance to pick any logical record before arming the
           tape transport. This also prevents a high MOTOR from immediately
           starting record 1 while the selector is visible. */
        mzt_selection_pending = true;
        mz_sense_set(true);
        return;
    }

    if (session_control_mode == PLAY_CONTROL_MOTOR)
    {
        /* Signal that the CMT session is ready while awaiting MOTOR HIGH. */
        waiting_for_motor = true;
        mz_sense_set(false);
    }
    else
    {
        /* MANUAL begins immediately; SELECT is then only pause/resume. */
        (void)play_controller_start_engine();
    }
}

bool play_controller_select_mzt_record(int8_t direction)
{
    uint16_t current;
    uint16_t count;
    uint16_t target;

    if ((session_format != FILE_FORMAT_MZT) || !mzt_selection_pending ||
        (session_state != PLAY_CONTROLLER_STATE_READY) ||
        (direction == 0))
    {
        return false;
    }

    current = play_engine_get_mzt_record_index();
    count = play_engine_get_mzt_record_count();
    if ((current == 0U) || (count == 0U)) return false;

    if (direction < 0)
    {
        target = (current <= 1U) ? count : (uint16_t)(current - 1U);
    }
    else
    {
        target = (current >= count) ? 1U : (uint16_t)(current + 1U);
    }

    if (!play_controller_prepare_engine(target))
    {
        return false;
    }

    waiting_for_motor = false;
    motor_control_released = false;
    manual_motor_override = false;
    play_controller_clear_pause_reason();
    session_state = PLAY_CONTROLLER_STATE_READY;
    mz_sense_set(true);
    return true;
}

void play_controller_toggle_play_pause(void)
{
    if (!play_controller_can_start()) return;

    switch (session_state)
    {
        case PLAY_CONTROLLER_STATE_READY:
            if (mzt_selection_pending)
            {
                mzt_selection_pending = false;
                if (session_control_mode == PLAY_CONTROL_MOTOR)
                {
                    mz_sense_set(false);
                    if (!mz_motor_get())
                    {
                        waiting_for_motor = true;
                        return;
                    }
                }
                waiting_for_motor = false;
                (void)play_controller_start_engine();
                return;
            }

            if ((session_control_mode == PLAY_CONTROL_MOTOR) &&
                !motor_control_released && !mz_motor_get())
            {
                waiting_for_motor = true;
                mz_sense_set(false);
                return;
            }

            waiting_for_motor = false;
            (void)play_controller_start_engine();
            break;

        case PLAY_CONTROLLER_STATE_PLAYING:
            play_controller_pause_for_user();
            break;

        case PLAY_CONTROLLER_STATE_PAUSED:
            play_controller_resume_from_user();
            break;

        case PLAY_CONTROLLER_STATE_ERROR:
        default:
            play_engine_stop();
            session_state = PLAY_CONTROLLER_STATE_READY;
            waiting_for_motor = false;
            motor_control_released = false;
            manual_motor_override = false;
            mzt_selection_pending = (session_format == FILE_FORMAT_MZT);
            play_controller_clear_pause_reason();
            mz_sense_set(true);
            break;
    }
}

void play_controller_stop(void)
{
    play_engine_stop();
    mz_sense_set(true);
    session_state = PLAY_CONTROLLER_STATE_READY;
    waiting_for_motor = false;
    motor_control_released = false;
    manual_motor_override = false;
    mzt_selection_pending = false;
    session_media_removed = false;
    play_controller_clear_pause_reason();
}

bool play_controller_stop_to_mzt_selector(void)
{
    uint16_t record_index;

    /*
        MZT has a two-level STOP/BACK hierarchy:
          active MZT transport -> MZT mini-browser
          MZT mini-browser     -> main file browser

        Returning false while the selector is already visible lets the normal
        PLAY_SCREEN_ACTION_BACK path perform the second step.
    */
    if ((session_format != FILE_FORMAT_MZT) ||
        mzt_selection_pending ||
        session_media_removed)
    {
        /* Keep media-removal state intact so reinsertion recovery or the
           normal BACK path can resolve the session consistently. */
        return false;
    }

    /*
        Capture the logical record BEFORE stopping because play_engine_stop()
        clears the active-record index. prepared_mzt_record_index is kept as a
        safe fallback and is continuously synchronized while MZT runs.
    */
    record_index = play_engine_get_mzt_record_index();
    if (record_index == 0U)
    {
        record_index = play_engine_get_prepared_mzt_record_index();
    }
    if (record_index == 0U)
    {
        record_index = 1U;
    }

    play_engine_stop();
    mz_sense_set(true);
    waiting_for_motor = false;
    motor_control_released = false;
    manual_motor_override = false;
    play_controller_clear_pause_reason();

    /*
        Re-prepare the interrupted/current record so the mini-browser has the
        normal title/count/effective-loader information and can immediately
        navigate with UP/DOWN.
    */
    if (!play_controller_prepare_engine(record_index))
    {
        /* The transport is already stopped. Returning false delegates the
           second-level exit to the normal BACK handler. */
        mzt_selection_pending = false;
        return false;
    }

    session_state = PLAY_CONTROLLER_STATE_READY;
    mzt_selection_pending = true;
    mz_sense_set(true);
    return true;
}

static bool play_controller_recover_inserted_media(void)
{
    uint16_t record_index = 1U;

    if (!sdcard_reinitialize())
    {
        /* The switch says inserted, but the card did not initialize.  Keep
           the session in a safe error state; a fresh eject/insert can retry. */
        play_engine_abort_media_removed();
        session_state = PLAY_CONTROLLER_STATE_ERROR;
        return false;
    }

    session_media_removed = false;
    waiting_for_motor = false;
    motor_control_released = false;
    manual_motor_override = false;
    mzt_selection_pending = false;
    play_controller_clear_pause_reason();
    play_controller_refresh_info_sidecar();

    if (session_format == FILE_FORMAT_MZT)
    {
        record_index = play_engine_get_prepared_mzt_record_index();
        if (record_index == 0U) record_index = 1U;
    }

    if (!play_controller_prepare_engine(record_index))
    {
        return false;
    }

    session_state = PLAY_CONTROLLER_STATE_READY;
    if (session_format == FILE_FORMAT_MZT)
    {
        /* Return to the MZT selector at the interrupted record.  Do not
           automatically resume an interrupted tape stream after media swap. */
        mzt_selection_pending = true;
        mz_sense_set(true);
    }
    else if (session_control_mode == PLAY_CONTROL_MOTOR)
    {
        waiting_for_motor = true;
        mz_sense_set(false);
    }
    else
    {
        /* MANUAL recovery is deliberately READY: the user explicitly starts
           playback again instead of an insert event restarting the tape. */
        mz_sense_set(true);
    }
    return true;
}

void play_controller_service(void)
{
    play_engine_state_t engine_state;

    /* The detect driver rate-limits the physical pin read to 100 Hz, so this
       remains cheap even though PLAY has a tight realtime service loop. */
    if (session_media_removed)
    {
        if (sdcard_detect_consume_inserted_edge())
        {
            (void)play_controller_recover_inserted_media();
        }
        return;
    }

    if (sdcard_detect_removed_edge())
    {
        /* prepared_mzt_record_index is kept aligned with the active MZT
           record and is reused for recovery, avoiding a second SRAM index. */
        session_media_removed = true;
        waiting_for_motor = false;
        motor_control_released = false;
        manual_motor_override = false;
        mzt_selection_pending = false;
        play_controller_clear_pause_reason();
        play_engine_abort_media_removed();
        session_state = PLAY_CONTROLLER_STATE_ERROR;
        return;
    }

    play_engine_service();
    engine_state = play_engine_get_state();

    switch (engine_state)
    {
        case PLAY_ENGINE_STATE_RUNNING:
            session_state = PLAY_CONTROLLER_STATE_PLAYING;
            play_controller_clear_pause_reason();
            /* MZT may change loader without restarting the engine, so MOTOR
               ownership follows the loader of the active record. */
            if ((session_control_mode == PLAY_CONTROL_MOTOR) &&
                (session_format == FILE_FORMAT_MZT))
            {
                motor_control_released = play_engine_is_ul_loader_active();
                if (motor_control_released)
                {
                    waiting_for_motor = false;
                    manual_motor_override = false;
                }
            }
            break;

        case PLAY_ENGINE_STATE_PAUSED:
            session_state = PLAY_CONTROLLER_STATE_PAUSED;
            break;

        case PLAY_ENGINE_STATE_ERROR:
            session_state = PLAY_CONTROLLER_STATE_ERROR;
            waiting_for_motor = false;
            motor_control_released = false;
            manual_motor_override = false;
            play_controller_clear_pause_reason();
            break;

        case PLAY_ENGINE_STATE_READY:
        case PLAY_ENGINE_STATE_STOPPED:
        default:
            if ((session_state == PLAY_CONTROLLER_STATE_PLAYING) ||
                (session_state == PLAY_CONTROLLER_STATE_PAUSED))
            {
                session_state = PLAY_CONTROLLER_STATE_READY;
                waiting_for_motor = false;
                motor_control_released = false;
            manual_motor_override = false;
                play_controller_clear_pause_reason();
                if (session_format == FILE_FORMAT_MZT)
                {
                    /* End of the selected MZT run returns to the record chooser
                       instead of silently restarting record 1. */
                    mzt_selection_pending = true;
                    mz_sense_set(true);
                }
            }
            break;
    }

    if ((session_control_mode == PLAY_CONTROL_MOTOR) &&
        manual_motor_override && mz_motor_get())
    {
        /* The user override has done its job. HIGH re-arms ordinary MOTOR
           control without changing the currently running transport. */
        manual_motor_override = false;
    }

    if (mzt_selection_pending ||
        (session_control_mode != PLAY_CONTROL_MOTOR) ||
        (session_state == PLAY_CONTROLLER_STATE_ERROR) ||
        motor_control_released ||
        manual_motor_override)
    {
        return;
    }

    if (waiting_for_motor)
    {
        if (mz_motor_get() && (play_engine_get_state() == PLAY_ENGINE_STATE_READY))
        {
            if (play_controller_start_engine()) waiting_for_motor = false;
        }
        return;
    }

    if ((session_state == PLAY_CONTROLLER_STATE_PLAYING) && !mz_motor_get())
    {
        play_controller_pause_for_motor();
    }
    else if (session_state == PLAY_CONTROLLER_STATE_PAUSED)
    {
        if (paused_by_user)
        {
            return;
        }

        if (!mz_motor_get())
        {
            paused_by_motor = true;
            return;
        }

        if (paused_by_motor)
        {
            paused_by_motor = false;
            (void)play_controller_resume_engine();
        }
    }
}

void play_controller_get_view(play_controller_view_t *view)
{
    bool timed_loader_progress;

    if (view == NULL) return;

    view->filename = play_controller_session_filename();
    view->full_path = session_full_path;
    view->format = session_format;
    view->invert_signal = session_invert_signal;
    view->loader_mode = session_loader_mode;
    view->control_mode = session_control_mode;
    view->waiting_for_motor = waiting_for_motor;
    view->paused_by_motor = paused_by_motor;
    view->paused_by_user = paused_by_user;
    view->state = session_state;
    view->error_text = play_engine_get_error_text();
    view->elapsed_ms = play_engine_get_elapsed_ms();
    view->total_duration_ms = play_engine_get_total_duration_ms();
    view->progress_phase = play_engine_get_progress_phase();
    timed_loader_progress =
        (view->progress_phase == PLAY_PROGRESS_PHASE_IC_TURBO_LOADER) ||
        (view->progress_phase == PLAY_PROGRESS_PHASE_IC_TURBO_DATA) ||
        (view->progress_phase == PLAY_PROGRESS_PHASE_TC_TURBO_LOADER) ||
        (view->progress_phase == PLAY_PROGRESS_PHASE_TC_TURBO_DATA) ||
        (view->progress_phase == PLAY_PROGRESS_PHASE_MZ700_FAST3_LOW) ||
        (view->progress_phase == PLAY_PROGRESS_PHASE_MZ700_FAST3_HIGH);
    view->progress_is_percent = (session_format == FILE_FORMAT_LEP) ||
                                (session_format == FILE_FORMAT_L16) ||
                                (session_format == FILE_FORMAT_TAP) ||
                                ((view->progress_phase != PLAY_PROGRESS_PHASE_NORMAL) &&
                                 !timed_loader_progress);
    view->progress_percent = play_engine_get_progress_percent();
    view->buffer_fill_percent = play_engine_get_buffer_fill_percent();
    view->has_info_sidecar = session_has_info_sidecar;
    view->loader_from_info_sidecar = play_engine_get_loader_from_info_sidecar();
    view->mzt_selection_pending = mzt_selection_pending;
    view->mzt_record_index = play_engine_get_mzt_record_index();
    view->mzt_record_count = play_engine_get_mzt_record_count();
    view->mzt_record_title = play_engine_get_mzt_record_title();
    view->mzt_record_loader_mode = play_engine_get_mzt_record_loader_mode();
}

const char* play_controller_get_filename(void) { return play_controller_session_filename(); }
const char* play_controller_get_full_path(void) { return session_full_path; }
file_format_t play_controller_get_format(void) { return session_format; }
bool play_controller_get_invert_signal(void) { return session_invert_signal; }
loader_mode_t play_controller_get_loader_mode(void) { return session_loader_mode; }
play_control_mode_t play_controller_get_control_mode(void) { return session_control_mode; }
play_controller_state_t play_controller_get_state(void) { return session_state; }
