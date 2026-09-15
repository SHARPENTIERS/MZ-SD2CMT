#include "record_menu.h"

#include <Arduino.h>
#include <string.h>

#include "../drivers/calibration_store.h"
#include "../drivers/lcd.h"
#include "../drivers/flash_text.h"

typedef enum
{
    RECORD_MENU_ITEM_TYPE = 0,
    RECORD_MENU_ITEM_CONTROL,
    RECORD_MENU_ITEM_AUTONAME,
    RECORD_MENU_ITEM_COUNT
} record_menu_item_t;

typedef enum
{
    /* Menu order keeps the existing default and adds direct MZF output. */
    RECORD_TYPE_WAV_44K = 0,
    RECORD_TYPE_WAV_22K,
    RECORD_TYPE_L16,
    RECORD_TYPE_LEP,
    RECORD_TYPE_MZF,
    RECORD_TYPE_COUNT
} record_type_t;

static record_menu_item_t selected_item = RECORD_MENU_ITEM_TYPE;
static record_type_t record_type = RECORD_TYPE_WAV_44K;
static record_control_mode_t control_mode = RECORD_CONTROL_MOTOR;
static bool autoname = false;
static record_settings_data_t saved_settings;
static bool settings_dirty = false;

static const char text_rec_type[] PROGMEM = ">REC TYPE";
static const char text_rec_mode[] PROGMEM = ">REC MODE";
static const char text_autoname[] PROGMEM = ">AUTONAME";
static const char text_off[] PROGMEM = "OFF";
static const char text_on[] PROGMEM = "ON";
static const char text_motor[] PROGMEM = "MOTOR";
static const char text_auto[] PROGMEM = "AUTO";
static const char text_manual[] PROGMEM = "MANUAL";
static const char text_lep[] PROGMEM = "LEP 50us";
static const char text_l16[] PROGMEM = "L16 16us";
static const char text_wav22[] PROGMEM = "WAV 22kHz";
static const char text_wav44[] PROGMEM = "WAV 44kHz";
static const char text_mzf[] PROGMEM = "MZF";
static const char text_unknown[] PROGMEM = "?";

static const char * const record_control_labels_P[] PROGMEM =
{
    text_motor, text_auto, text_manual
};
static const char * const record_type_labels_P[RECORD_TYPE_COUNT] PROGMEM =
{
    text_wav44, text_wav22, text_l16, text_lep, text_mzf
};

static void lcd_line_P(uint8_t row, PGM_P text)
{
    char line[17];
    uint8_t length;
    flash_text_copy(line, sizeof(line), text);
    for (length = 0U; (length < 16U) && (line[length] != '\0'); ++length) {}
    while (length < 16U) line[length++] = ' ';
    line[16] = '\0';
    lcd_set_cursor(0U, row);
    lcd_print(line);
}

static void lcd_value_line_P(PGM_P text)
{
    char line[17];
    line[0] = ' ';
    flash_text_copy(&line[1], sizeof(line) - 1U, text);
    for (uint8_t i = 0U; i < 16U; ++i)
    {
        if (line[i] == '\0')
        {
            for (; i < 16U; ++i) line[i] = ' ';
            break;
        }
    }
    line[16] = '\0';
    lcd_set_cursor(0U, 1U);
    lcd_print(line);
}

PGM_P record_control_mode_label_P(record_control_mode_t mode)
{
    const uint8_t index = ((uint8_t)mode <= (uint8_t)RECORD_CONTROL_MANUAL) ?
        (uint8_t)mode : (uint8_t)RECORD_CONTROL_MOTOR;
    return (PGM_P)pgm_read_word(&record_control_labels_P[index]);
}

static PGM_P record_type_label_P(void)
{
    if ((uint8_t)record_type >= (uint8_t)RECORD_TYPE_COUNT)
        return text_unknown;
    return (PGM_P)pgm_read_word(&record_type_labels_P[(uint8_t)record_type]);
}

static void toggle_current(void)
{
    if (selected_item == RECORD_MENU_ITEM_TYPE)
    {
        record_type = (record_type_t)(((uint8_t)record_type + 1U) %
                                      (uint8_t)RECORD_TYPE_COUNT);
    }
    else if (selected_item == RECORD_MENU_ITEM_CONTROL)
    {
        control_mode = (control_mode == RECORD_CONTROL_MOTOR) ? RECORD_CONTROL_AUTO :
                       (control_mode == RECORD_CONTROL_AUTO) ? RECORD_CONTROL_MANUAL :
                                                               RECORD_CONTROL_MOTOR;
    }
    else
    {
        autoname = !autoname;
    }
}

file_format_t record_menu_get_format(void)
{
    if (record_type == RECORD_TYPE_MZF) return FILE_FORMAT_MZF;
    return (record_type == RECORD_TYPE_LEP) ? FILE_FORMAT_LEP :
           (record_type == RECORD_TYPE_L16) ? FILE_FORMAT_L16 : FILE_FORMAT_WAV;
}

uint32_t record_menu_get_wav_sample_rate(void)
{
    return (record_type == RECORD_TYPE_WAV_44K) ? 44100UL : 22050UL;
}

static void collect_settings(record_settings_data_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->format = (uint8_t)record_menu_get_format();
    settings->wav_rate_44k = (record_menu_get_wav_sample_rate() >= 40000UL) ? 1U : 0U;
    settings->control_mode = (uint8_t)control_mode;
    settings->autoname = autoname ? 1U : 0U;
}

static bool stored_settings_valid(const record_settings_data_t *settings)
{
    bool valid_format;

    if (settings == NULL) return false;
    valid_format = (settings->format == (uint8_t)FILE_FORMAT_WAV) ||
                   (settings->format == (uint8_t)FILE_FORMAT_LEP) ||
                   (settings->format == (uint8_t)FILE_FORMAT_L16) ||
                   (settings->format == (uint8_t)FILE_FORMAT_MZF);
    return valid_format &&
           (settings->wav_rate_44k <= 1U) &&
           (settings->control_mode <= (uint8_t)RECORD_CONTROL_MANUAL) &&
           (settings->autoname <= 1U);
}

static void apply_stored_settings(const record_settings_data_t *settings)
{
    if (settings->format == (uint8_t)FILE_FORMAT_MZF)
        record_type = RECORD_TYPE_MZF;
    else if (settings->format == (uint8_t)FILE_FORMAT_LEP)
        record_type = RECORD_TYPE_LEP;
    else if (settings->format == (uint8_t)FILE_FORMAT_L16)
        record_type = RECORD_TYPE_L16;
    else
        record_type = settings->wav_rate_44k ? RECORD_TYPE_WAV_44K : RECORD_TYPE_WAV_22K;

    control_mode = (record_control_mode_t)settings->control_mode;
    autoname = settings->autoname != 0U;
}

void record_menu_init(void)
{
    record_settings_data_t stored;

    selected_item = RECORD_MENU_ITEM_TYPE;
    record_type = RECORD_TYPE_WAV_44K;
    control_mode = RECORD_CONTROL_MOTOR;
    autoname = false;

    if (calibration_store_load_record_settings(&stored) &&
        stored_settings_valid(&stored))
    {
        apply_stored_settings(&stored);
    }

    collect_settings(&saved_settings);
    settings_dirty = false;
}

bool record_menu_save_if_dirty(void)
{
    record_settings_data_t current;

    if (!settings_dirty) return true;
    collect_settings(&current);
    if (memcmp(&current, &saved_settings, sizeof(current)) == 0)
    {
        settings_dirty = false;
        return true;
    }
    if (!calibration_store_save_record_settings(&current)) return false;
    saved_settings = current;
    settings_dirty = false;
    return true;
}

record_menu_action_t record_menu_handle_event(button_event_t event)
{
    switch (event)
    {
        case BUTTON_EVENT_UP_PRESS:
        case BUTTON_EVENT_UP_REPEAT:
            selected_item = (record_menu_item_t)
                (((uint8_t)selected_item + (uint8_t)RECORD_MENU_ITEM_COUNT - 1U) %
                 (uint8_t)RECORD_MENU_ITEM_COUNT);
            break;
        case BUTTON_EVENT_DOWN_PRESS:
        case BUTTON_EVENT_DOWN_REPEAT:
            selected_item = (record_menu_item_t)
                (((uint8_t)selected_item + 1U) % (uint8_t)RECORD_MENU_ITEM_COUNT);
            break;
        case BUTTON_EVENT_SELECT_SHORT:
            toggle_current();
            settings_dirty = true;
            break;
        case BUTTON_EVENT_LEFT_SHORT:
        case BUTTON_EVENT_LEFT_LONG:
            return RECORD_MENU_ACTION_BACK;
        default:
            break;
    }
    return RECORD_MENU_ACTION_NONE;
}

void record_menu_render(void)
{
    if (selected_item == RECORD_MENU_ITEM_TYPE)
    {
        lcd_line_P(0U, text_rec_type);
        lcd_value_line_P(record_type_label_P());
        return;
    }
    if (selected_item == RECORD_MENU_ITEM_CONTROL)
    {
        lcd_line_P(0U, text_rec_mode);
        lcd_value_line_P(record_control_mode_label_P(control_mode));
        return;
    }
    lcd_line_P(0U, text_autoname);
    lcd_value_line_P(autoname ? text_on : text_off);
}

record_control_mode_t record_menu_get_control_mode(void) { return control_mode; }
bool record_menu_get_autoname(void) { return autoname; }
