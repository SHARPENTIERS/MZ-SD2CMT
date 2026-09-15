#include "settings_menu.h"

#include <Arduino.h>
#include <string.h>

#include "../drivers/backlight.h"
#include "../drivers/calibration_store.h"
#include "../drivers/external_switch.h"
#include "../drivers/flash_text.h"
#include "../drivers/lcd.h"
#include "../drivers/ram_monitor.h"
#include "../version.h"

typedef enum
{
    SETTINGS_ITEM_CMT_SOURCE = 0,
    SETTINGS_ITEM_BACKLIGHT,
    SETTINGS_ITEM_ABOUT,
    SETTINGS_ITEM_COUNT
} settings_item_t;

static settings_item_t selected_item = SETTINGS_ITEM_CMT_SOURCE;
static system_settings_data_t saved_settings;
static bool settings_dirty = false;
static bool settings_loaded = false;
static bool settings_save_failed = false;

static const char text_source[] PROGMEM = ">CMT SOURCE";
static const char text_backlight[] PROGMEM = ">BACKLIGHT";
static const char text_about[] PROGMEM = "ABOUT v" SD2CMT2_VERSION;
static const char text_internal[] PROGMEM = " INTERNAL";
static const char text_external[] PROGMEM = " EXTERNAL";

static void settings_print_fixed_P(uint8_t row, PGM_P text)
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

static void settings_print_fixed(uint8_t row, const char *text)
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

static void settings_move(int8_t direction)
{
    int8_t next = (int8_t)selected_item + direction;
    if (next < 0) next = (int8_t)SETTINGS_ITEM_COUNT - 1;
    if (next >= (int8_t)SETTINGS_ITEM_COUNT) next = 0;
    selected_item = (settings_item_t)next;
}

static void collect_settings(system_settings_data_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->cmt_source = (uint8_t)external_switch_get_source();
    settings->backlight_percent = backlight_get_percent();
}

static bool stored_settings_valid(const system_settings_data_t *settings)
{
    return (settings != NULL) &&
           (settings->cmt_source <= (uint8_t)CMT_SOURCE_EXTERNAL) &&
           (settings->backlight_percent >= LCD_BACKLIGHT_MIN_PERCENT) &&
           (settings->backlight_percent <= 100U);
}

void settings_menu_init(void)
{
    selected_item = SETTINGS_ITEM_CMT_SOURCE;

    if (!settings_loaded)
    {
        system_settings_data_t stored;

        if (calibration_store_load_system_settings(&stored) &&
            stored_settings_valid(&stored))
        {
            external_switch_set_source((cmt_source_t)stored.cmt_source);
            backlight_set_percent(stored.backlight_percent);
        }

        collect_settings(&saved_settings);
        settings_dirty = false;
        settings_save_failed = false;
        settings_loaded = true;
    }
}

bool settings_menu_save_if_dirty(void)
{
    system_settings_data_t current;

    if (!settings_dirty) return true;
    collect_settings(&current);
    if (memcmp(&current, &saved_settings, sizeof(current)) == 0)
    {
        settings_dirty = false;
        settings_save_failed = false;
        return true;
    }
    if (!calibration_store_save_system_settings(&current))
    {
        settings_save_failed = true;
        return false;
    }
    saved_settings = current;
    settings_dirty = false;
    settings_save_failed = false;
    return true;
}

settings_menu_action_t settings_menu_handle_event(button_event_t event)
{
    switch (event)
    {
        case BUTTON_EVENT_UP_PRESS:
        case BUTTON_EVENT_UP_REPEAT:
            settings_move(-1);
            break;

        case BUTTON_EVENT_DOWN_PRESS:
        case BUTTON_EVENT_DOWN_REPEAT:
            settings_move(1);
            break;

        case BUTTON_EVENT_SELECT_SHORT:
            if (selected_item == SETTINGS_ITEM_CMT_SOURCE)
            {
                external_switch_set_source(
                    external_switch_get_source() == CMT_SOURCE_INTERNAL ?
                    CMT_SOURCE_EXTERNAL : CMT_SOURCE_INTERNAL);
                settings_dirty = true;
                settings_save_failed = false;
            }
            else if (selected_item == SETTINGS_ITEM_BACKLIGHT)
            {
                uint8_t percent = backlight_get_percent();
                percent = (percent >= 100U) ? LCD_BACKLIGHT_MIN_PERCENT :
                                              (uint8_t)(percent + 10U);
                backlight_set_percent(percent);
                settings_dirty = true;
                settings_save_failed = false;
            }
            /* ABOUT is read-only: SELECT deliberately has no effect. */
            break;

        case BUTTON_EVENT_LEFT_SHORT:
        case BUTTON_EVENT_LEFT_LONG:
            return SETTINGS_MENU_ACTION_BACK;

        default:
            break;
    }
    return SETTINGS_MENU_ACTION_NONE;
}

void settings_menu_render(void)
{
    if (selected_item == SETTINGS_ITEM_CMT_SOURCE)
    {
        settings_print_fixed_P(0U, text_source);
        settings_print_fixed_P(1U,
            external_switch_get_source() == CMT_SOURCE_EXTERNAL ?
            text_external : text_internal);
        return;
    }

    if (selected_item == SETTINGS_ITEM_BACKLIGHT)
    {
        char line[17];

        settings_print_fixed_P(0U, text_backlight);
        if (settings_save_failed)
        {
            flash_text_snprintf(line, sizeof(line), PSTR("%3u%% EEPROM ERR"),
                                backlight_get_percent());
        }
        else
        {
            flash_text_snprintf(line, sizeof(line), PSTR(" %3u%%"),
                                backlight_get_percent());
        }
        settings_print_fixed(1U, line);
        return;
    }

    {
        char line[17];

        settings_print_fixed_P(0U, text_about);
        flash_text_snprintf(line, sizeof(line), PSTR("RAM %u MIN%u"),
                            ram_monitor_get_free(),
                            ram_monitor_get_min_free());
        settings_print_fixed(1U, line);
    }
}
