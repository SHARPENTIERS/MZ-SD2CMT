#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "play_screen.h"
#include "../drivers/lcd.h"
#include "../drivers/flash_text.h"
#include "../formats/file_format.h"
#include "../play/mzf_loader.h"

static const char text_play[] PROGMEM = "PLAY";
static const char text_no_session[] PROGMEM = "NO SESSION";
static const char text_ply[] PROGMEM = "PLY";
static const char text_pla[] PROGMEM = "PLA";
static const char text_pau[] PROGMEM = "PAU";
static const char text_err[] PROGMEM = "ERR";
static const char text_rdy[] PROGMEM = "RDY";
static const char text_paused_full[] PROGMEM = "PAUSED";
static const char text_error[] PROGMEM = "ERR %.12s";
static const char text_unknown[] PROGMEM = "UNKNOWN";
static const char text_unsupported[] PROGMEM = "UNSUPPORTED";
static const char text_play_time[] PROGMEM = "%s %02u:%02u/%02u:%02u";
static const char text_play_percent[] PROGMEM = "%s        %03u%% ";
static const char text_play_phase_percent[] PROGMEM = "%s %-3s    %03u%% ";
static const char text_mzt_select_time[] PROGMEM = "SEL %-4s %02u:%02u";
static const char text_mzt_select_unknown[] PROGMEM = "SEL %-4s --:--";
static const char text_mzt_info_time[] PROGMEM = "MTI %-4s %02u:%02u";
static const char text_mzt_info_unknown[] PROGMEM = "MTI %-4s --:--";
static const char text_mzt_manual_time[] PROGMEM = "MAN %-4s %02u:%02u";
static const char text_mzt_manual_unknown[] PROGMEM = "MAN %-4s --:--";
static const char text_phase_nll[] PROGMEM = "NLL";
static const char text_phase_nlh[] PROGMEM = "NLH";
static const char text_phase_hll[] PROGMEM = "HLL";
static const char text_phase_hlh[] PROGMEM = "HLH";
static const char text_phase_u7l[] PROGMEM = "U7L";
static const char text_phase_u7h[] PROGMEM = "U7H";
static const char text_phase_icl[] PROGMEM = "ICL";
static const char text_phase_ic[] PROGMEM = "IC";
static const char text_phase_tc[] PROGMEM = "TC";
static const char text_phase_f3l[] PROGMEM = "F3L";
static const char text_phase_f3h[] PROGMEM = "F3H";
static const char text_phase_ul[] PROGMEM = "UL";

static const char mode_n11[] PROGMEM = "N11";
static const char mode_n12[] PROGMEM = "N12";
static const char mode_n13[] PROGMEM = "N13";
static const char mode_n14[] PROGMEM = "N14";
static const char mode_m71[] PROGMEM = "M71";
static const char mode_m73[] PROGMEM = "M73";
static const char mode_ic2[] PROGMEM = "IC2";
static const char mode_ic3[] PROGMEM = "IC3";
static const char mode_ic4[] PROGMEM = "IC4";
static const char mode_tc2[] PROGMEM = "TC2";
static const char mode_tc3[] PROGMEM = "TC3";
static const char mode_ul[] PROGMEM = "UL";
static const char mode_ul8[] PROGMEM = "UL8";
static const char mode_ul7[] PROGMEM = "UL7";

static const char * const play_state_labels_P[] PROGMEM =
{
    text_rdy, text_ply, text_pau, text_err
};

#define PLAY_NAME_SCROLL_START_HOLD_MS 1200UL
#define PLAY_NAME_SCROLL_STEP_MS 300UL
#define PLAY_NAME_SCROLL_END_HOLD_MS 800UL

static bool play_name_scroll_active = false;
static uint32_t play_name_scroll_started_ms = 0UL;
static uint16_t play_name_scroll_hash = 0U;
static uint8_t play_name_scroll_length = 0U;

static void lcd_print_fixed(uint8_t row, const char *text)
{
    char line[17];
    uint8_t length = 0U;
    if (text != NULL)
    {
        while ((length < 16U) && (text[length] != '\0'))
        {
            line[length] = text[length];
            ++length;
        }
    }
    while (length < 16U) line[length++] = ' ';
    line[16] = '\0';
    lcd_set_cursor(0U, row);
    lcd_print(line);
}

static void lcd_print_fixed_P(uint8_t row, PGM_P text)
{
    char line[17];
    flash_text_copy(line, sizeof(line), text);
    lcd_print_fixed(row, line);
}

static uint16_t filename_hash(const char *name, uint8_t *length)
{
    uint16_t hash = 5381U;
    uint8_t count = 0U;
    if (name != NULL)
    {
        while ((count < 159U) && (name[count] != '\0'))
        {
            hash = (uint16_t)((hash * 33U) ^ (uint8_t)name[count]);
            ++count;
        }
    }
    if (length != NULL) *length = count;
    return hash;
}

static void copy_scrolling_filename(char *destination,
                                    const char *filename,
                                    uint8_t visible_columns)
{
    uint8_t length;
    uint8_t offset = 0U;
    uint16_t hash;

    if (destination == NULL) return;
    if ((filename == NULL) || (filename[0] == '\0'))
    {
        destination[0] = 'P';
        destination[1] = 'L';
        destination[2] = 'A';
        destination[3] = 'Y';
        destination[4] = '\0';
        play_name_scroll_active = false;
        return;
    }
    if (visible_columns > 16U) visible_columns = 16U;

    hash = filename_hash(filename, &length);
    if ((hash != play_name_scroll_hash) ||
        (length != play_name_scroll_length))
    {
        play_name_scroll_hash = hash;
        play_name_scroll_length = length;
        play_name_scroll_active = false;
    }

    if (length > visible_columns)
    {
        uint8_t maximum_offset = (uint8_t)(length - visible_columns);
        uint32_t travel_ms = (uint32_t)maximum_offset *
                             PLAY_NAME_SCROLL_STEP_MS;
        uint32_t cycle_ms = PLAY_NAME_SCROLL_START_HOLD_MS + travel_ms +
                            PLAY_NAME_SCROLL_END_HOLD_MS;
        uint32_t elapsed;

        if (!play_name_scroll_active)
        {
            play_name_scroll_active = true;
            play_name_scroll_started_ms = millis();
        }
        elapsed = (millis() - play_name_scroll_started_ms) % cycle_ms;
        if (elapsed >= PLAY_NAME_SCROLL_START_HOLD_MS)
        {
            elapsed -= PLAY_NAME_SCROLL_START_HOLD_MS;
            offset = (elapsed >= travel_ms) ? maximum_offset :
                (uint8_t)(1UL + elapsed / PLAY_NAME_SCROLL_STEP_MS);
            if (offset > maximum_offset) offset = maximum_offset;
        }
    }

    for (uint8_t column = 0U; column < visible_columns; ++column)
    {
        uint8_t index = (uint8_t)(offset + column);
        destination[column] = (index < length) ? filename[index] : ' ';
    }
    destination[visible_columns] = '\0';
}

static void append_buffer_bar(char *destination, uint8_t percent)
{
    uint8_t column = 0U;
    if (destination == NULL) return;
    while ((column < 15U) && (destination[column] != '\0')) ++column;
    while (column < 15U) destination[column++] = ' ';
    destination[15] = (char)LCD_VERTICAL_BAR_CHAR;
    destination[16] = '\0';
    lcd_set_vertical_bar(percent);
}

static PGM_P state_to_label_P(play_controller_state_t state)
{
    const uint8_t index =
        ((uint8_t)state <= (uint8_t)PLAY_CONTROLLER_STATE_ERROR) ?
            (uint8_t)state : (uint8_t)PLAY_CONTROLLER_STATE_READY;
    return (PGM_P)pgm_read_word(&play_state_labels_P[index]);
}

static PGM_P play_state_to_label_P(const play_controller_view_t *view)
{
    if ((view != NULL) &&
        (view->state == PLAY_CONTROLLER_STATE_PLAYING) &&
        view->loader_from_info_sidecar)
    {
        return text_pla;
    }
    return state_to_label_P((view == NULL) ? PLAY_CONTROLLER_STATE_READY :
                                             view->state);
}

static void play_time_parts(uint32_t milliseconds,
                            uint8_t *minutes,
                            uint8_t *seconds)
{
    uint32_t total_seconds = milliseconds / 1000UL;
    if (total_seconds > 5999UL) total_seconds = 5999UL;
    *minutes = (uint8_t)(total_seconds / 60UL);
    *seconds = (uint8_t)(total_seconds % 60UL);
}

static char play_pause_indicator(const play_controller_view_t *view)
{
    if ((view == NULL) || (view->state != PLAY_CONTROLLER_STATE_PAUSED))
    {
        return '\0';
    }
    if (view->paused_by_user) return 'U';
    if (view->paused_by_motor) return 'M';
    return '\0';
}

static PGM_P progress_phase_label_P(play_progress_phase_t phase)
{
    switch (phase)
    {
        case PLAY_PROGRESS_PHASE_ULTRAFAST_LOADER_LOW: return text_phase_nll;
        case PLAY_PROGRESS_PHASE_ULTRAFAST_LOADER_HIGH: return text_phase_nlh;
        case PLAY_PROGRESS_PHASE_ULTRAFAST_HEADER:
            return mzf_loader_is_mz800_header_high() ? text_phase_hlh :
                                                       text_phase_hll;
        case PLAY_PROGRESS_PHASE_MZ700_UL_LOW: return text_phase_u7l;
        case PLAY_PROGRESS_PHASE_MZ700_UL_HIGH: return text_phase_u7h;
        case PLAY_PROGRESS_PHASE_IC_TURBO_LOADER: return text_phase_icl;
        case PLAY_PROGRESS_PHASE_IC_TURBO_DATA: return text_phase_ic;
        case PLAY_PROGRESS_PHASE_TC_TURBO_LOADER: return text_phase_tc;
        case PLAY_PROGRESS_PHASE_TC_TURBO_DATA: return text_phase_tc;
        case PLAY_PROGRESS_PHASE_MZ700_FAST3_LOW: return text_phase_f3l;
        case PLAY_PROGRESS_PHASE_MZ700_FAST3_HIGH: return text_phase_f3h;
        case PLAY_PROGRESS_PHASE_ULTRAFAST_DATA: return text_phase_ul;
        default: return NULL;
    }
}

static PGM_P mzt_loader_label_P(loader_mode_t mode)
{
    switch (mode)
    {
        case LOADER_MODE_NORMAL_1_2: return mode_n12;
        case LOADER_MODE_NORMAL_1_3: return mode_n13;
        case LOADER_MODE_NORMAL_1_4: return mode_n14;
        case LOADER_MODE_MZ700_1X: return mode_m71;
        case LOADER_MODE_MZ700_3X: return mode_m73;
        case LOADER_MODE_IC_1_2: return mode_ic2;
        case LOADER_MODE_IC_1_3: return mode_ic3;
        case LOADER_MODE_IC_1_4: return mode_ic4;
        case LOADER_MODE_TC_1_2: return mode_tc2;
        case LOADER_MODE_TC_1_3: return mode_tc3;
        case LOADER_MODE_UL: return mode_ul;
        case LOADER_MODE_UL_MZ800: return mode_ul8;
        case LOADER_MODE_UL_MZ700: return mode_ul7;
        case LOADER_MODE_NORMAL_1_1:
        default: return mode_n11;
    }
}

static void append_pause_indicator(char *line, char indicator)
{
    uint8_t length = 0U;

    if ((line == NULL) || (indicator == '\0')) return;

    while ((length < 16U) && (line[length] != '\0')) ++length;
    while (length < 16U) line[length++] = ' ';
    line[15] = indicator;
    line[16] = '\0';
}

static void build_play_line0(const play_controller_view_t *view, char *line0)
{
    char scrolled[17];
    uint8_t prefix_length = 0U;
    const char *name;

    memset(line0, ' ', 16U);
    line0[16] = '\0';

    if (view->format == FILE_FORMAT_MZT)
    {
        char prefix[13];
        int written = snprintf(prefix, sizeof(prefix), "%u%c%u ",
                               (unsigned int)view->mzt_record_index,
                               view->has_info_sidecar ? 'I' : '/',
                               (unsigned int)view->mzt_record_count);
        if (written < 0) written = 0;
        prefix_length = (uint8_t)written;
        if (prefix_length >= sizeof(prefix))
            prefix_length = (uint8_t)(sizeof(prefix) - 1U);
        if (prefix_length > 14U) prefix_length = 14U;
        memcpy(line0, prefix, prefix_length);
        name = ((view->mzt_record_title != NULL) &&
                (view->mzt_record_title[0] != '\0')) ?
                    view->mzt_record_title : view->filename;
        copy_scrolling_filename(scrolled, name,
                                (uint8_t)(15U - prefix_length));
        memcpy(line0 + prefix_length, scrolled,
               (uint8_t)(15U - prefix_length));
    }
    else if (view->loader_from_info_sidecar && (view->format == FILE_FORMAT_MZF))
    {
        line0[0] = 'M';
        line0[1] = 'F';
        line0[2] = 'I';
        line0[3] = ' ';
        copy_scrolling_filename(scrolled, view->filename, 11U);
        memcpy(line0 + 4U, scrolled, 11U);
    }
    else
    {
        copy_scrolling_filename(line0, view->filename, 15U);
    }

    append_buffer_bar(line0, view->buffer_fill_percent);
}

play_screen_action_t play_screen_handle_event(button_event_t event)
{
    switch (event)
    {
        case BUTTON_EVENT_UP_PRESS:
        case BUTTON_EVENT_UP_REPEAT:
            (void)play_controller_select_mzt_record(-1);
            return PLAY_SCREEN_ACTION_NONE;
        case BUTTON_EVENT_DOWN_PRESS:
        case BUTTON_EVENT_DOWN_REPEAT:
            (void)play_controller_select_mzt_record(1);
            return PLAY_SCREEN_ACTION_NONE;
        case BUTTON_EVENT_SELECT_SHORT: return PLAY_SCREEN_ACTION_TOGGLE_PLAY;
        case BUTTON_EVENT_LEFT_SHORT:
        case BUTTON_EVENT_LEFT_LONG:
            /*
                First STOP inside an active MZT returns to its mini-browser.
                When the mini-browser is already visible the helper returns
                false and the normal BACK action exits to the file browser.
            */
            if (play_controller_stop_to_mzt_selector())
            {
                return PLAY_SCREEN_ACTION_NONE;
            }
            return PLAY_SCREEN_ACTION_BACK;
        default: return PLAY_SCREEN_ACTION_NONE;
    }
}

void play_screen_render(const play_controller_view_t *view)
{
    char line0[17];
    char line1[17];
    char label[4];
    char phase_label[4];
    char mode_label[5];
    char error_fallback[17];
    uint8_t elapsed_minutes;
    uint8_t elapsed_seconds;
    uint8_t total_minutes;
    uint8_t total_seconds;
    char pause_indicator;

    if (view == NULL)
    {
        play_name_scroll_active = false;
        lcd_print_fixed_P(0U, text_play);
        lcd_print_fixed_P(1U, text_no_session);
        return;
    }

    build_play_line0(view, line0);
    lcd_print_fixed(0U, line0);
    if (view->state == PLAY_CONTROLLER_STATE_ERROR)
    {
        const char *error_text = view->error_text;
        if (error_text == NULL)
        {
            flash_text_copy(error_fallback, sizeof(error_fallback), text_unknown);
            error_text = error_fallback;
        }
        flash_text_snprintf(line1, sizeof(line1), text_error, error_text);
    }
    else if (view->format == FILE_FORMAT_UNKNOWN)
    {
        flash_text_copy(line1, sizeof(line1), text_unsupported);
    }
    else if ((view->format == FILE_FORMAT_MZT) && view->mzt_selection_pending)
    {
        flash_text_copy(mode_label, sizeof(mode_label),
                        mzt_loader_label_P(view->mzt_record_loader_mode));
        if (view->total_duration_ms == 0UL)
        {
            flash_text_snprintf(line1, sizeof(line1),
                                view->loader_from_info_sidecar ?
                                    text_mzt_info_unknown :
                                (view->loader_mode != LOADER_MODE_AUTO) ?
                                    text_mzt_manual_unknown :
                                    text_mzt_select_unknown,
                                mode_label);
        }
        else
        {
            play_time_parts(view->total_duration_ms,
                            &total_minutes, &total_seconds);
            flash_text_snprintf(line1, sizeof(line1),
                                view->loader_from_info_sidecar ?
                                    text_mzt_info_time :
                                (view->loader_mode != LOADER_MODE_AUTO) ?
                                    text_mzt_manual_time :
                                    text_mzt_select_time,
                                mode_label,
                                (unsigned int)total_minutes,
                                (unsigned int)total_seconds);
        }
    }
    else if (view->progress_is_percent)
    {
        PGM_P phase_label_P = progress_phase_label_P(view->progress_phase);
        if (phase_label_P != NULL)
        {
            flash_text_copy(phase_label, sizeof(phase_label), phase_label_P);
        }

        if (view->waiting_for_motor &&
            (view->state != PLAY_CONTROLLER_STATE_PAUSED))
        {
            flash_text_copy(line1, sizeof(line1), text_paused_full);
        }
        else
        {
            flash_text_copy(label, sizeof(label), play_state_to_label_P(view));
            if (phase_label_P != NULL)
            {
                flash_text_snprintf(line1, sizeof(line1), text_play_phase_percent,
                                    label, phase_label,
                                    (unsigned int)view->progress_percent);
            }
            else
            {
                flash_text_snprintf(line1, sizeof(line1), text_play_percent, label,
                                    (unsigned int)view->progress_percent);
            }
        }
    }
    else
    {
        play_time_parts(view->elapsed_ms, &elapsed_minutes, &elapsed_seconds);
        play_time_parts(view->total_duration_ms, &total_minutes, &total_seconds);

        if (view->waiting_for_motor &&
            (view->state != PLAY_CONTROLLER_STATE_PAUSED))
        {
            flash_text_copy(line1, sizeof(line1), text_paused_full);
        }
        else
        {
            flash_text_copy(label, sizeof(label), play_state_to_label_P(view));
            flash_text_snprintf(line1, sizeof(line1), text_play_time, label,
                                (unsigned int)elapsed_minutes,
                                (unsigned int)elapsed_seconds,
                                (unsigned int)total_minutes,
                                (unsigned int)total_seconds);
        }
    }

    pause_indicator = view->waiting_for_motor ? 'M' : play_pause_indicator(view);
    append_pause_indicator(line1, pause_indicator);
    lcd_print_fixed(1U, line1);
}
