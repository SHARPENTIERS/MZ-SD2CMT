#include "record_engine.h"

#include <Arduino.h>
#include <string.h>

#include "wav_record_engine.h"
#include "edge_record_engine.h"
#include "mzf_record_engine.h"
#include "record_autoname.h"
#include "../drivers/mzio.h"
#include "../drivers/monitor_mute.h"
#include "../drivers/write_edge_monitor.h"
#include "../drivers/flash_text.h"

static record_engine_state_t engine_state = RECORD_ENGINE_STOPPED;
static record_engine_config_t engine_config =
    { FILE_FORMAT_WAV, 44100UL, RECORD_CONTROL_MOTOR, false };
/* The caller owns this stable path while a Record session is active. */
static const char *engine_directory = NULL;
static char engine_error[17];
static bool capture_started = false;

/* Manual pause dominates MOTOR. A user resume clears only this lock; the
   current MOTOR level is then applied again. */
static bool paused_by_motor = false;
static bool paused_by_user = false;

static uint32_t active_started_ms = 0UL;
static uint32_t pause_started_ms = 0UL;
static uint32_t paused_total_ms = 0UL;
static bool cancelled_file_removed = false;
static uint16_t auto_last_activity_counter = 0U;
static uint32_t auto_last_activity_ms = 0UL;
static uint16_t auto_wav_idle_limit_bytes = 0U;
#define AUTO_RECORD_IDLE_STOP_MS 5000UL

static bool is_edge_format(file_format_t format)
{
    return (format == FILE_FORMAT_LEP) || (format == FILE_FORMAT_L16);
}

static uint16_t record_engine_auto_activity_counter(void)
{
    return write_edge_monitor_get_edge_count();
}

static void record_engine_clear_pause_reason(void)
{
    paused_by_motor = false;
    paused_by_user = false;
}

static void record_engine_set_error(const char *text)
{
    if (text == NULL)
    {
        flash_text_copy(engine_error, sizeof(engine_error), PSTR("REC ERROR"));
    }
    else
    {
        strncpy(engine_error, text, sizeof(engine_error) - 1U);
        engine_error[sizeof(engine_error) - 1U] = '\0';
    }
    engine_state = RECORD_ENGINE_ERROR;
    monitor_disable();
    record_engine_clear_pause_reason();
    write_edge_monitor_stop();
    mz_sense_set(true);
}

static void record_engine_set_error_P(PGM_P text)
{
    flash_text_copy(engine_error, sizeof(engine_error), text);
    engine_state = RECORD_ENGINE_ERROR;
    monitor_disable();
    record_engine_clear_pause_reason();
    write_edge_monitor_stop();
    mz_sense_set(true);
}

static bool record_engine_begin_capture(void)
{
    bool ok;

    if (capture_started)
    {
        return true;
    }

    /* AUTO WAV uses PCINT1 only to wake from ARMED.  Once the trigger has been
       consumed, Timer1 becomes the sole realtime observer of WRITE.  Leaving
       the pin-change monitor enabled here would run a second ISR on every tape
       edge and can overflow the WAV FIFO even at Normal 1:1. */
    if ((engine_config.control_mode == RECORD_CONTROL_AUTO) &&
        (engine_config.format == FILE_FORMAT_WAV))
    {
        write_edge_monitor_stop();
    }

    record_autoname_begin(engine_config.autoname, engine_config.format,
                          engine_config.wav_sample_rate);

    if (engine_config.format == FILE_FORMAT_WAV)
    {
        ok = wav_record_engine_start(
            engine_directory, engine_config.wav_sample_rate,
            engine_config.control_mode == RECORD_CONTROL_AUTO);
    }
    else if (is_edge_format(engine_config.format))
    {
        ok = edge_record_engine_start(engine_directory, engine_config.format);
    }
    else if (engine_config.format == FILE_FORMAT_MZF)
    {
        ok = mzf_record_engine_start(engine_directory);
    }
    else
    {
        record_engine_set_error_P(PSTR("REC FORMAT"));
        return false;
    }

    if (!ok)
    {
        const char *error = (engine_config.format == FILE_FORMAT_WAV) ?
            wav_record_engine_get_error_text() :
            (engine_config.format == FILE_FORMAT_MZF) ?
                mzf_record_engine_get_error_text() :
                edge_record_engine_get_error_text();
        record_engine_set_error(error);
        return false;
    }

    capture_started = true;
    engine_state = RECORD_ENGINE_RECORDING;
    record_engine_clear_pause_reason();
    active_started_ms = millis();
    pause_started_ms = 0UL;
    paused_total_ms = 0UL;

    if (engine_config.control_mode == RECORD_CONTROL_AUTO)
    {
        if (engine_config.format == FILE_FORMAT_WAV)
        {
            /* Five seconds expressed directly as packed 1-bit WAV bytes.
               This is computed once, outside the recording hot path. */
            uint32_t limit =
                (((uint32_t)engine_config.wav_sample_rate *
                  (AUTO_RECORD_IDLE_STOP_MS / 1000UL)) + 7UL) / 8UL;
            auto_wav_idle_limit_bytes =
                (limit > 0xFFFFUL) ? 0xFFFFU : (uint16_t)limit;
        }
        else
        {
            auto_last_activity_counter = record_engine_auto_activity_counter();
            auto_last_activity_ms = active_started_ms;
        }
    }

    mz_sense_set(false);
    return true;
}

static bool record_engine_pause_capture(void)
{
    bool ok = false;
    if (!capture_started || (engine_state != RECORD_ENGINE_RECORDING))
    {
        return false;
    }
    if (engine_config.format == FILE_FORMAT_WAV)
    {
        ok = wav_record_engine_pause();
    }
    else
    {
        ok = (engine_config.format == FILE_FORMAT_MZF) ?
            mzf_record_engine_pause() : edge_record_engine_pause();
    }
    if (!ok)
    {
        record_engine_set_error_P(PSTR("REC PAUSE"));
        return false;
    }
    pause_started_ms = millis();
    /* Both record engines keep draining their already captured FIFO while
       paused.  Do not reset AUTONAME here: the end of a NORMAL header can
       still be waiting in that FIFO when the MZ drops MOTOR.  Captured WAV
       samples and edge intervals are intentionally concatenated across a
       pause, so preserving the decoder state matches the saved file. */
    engine_state = RECORD_ENGINE_PAUSED;
    return true;
}

static bool record_engine_resume_capture(void)
{
    bool ok = false;
    if (!capture_started || (engine_state != RECORD_ENGINE_PAUSED))
    {
        return false;
    }
    if (engine_config.format == FILE_FORMAT_WAV)
    {
        ok = wav_record_engine_resume();
    }
    else
    {
        ok = (engine_config.format == FILE_FORMAT_MZF) ?
            mzf_record_engine_resume() : edge_record_engine_resume();
    }
    if (!ok)
    {
        record_engine_set_error_P(PSTR("REC RESUME"));
        return false;
    }
    if (pause_started_ms != 0UL)
    {
        paused_total_ms += millis() - pause_started_ms;
        pause_started_ms = 0UL;
    }
    engine_state = RECORD_ENGINE_RECORDING;
    record_engine_clear_pause_reason();

    /* A user resume in AUTO must start a fresh idle interval; otherwise a long
       manual pause would immediately finalize the recording. */
    if (engine_config.control_mode == RECORD_CONTROL_AUTO)
    {
        if (engine_config.format == FILE_FORMAT_WAV)
        {
            wav_record_engine_reset_idle_activity();
        }
        else
        {
            auto_last_activity_counter = record_engine_auto_activity_counter();
            auto_last_activity_ms = millis();
        }
    }
    return true;
}

static void record_engine_pause_for_motor(void)
{
    if (record_engine_pause_capture())
    {
        paused_by_user = false;
        paused_by_motor = (engine_state == RECORD_ENGINE_PAUSED);
    }
}

static void record_engine_pause_for_user(void)
{
    if (record_engine_pause_capture())
    {
        paused_by_motor = false;
        paused_by_user = (engine_state == RECORD_ENGINE_PAUSED);
    }
}

static void record_engine_resume_from_user(void)
{
    paused_by_user = false;

    if ((engine_config.control_mode == RECORD_CONTROL_MOTOR) && !mz_motor_get())
    {
        paused_by_motor = true;
        return;
    }

    paused_by_motor = false;
    (void)record_engine_resume_capture();
}

void record_engine_init(void)
{
    monitor_disable();
    wav_record_engine_init();
    edge_record_engine_init();
    mzf_record_engine_init();
    engine_state = RECORD_ENGINE_STOPPED;
    engine_config.format = FILE_FORMAT_WAV;
    engine_config.wav_sample_rate = 44100UL;
    engine_config.control_mode = RECORD_CONTROL_MOTOR;
    engine_config.autoname = false;
    engine_directory = NULL;
    engine_error[0] = '\0';
    capture_started = false;
    record_engine_clear_pause_reason();
    active_started_ms = 0UL;
    pause_started_ms = 0UL;
    paused_total_ms = 0UL;
    cancelled_file_removed = false;
    auto_last_activity_counter = 0U;
    auto_last_activity_ms = 0UL;
    auto_wav_idle_limit_bytes = 0U;
    write_edge_monitor_stop();
    mz_sense_set(true);
    record_autoname_begin(false, FILE_FORMAT_UNKNOWN, 0UL);
}

static void record_engine_finish_capture(void)
{
    if (engine_state == RECORD_ENGINE_FINISHED)
    {
        return;
    }

    if (engine_config.autoname)
    {
        /* AUTONAME is cosmetic. A failed rename deliberately leaves the
           completely saved RECxxxx file intact. */
        (void)record_autoname_apply(engine_directory, engine_config.format);
    }
    engine_state = RECORD_ENGINE_FINISHED;
    record_engine_clear_pause_reason();
    write_edge_monitor_stop();
    mz_sense_set(true);
}

bool record_engine_start(const char *directory_path, const record_engine_config_t *config)
{
    record_engine_init();

    if ((directory_path == NULL) || (config == NULL))
    {
        record_engine_set_error_P(PSTR("REC ARG"));
        return false;
    }

    engine_directory = directory_path;
    engine_config = *config;

    if (engine_config.control_mode == RECORD_CONTROL_MANUAL)
    {
        mz_sense_set(false);
        return record_engine_begin_capture();
    }

    /* MOTOR/AUTO do not create a file yet, but show the same future
       RECxxxx name as MANUAL. The name is scanned only; no SD file is opened. */
    {
        bool preview_ok = (engine_config.format == FILE_FORMAT_WAV) ?
            wav_record_engine_preview_filename(engine_directory) :
            (engine_config.format == FILE_FORMAT_MZF) ?
                mzf_record_engine_preview_filename(engine_directory) :
                edge_record_engine_preview_filename(engine_directory,
                                                    engine_config.format);

        if (!preview_ok)
        {
            const char *error = (engine_config.format == FILE_FORMAT_WAV) ?
                wav_record_engine_get_error_text() :
                (engine_config.format == FILE_FORMAT_MZF) ?
                    mzf_record_engine_get_error_text() :
                    edge_record_engine_get_error_text();
            record_engine_set_error(error);
            return false;
        }
    }

    mz_sense_set(false);

    /* MOTOR and AUTO are armed without allocating/creating a file yet. */
    engine_state = RECORD_ENGINE_ARMED;
    if (engine_config.control_mode == RECORD_CONTROL_AUTO)
    {
        write_edge_monitor_arm_auto_trigger();
    }
    return true;
}

void record_engine_service(void)
{
    if (engine_state == RECORD_ENGINE_ARMED)
    {
        if ((engine_config.control_mode == RECORD_CONTROL_MOTOR && mz_motor_get()) ||
            (engine_config.control_mode == RECORD_CONTROL_AUTO &&
             write_edge_monitor_take_auto_trigger()))
        {
            (void)record_engine_begin_capture();
        }
        return;
    }

    if (engine_state == RECORD_ENGINE_RECORDING || engine_state == RECORD_ENGINE_PAUSED)
    {
        if (engine_config.control_mode == RECORD_CONTROL_MOTOR)
        {
            if ((engine_state == RECORD_ENGINE_RECORDING) && !mz_motor_get())
            {
                record_engine_pause_for_motor();
            }
            else if (engine_state == RECORD_ENGINE_PAUSED)
            {
                /* User pause wins over MOTOR HIGH. */
                if (paused_by_user)
                {
                    /* Stay paused until the user explicitly resumes. */
                }
                else if (!mz_motor_get())
                {
                    paused_by_motor = true;
                }
                else if (paused_by_motor)
                {
                    paused_by_motor = false;
                    (void)record_engine_resume_capture();
                }
            }
        }
    }

    if ((engine_config.control_mode == RECORD_CONTROL_AUTO) &&
        (engine_state == RECORD_ENGINE_RECORDING) &&
        (engine_config.format != FILE_FORMAT_WAV))
    {
        uint16_t activity_counter = record_engine_auto_activity_counter();
        uint32_t now = millis();
        if (activity_counter != auto_last_activity_counter)
        {
            auto_last_activity_counter = activity_counter;
            auto_last_activity_ms = now;
        }
        else if ((uint32_t)(now - auto_last_activity_ms) >= AUTO_RECORD_IDLE_STOP_MS)
        {
            record_engine_request_stop();
        }
    }

    if (engine_config.format == FILE_FORMAT_WAV)
    {
        wav_record_engine_service();
        if ((engine_config.control_mode == RECORD_CONTROL_AUTO) &&
            (engine_state == RECORD_ENGINE_RECORDING) &&
            (auto_wav_idle_limit_bytes != 0U) &&
            (wav_record_engine_get_idle_packed_bytes() >=
             auto_wav_idle_limit_bytes))
        {
            record_engine_request_stop();
        }
        switch (wav_record_engine_get_state())
        {
            case WAV_RECORD_ENGINE_FINALIZING: engine_state = RECORD_ENGINE_FINALIZING; break;
            case WAV_RECORD_ENGINE_FINISHED:
                record_engine_finish_capture();
                break;
            case WAV_RECORD_ENGINE_ERROR: record_engine_set_error(wav_record_engine_get_error_text()); break;
            case WAV_RECORD_ENGINE_PAUSED: engine_state = RECORD_ENGINE_PAUSED; break;
            case WAV_RECORD_ENGINE_RECORDING:
                if (engine_state != RECORD_ENGINE_FINALIZING)
                {
                    engine_state = RECORD_ENGINE_RECORDING;
                    record_engine_clear_pause_reason();
                }
                break;
            default: break;
        }
    }
    else if (is_edge_format(engine_config.format))
    {
        edge_record_engine_service();
        switch (edge_record_engine_get_state())
        {
            case EDGE_RECORD_ENGINE_FINALIZING: engine_state = RECORD_ENGINE_FINALIZING; break;
            case EDGE_RECORD_ENGINE_FINISHED:
                record_engine_finish_capture();
                break;
            case EDGE_RECORD_ENGINE_ERROR: record_engine_set_error(edge_record_engine_get_error_text()); break;
            case EDGE_RECORD_ENGINE_PAUSED: engine_state = RECORD_ENGINE_PAUSED; break;
            case EDGE_RECORD_ENGINE_RECORDING:
                if (engine_state != RECORD_ENGINE_FINALIZING)
                {
                    engine_state = RECORD_ENGINE_RECORDING;
                    record_engine_clear_pause_reason();
                }
                break;
            default: break;
        }
    }
    else if (engine_config.format == FILE_FORMAT_MZF)
    {
        mzf_record_engine_service();
        switch (mzf_record_engine_get_state())
        {
            case MZF_RECORD_ENGINE_FINALIZING: engine_state = RECORD_ENGINE_FINALIZING; break;
            case MZF_RECORD_ENGINE_FINISHED:
                record_engine_finish_capture();
                break;
            case MZF_RECORD_ENGINE_ERROR:
                record_engine_set_error(mzf_record_engine_get_error_text());
                break;
            case MZF_RECORD_ENGINE_PAUSED: engine_state = RECORD_ENGINE_PAUSED; break;
            case MZF_RECORD_ENGINE_RECORDING:
                if (engine_state != RECORD_ENGINE_FINALIZING)
                {
                    engine_state = RECORD_ENGINE_RECORDING;
                    record_engine_clear_pause_reason();
                }
                break;
            default: break;
        }
    }
}

void record_engine_toggle_pause(void)
{
    if (engine_state == RECORD_ENGINE_ARMED)
    {
        if (record_engine_begin_capture() &&
            (engine_config.control_mode == RECORD_CONTROL_MOTOR) && !mz_motor_get())
        {
            record_engine_pause_for_motor();
        }
        return;
    }

    if (engine_state == RECORD_ENGINE_RECORDING)
    {
        record_engine_pause_for_user();
        return;
    }

    if (engine_state == RECORD_ENGINE_PAUSED)
    {
        record_engine_resume_from_user();
    }
}

void record_engine_request_stop(void)
{
    if (engine_state == RECORD_ENGINE_ARMED)
    {
        record_engine_cancel();
        return;
    }
    if (engine_state == RECORD_ENGINE_RECORDING || engine_state == RECORD_ENGINE_PAUSED)
    {
        if (engine_config.format == FILE_FORMAT_WAV)
        {
            wav_record_engine_request_stop();
        }
        else
        {
            if (engine_config.format == FILE_FORMAT_MZF)
                mzf_record_engine_request_stop();
            else
                edge_record_engine_request_stop();
        }
        engine_state = RECORD_ENGINE_FINALIZING;
        record_engine_clear_pause_reason();
        write_edge_monitor_stop();
    }
}

void record_engine_cancel(void)
{
    /* A started capture always owns a just-created RECxxxx file. */
    cancelled_file_removed = capture_started;

    if (capture_started)
    {
        if (engine_config.format == FILE_FORMAT_WAV)
        {
            wav_record_engine_cancel();
        }
        else if (is_edge_format(engine_config.format))
        {
            edge_record_engine_cancel();
        }
        else if (engine_config.format == FILE_FORMAT_MZF)
        {
            mzf_record_engine_cancel();
        }
    }

    /* Keep the result visible; short LEFT acknowledges it and returns. */
    engine_state = RECORD_ENGINE_CANCELLED;
    capture_started = false;
    record_engine_clear_pause_reason();
    write_edge_monitor_stop();
    mz_sense_set(true);
}

record_engine_state_t record_engine_get_state(void) { return engine_state; }
file_format_t record_engine_get_format(void) { return engine_config.format; }
record_control_mode_t record_engine_get_control_mode(void) { return engine_config.control_mode; }
const char *record_engine_get_filename(void)
{
    const char *name = (engine_config.format == FILE_FORMAT_WAV) ?
        wav_record_engine_get_filename() :
        (engine_config.format == FILE_FORMAT_MZF) ?
            mzf_record_engine_get_filename() : edge_record_engine_get_filename();

    return (name != NULL && name[0] != '\0') ? name : NULL;
}
bool record_engine_get_detected_loader_mode(loader_mode_t *loader_mode)
{
    if (!engine_config.autoname) return false;
    return (engine_config.format == FILE_FORMAT_MZF) ?
        mzf_record_engine_get_detected_loader_mode(loader_mode) :
        record_autoname_get_detected_loader_mode(loader_mode);
}

bool record_engine_get_detected_aux_profile(record_autoname_aux_profile_t *profile)
{
    if (!engine_config.autoname || (engine_config.format == FILE_FORMAT_MZF))
        return false;
    return record_autoname_get_detected_aux_profile(profile);
}

bool record_engine_get_payload_progress_percent(uint8_t *percent)
{
    if (!engine_config.autoname) return false;
    return (engine_config.format == FILE_FORMAT_MZF) ?
        mzf_record_engine_get_payload_progress_percent(percent) :
        record_autoname_get_payload_progress_percent(percent);
}
const char *record_engine_get_error_text(void) { return engine_error; }
bool record_engine_cancelled_file_removed(void) { return cancelled_file_removed; }
uint8_t record_engine_get_buffer_fill_percent(void)
{
    if (!capture_started) return 100U;
    return (engine_config.format == FILE_FORMAT_WAV) ?
        (uint8_t)(100U - wav_record_engine_get_buffer_fill_percent()) :
        (engine_config.format == FILE_FORMAT_MZF) ?
            mzf_record_engine_get_buffer_headroom_percent() :
            edge_record_engine_get_buffer_headroom_percent();
}

const char *record_engine_get_live_name(void)
{
    return engine_config.autoname ? record_autoname_get_name() : NULL;
}
uint32_t record_engine_get_elapsed_seconds(void)
{
    uint32_t end_ms;
    if (!capture_started) return 0UL;
    end_ms = (engine_state == RECORD_ENGINE_PAUSED && pause_started_ms != 0UL) ?
        pause_started_ms : millis();
    if (end_ms < active_started_ms + paused_total_ms) return 0UL;
    return (end_ms - active_started_ms - paused_total_ms) / 1000UL;
}

uint32_t record_engine_get_wav_sample_rate(void)
{
    return (engine_config.format == FILE_FORMAT_WAV) ?
        engine_config.wav_sample_rate : 0UL;
}

char record_engine_get_pause_indicator(void)
{
    if (engine_state != RECORD_ENGINE_PAUSED) return '\0';
    if (paused_by_user) return 'U';
    if (paused_by_motor) return 'M';
    return '\0';
}
