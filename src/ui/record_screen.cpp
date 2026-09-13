#include "record_screen.h"

#include <Arduino.h>
#include <avr/pgmspace.h>

#include "../drivers/lcd.h"
#include "../drivers/flash_text.h"
#include "../record/record_engine.h"

static const char text_rec_fallback[] PROGMEM = "REC";
static const char text_wav_rate_22k[] PROGMEM = "22k";
static const char text_wav_rate_44k[] PROGMEM = "44k";
static const char text_wait_signal[] PROGMEM = "WAIT SIGNAL";
static const char text_paused_waiting_motor[] PROGMEM = "PAUSED         M";
static const char text_recording[] PROGMEM = "REC %02lu:%02lu";
static const char text_paused[] PROGMEM = "PAU %02lu:%02lu";
static const char text_saving_prefix[] PROGMEM = "SAVING ";
static const char text_please_wait[] PROGMEM = "PLEASE WAIT";
static const char text_saved_prefix[] PROGMEM = "SVD ";
static const char text_wait_short[] PROGMEM = "WAIT";
static const char text_file_deleted[] PROGMEM = "FILE DELETED";
static const char text_rec_cancelled[] PROGMEM = "REC CANCELLED";
static const char text_rec_error[] PROGMEM = "REC ERROR";
static const char text_detect_normal_1_1[] PROGMEM = "NRL 1:1";
static const char text_detect_normal_1_2[] PROGMEM = "NRL 1:2";
static const char text_detect_normal_1_3[] PROGMEM = "NRL 1:3";
static const char text_detect_normal_1_4[] PROGMEM = "NRL 1:4";
static const char text_detect_mz700_1_1[] PROGMEM = "MZ7 1:1";
static const char text_detect_mz700_1_3[] PROGMEM = "MZ7 1:3";
static const char text_detect_ic_1_4[] PROGMEM = "IC 1:4";
static const char text_detect_ic_1_3[] PROGMEM = "IC 1:3";
static const char text_detect_ic_1_2[] PROGMEM = "IC 1:2";
static const char text_detect_tc_1_3[] PROGMEM = "TC 1:3";
static const char text_detect_tc_1_4[] PROGMEM = "TC 1:4";
static const char text_detect_tc_1_2[] PROGMEM = "TC 1:2";
static const char text_detect_sin_1_1[] PROGMEM = "SIN 1:1";
static const char text_detect_sin_1_2[] PROGMEM = "SIN 1:2";
static const char text_detect_sin_1_3[] PROGMEM = "SIN 1:3";
static const char text_detect_sin_1_4[] PROGMEM = "SIN 1:4";
static const char text_detect_cpm_1_1[] PROGMEM = "CPM 1:1";
static const char text_detect_cpm_1_2[] PROGMEM = "CPM 1:2";
static const char text_detect_cpm_1_3[] PROGMEM = "CPM 1:3";
static const char text_detect_cpm_1_4[] PROGMEM = "CPM 1:4";

/* Immutable label pointers belong in flash too; a return-value switch makes
   AVR-GCC copy the equivalent 32-byte table into SRAM at startup. */
static const char * const detected_loader_labels_P[LOADER_MODE_COUNT] PROGMEM =
{
    text_detect_normal_1_1, NULL, NULL, NULL,
    NULL, text_detect_mz700_1_3, text_detect_ic_1_4, text_detect_ic_1_3,
    text_detect_ic_1_2, text_detect_tc_1_3, text_detect_tc_1_2,
    text_detect_normal_1_2, text_detect_normal_1_3,
    text_detect_mz700_1_1, text_detect_normal_1_4, text_detect_tc_1_4
};

static const char * const detected_aux_labels_P[9] PROGMEM =
{
    NULL,
    text_detect_sin_1_1, text_detect_sin_1_2,
    text_detect_sin_1_3, text_detect_sin_1_4,
    text_detect_cpm_1_1, text_detect_cpm_1_2,
    text_detect_cpm_1_3, text_detect_cpm_1_4
};

#define RECORD_NAME_SCROLL_START_HOLD_MS 1200UL
#define RECORD_NAME_SCROLL_STEP_MS 300UL
#define RECORD_NAME_SCROLL_END_HOLD_MS 800UL
#define RECORD_SAVED_PREFIX_LENGTH 4U
#define RECORD_SAVED_NAME_COLUMNS (16U - RECORD_SAVED_PREFIX_LENGTH)

static bool live_name_scroll_active = false;
static uint32_t live_name_scroll_started_ms = 0UL;
static bool saving_name_scroll_active = false;
static uint32_t saving_name_scroll_started_ms = 0UL;
static bool saved_name_scroll_active = false;
static uint32_t saved_name_scroll_started_ms = 0UL;

static void line(uint8_t row, const char *text)
{
    char output[17];
    uint8_t length = 0U;

    if (text != NULL)
    {
        while ((length < 16U) && (text[length] != '\0'))
        {
            output[length] = text[length];
            ++length;
        }
    }
    while (length < 16U) output[length++] = ' ';
    output[16] = '\0';
    lcd_set_cursor(0, row);
    lcd_print(output);
}

static void copy_filename_line(char *destination, const char *filename)
{
    if ((filename == NULL) || (filename[0] == '\0'))
    {
        flash_text_copy(destination, 17U, text_rec_fallback);
        return;
    }
    for (uint8_t i = 0U; i < 16U; ++i)
    {
        destination[i] = filename[i];
        if (filename[i] == '\0') return;
    }
    destination[16] = '\0';
}

/* Time-derived scrolling has no delays and needs no extra persistent buffer. */
static void copy_scrolling_name(char *destination, const char *name,
                                uint8_t visible_columns)
{
    uint8_t length = 0U;
    uint8_t offset = 0U;
    while ((length < 63U) && (name[length] != '\0')) ++length;
    if (visible_columns > 16U) visible_columns = 16U;
    if (length <= visible_columns)
    {
        copy_filename_line(destination, name);
        return;
    }

    if (!live_name_scroll_active)
    {
        live_name_scroll_active = true;
        live_name_scroll_started_ms = millis();
    }
    {
        uint8_t maximum_offset = (uint8_t)(length - visible_columns);
        uint32_t travel_ms = (uint32_t)maximum_offset *
                             RECORD_NAME_SCROLL_STEP_MS;
        uint32_t cycle_ms = RECORD_NAME_SCROLL_START_HOLD_MS + travel_ms +
                            RECORD_NAME_SCROLL_END_HOLD_MS;
        uint32_t elapsed = (millis() - live_name_scroll_started_ms) % cycle_ms;
        if (elapsed >= RECORD_NAME_SCROLL_START_HOLD_MS)
        {
            elapsed -= RECORD_NAME_SCROLL_START_HOLD_MS;
            offset = (elapsed >= travel_ms) ? maximum_offset :
                (uint8_t)(1UL + elapsed / RECORD_NAME_SCROLL_STEP_MS);
            if (offset > maximum_offset) offset = maximum_offset;
        }
    }
    for (uint8_t column = 0U; column < visible_columns; ++column)
    {
        uint8_t index = (uint8_t)(offset + column);
        destination[column] = (index < length) ? name[index] : ' ';
    }
    destination[visible_columns] = '\0';
}

/* Keep a status prefix fixed and scroll only the filename beside it. */
static void copy_scrolling_prefixed(char *destination,
                                    PGM_P prefix,
                                    uint8_t prefix_length,
                                    const char *filename,
                                    bool *scroll_active,
                                    uint32_t *scroll_started_ms)
{
    uint8_t filename_length = 0U;
    uint8_t offset = 0U;
    uint8_t name_columns;

    if ((destination == NULL) || (prefix == NULL) ||
        (scroll_active == NULL) || (scroll_started_ms == NULL)) return;
    if (prefix_length > 15U) prefix_length = 15U;
    name_columns = (uint8_t)(16U - prefix_length);

    if ((filename == NULL) || (filename[0] == '\0'))
    {
        for (uint8_t column = 0U; column < prefix_length; ++column)
            destination[column] = (char)pgm_read_byte(prefix + column);
        uint8_t column = prefix_length;
        if (column < 16U) destination[column++] = 'R';
        if (column < 16U) destination[column++] = 'E';
        if (column < 16U) destination[column++] = 'C';
        for (;
             column < 16U; ++column)
        {
            destination[column] = ' ';
        }
        destination[16] = '\0';
        return;
    }

    while ((filename_length < 63U) && (filename[filename_length] != '\0'))
        ++filename_length;

    if (filename_length > name_columns)
    {
        if (!*scroll_active)
        {
            *scroll_active = true;
            *scroll_started_ms = millis();
        }

        uint8_t maximum_offset =
            (uint8_t)(filename_length - name_columns);
        uint32_t travel_ms = (uint32_t)maximum_offset *
                             RECORD_NAME_SCROLL_STEP_MS;
        uint32_t cycle_ms = RECORD_NAME_SCROLL_START_HOLD_MS + travel_ms +
                            RECORD_NAME_SCROLL_END_HOLD_MS;
        uint32_t elapsed = (millis() - *scroll_started_ms) % cycle_ms;

        if (elapsed >= RECORD_NAME_SCROLL_START_HOLD_MS)
        {
            elapsed -= RECORD_NAME_SCROLL_START_HOLD_MS;
            offset = (elapsed >= travel_ms) ? maximum_offset :
                (uint8_t)(1UL + elapsed / RECORD_NAME_SCROLL_STEP_MS);
            if (offset > maximum_offset) offset = maximum_offset;
        }
    }

    for (uint8_t column = 0U; column < prefix_length; ++column)
    {
        destination[column] =
            (char)pgm_read_byte(prefix + column);
    }
    for (uint8_t column = 0U; column < name_columns; ++column)
    {
        const uint8_t index = (uint8_t)(offset + column);
        destination[prefix_length + column] =
            (index < filename_length) ? filename[index] : ' ';
    }
    destination[16] = '\0';
}

static void copy_scrolling_saved(char *destination, const char *filename)
{
    copy_scrolling_prefixed(destination, text_saved_prefix,
                            RECORD_SAVED_PREFIX_LENGTH, filename,
                            &saved_name_scroll_active,
                            &saved_name_scroll_started_ms);
}

static PGM_P wav_rate_label_P(uint32_t sample_rate)
{
    return (sample_rate >= 40000UL) ? text_wav_rate_44k : text_wav_rate_22k;
}

static PGM_P detected_loader_label_P(loader_mode_t mode)
{
    if ((uint8_t)mode >= (uint8_t)LOADER_MODE_COUNT) return NULL;
    return (PGM_P)pgm_read_word(&detected_loader_labels_P[(uint8_t)mode]);
}

static PGM_P detected_record_profile_label_P(void)
{
    loader_mode_t loader_mode;
    record_autoname_aux_profile_t aux_profile;

    if (record_engine_get_detected_loader_mode(&loader_mode))
        return detected_loader_label_P(loader_mode);

    if (!record_engine_get_detected_aux_profile(&aux_profile) ||
        (aux_profile > RECORD_AUTONAME_AUX_PROFILE_CPM_1_4))
    {
        return NULL;
    }
    return (PGM_P)pgm_read_word(&detected_aux_labels_P[aux_profile]);
}

static bool copy_detected_status_line(char *destination, PGM_P status)
{
    PGM_P detected_label = detected_record_profile_label_P();
    uint8_t column = 0U;

    if (detected_label == NULL)
        return false;

    while (column < 12U)
    {
        const char value = (char)pgm_read_byte(detected_label + column);
        if (value == '\0')
            break;
        destination[column++] = value;
    }
    while (column < 12U)
        destination[column++] = ' ';
    for (uint8_t status_column = 0U; status_column < 4U; ++status_column)
    {
        destination[column++] =
            (char)pgm_read_byte(status + status_column);
    }
    destination[16] = '\0';
    return true;
}

static bool copy_detected_profile_line(char *destination)
{
    PGM_P detected_label = detected_record_profile_label_P();

    if (detected_label == NULL) return false;
    flash_text_copy(destination, 17U, detected_label);
    return true;
}

/* Fixed detected layout:
     REC NRL 1:4 45%[blank]
     PAU NRL 1:4 45%M
   The percent field always stays in columns 11--14.  The final column is
   permanently reserved for the pause reason, so M/U cannot move the field. */
static bool copy_detected_progress_line(char *destination,
                                        bool paused,
                                        char pause_reason)
{
    PGM_P detected_label = detected_record_profile_label_P();
    uint8_t percent;
    uint8_t profile_column = 0U;
    const uint8_t percent_end = 14U;
    uint8_t percent_start = (uint8_t)(percent_end - 3U);

    if ((detected_label == NULL) ||
        !record_engine_get_payload_progress_percent(&percent))
    {
        return false;
    }

    for (uint8_t column = 0U; column < 16U; ++column)
        destination[column] = ' ';
    destination[0] = paused ? 'P' : 'R';
    destination[1] = paused ? 'A' : 'E';
    destination[2] = paused ? 'U' : 'C';

    while (profile_column < 7U)
    {
        const char value =
            (char)pgm_read_byte(detected_label + profile_column);
        if (value == '\0') break;
        destination[4U + profile_column] = value;
        ++profile_column;
    }

    destination[percent_start] = (percent >= 100U) ? '1' : ' ';
    destination[percent_start + 1U] = (percent >= 10U) ?
        (char)('0' + ((percent / 10U) % 10U)) : ' ';
    destination[percent_start + 2U] = (char)('0' + (percent % 10U));
    destination[percent_end] = '%';

    /* 100% means that the detected payload length has been reached, not that
       RECORD has stopped.  In MANUAL the capture may continue indefinitely;
       in AUTO the five-second idle tail is still being recorded.  Blink REC
       at ~0.5 s cadence while the engine is genuinely RECORDING so the user
       cannot mistake payload completion for record completion.  Derive the
       phase directly from millis(): no persistent SRAM state is required. */
    if (!paused && (percent >= 100U) &&
        ((((uint16_t)millis()) & 0x0200U) != 0U))
    {
        destination[0] = ' ';
        destination[1] = ' ';
        destination[2] = ' ';
    }

    if (paused)
        destination[15] = (pause_reason == '\0') ? '?' : pause_reason;
    destination[16] = '\0';
    return true;
}

static void append_buffer_bar(char *destination)
{
    uint8_t column = 0U;
    uint8_t buffer_percent = record_engine_get_buffer_fill_percent();

    while ((column < 15U) && (destination[column] != '\0')) ++column;
    while (column < 15U) destination[column++] = ' ';
    destination[15] = (char)LCD_VERTICAL_BAR_CHAR;
    destination[16] = '\0';
    lcd_set_vertical_bar(buffer_percent);
}

/* RECxxxx.WAV is exactly 11 characters. The compact flash suffix is right
   aligned to the final LCD columns: REC0001.WAV  44k. */
static void copy_wav_filename_line(char *destination, const char *filename,
                                   uint32_t sample_rate)
{
    char rate[4];
    uint8_t index = 0U;
    uint8_t rate_length = 0U;
    uint8_t rate_start;

    flash_text_copy(rate, sizeof(rate), wav_rate_label_P(sample_rate));
    while ((rate_length < (uint8_t)(sizeof(rate) - 1U)) &&
           (rate[rate_length] != '\0'))
    {
        ++rate_length;
    }
    /* Column 15 is reserved for the common graphical buffer indicator. */
    rate_start = (uint8_t)(15U - rate_length);

    if ((filename == NULL) || (filename[0] == '\0'))
    {
        flash_text_copy(destination, 17U, text_rec_fallback);
        while ((destination[index] != '\0') && (index < rate_start))
        {
            ++index;
        }
    }
    else
    {
        while ((index < rate_start) && (filename[index] != '\0'))
        {
            destination[index] = filename[index];
            ++index;
        }
    }

    while (index < rate_start)
    {
        destination[index++] = ' ';
    }
    for (uint8_t i = 0U; i < rate_length; ++i)
    {
        destination[index++] = rate[i];
    }
    destination[15] = '\0';
}

static void append_pause_reason(char *destination, char reason)
{
    uint8_t column = 0U;
    if ((destination == NULL) || (reason == '\0')) return;
    while ((column < 16U) && (destination[column] != '\0')) ++column;
    while (column < 16U) destination[column++] = ' ';
    destination[15] = reason;
    destination[16] = '\0';
}

static void copy_active_name_and_buffer(char *destination,
                                        const char *filename,
                                        const char *live_name)
{
    if (live_name != NULL)
    {
        copy_scrolling_name(destination, live_name, 15U);
    }
    else if (record_engine_get_format() == FILE_FORMAT_WAV)
    {
        copy_wav_filename_line(destination, filename,
                               record_engine_get_wav_sample_rate());
    }
    else
    {
        if (filename != NULL)
            copy_scrolling_name(destination, filename, 15U);
        else
            flash_text_copy(destination, 17U, text_rec_fallback);
    }
    append_buffer_bar(destination);
}

record_screen_action_t record_screen_handle_event(button_event_t event)
{
    switch (event)
    {
        case BUTTON_EVENT_SELECT_SHORT:
            return RECORD_SCREEN_ACTION_TOGGLE_PAUSE;
        case BUTTON_EVENT_LEFT_SHORT:
            return RECORD_SCREEN_ACTION_STOP_SAVE;
        case BUTTON_EVENT_LEFT_LONG:
            return RECORD_SCREEN_ACTION_CANCEL_BACK;
        default:
            return RECORD_SCREEN_ACTION_NONE;
    }
}

void record_screen_render(void)
{
    record_engine_state_t state = record_engine_get_state();
    const char *filename = record_engine_get_filename();
    const char *live_name = record_engine_get_live_name();
    char line0[17];
    char line1[17];
    char filename_copy[17];
    uint32_t seconds = record_engine_get_elapsed_seconds();
    char pause_indicator = record_engine_get_pause_indicator();

    copy_filename_line(line0, filename);
    copy_filename_line(filename_copy, filename);

    if (live_name == NULL) live_name_scroll_active = false;
    if (state != RECORD_ENGINE_FINALIZING) saving_name_scroll_active = false;
    if (state != RECORD_ENGINE_FINISHED) saved_name_scroll_active = false;

    switch (state)
    {
        case RECORD_ENGINE_ARMED:
            copy_active_name_and_buffer(line0, filename, live_name);
            if (record_engine_get_control_mode() == RECORD_CONTROL_AUTO)
            {
                flash_text_copy(line1, sizeof(line1), text_wait_signal);
            }
            else
            {
                flash_text_copy(line1, sizeof(line1), text_paused_waiting_motor);
            }
            break;
        case RECORD_ENGINE_RECORDING:
            copy_active_name_and_buffer(line0, filename, live_name);
            if (!copy_detected_progress_line(line1, false, '\0'))
            {
                flash_text_snprintf(line1, sizeof(line1), text_recording,
                                    (unsigned long)((seconds / 60UL) % 100UL),
                                    (unsigned long)(seconds % 60UL));
            }
            break;
        case RECORD_ENGINE_PAUSED:
            copy_active_name_and_buffer(line0, filename, live_name);
            if (!copy_detected_progress_line(line1, true, pause_indicator))
            {
                flash_text_snprintf(line1, sizeof(line1), text_paused,
                                    (unsigned long)((seconds / 60UL) % 100UL),
                                    (unsigned long)(seconds % 60UL));
                append_pause_reason(line1,
                                    (pause_indicator == '\0') ? '?' :
                                                                 pause_indicator);
            }
            break;
        case RECORD_ENGINE_FINALIZING:
            copy_scrolling_prefixed(line0, text_saving_prefix, 7U,
                                    (live_name != NULL) ? live_name : filename_copy,
                                    &saving_name_scroll_active,
                                    &saving_name_scroll_started_ms);
            if (!copy_detected_status_line(line1, text_wait_short))
                flash_text_copy(line1, sizeof(line1), text_please_wait);
            break;
        case RECORD_ENGINE_FINISHED:
            copy_scrolling_saved(line0, filename);
            if (!copy_detected_profile_line(line1)) line1[0] = '\0';
            break;
        case RECORD_ENGINE_CANCELLED:
            flash_text_copy(line0, sizeof(line0), record_engine_cancelled_file_removed() ?
                            text_file_deleted : text_rec_cancelled);
            line1[0] = '\0';
            break;
        case RECORD_ENGINE_ERROR:
            flash_text_copy(line0, sizeof(line0), text_rec_error);
            copy_filename_line(line1, record_engine_get_error_text());
            break;
        case RECORD_ENGINE_STOPPED:
        default:
            line1[0] = '\0';
            break;
    }

    line(0U, line0);
    line(1U, line1);
}
