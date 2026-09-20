#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "drivers/lcd.h"
#include "drivers/keypad.h"
#include "drivers/calibration_store.h"
#include "drivers/sdcard.h"
#include "drivers/flash_text.h"
#include "drivers/backlight.h"
#include "drivers/external_switch.h"
#include "drivers/monitor_mute.h"
#include "drivers/ram_monitor.h"

#include "play/play_controller.h"
#include "ui/browser.h"
#include "ui/menu.h"
#include "ui/record_menu.h"
#include "ui/play_screen.h"
#include "ui/record_screen.h"
#include "ui/settings_menu.h"
#include "record/record_engine.h"

#define CALIBRATION_PRESS_DELTA 30U
#define CALIBRATION_RELEASE_DELTA 20U
#define ACTIVE_KEYPAD_POLL_MS 5U
#define ACTIVE_LCD_UPDATE_MS 250U

typedef enum
{
    APP_SCREEN_BROWSER = 0,
    APP_SCREEN_PLAY,
    APP_SCREEN_RECORD,
    APP_SCREEN_PLAY_MENU,
    APP_SCREEN_RECORD_MENU,
    APP_SCREEN_SETTINGS_MENU
} app_screen_t;


static uint16_t last_lcd_update_ms = 0U;
static uint16_t last_active_keypad_poll_ms = 0U;
static bool sd_ok = false;
static app_screen_t current_screen = APP_SCREEN_BROWSER;

/* Fixed root-level destination copied from flash only when RECORD starts. */
static const char recordings_directory_P[] PROGMEM = "/RECORDINGS";
static char recordings_directory[12];

static void lcd_print_line(uint8_t row, const char *text)
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

static void lcd_print_line_P(uint8_t row, PGM_P text)
{
    char line[17];
    flash_text_copy(line, sizeof(line), text);
    lcd_print_line(row, line);
}

static uint16_t adc_average(uint8_t samples)
{
    uint32_t sum = 0UL;
    for (uint8_t i = 0U; i < samples; ++i)
    {
        sum += keypad_read_raw();
        delay(5);
    }
    return (uint16_t)(sum / samples);
}

static uint16_t calibration_adc_diff(uint16_t left, uint16_t right)
{
    return (left > right) ? (uint16_t)(left - right) :
                            (uint16_t)(right - left);
}

static void wait_for_release(uint16_t none_adc)
{
    lcd_print_line_P(0U, PSTR("RELEASE ALL"));
    lcd_print_line_P(1U, PSTR("WAIT..."));
    while (calibration_adc_diff(keypad_read_raw(), none_adc) >
           CALIBRATION_RELEASE_DELTA) delay(20);
    delay(200);
}

static uint16_t calibrate_none(void)
{
    lcd_clear();
    lcd_print_line_P(0U, PSTR("CAL NONE"));
    lcd_print_line_P(1U, PSTR("RELEASE ALL"));
    /* NONE is the reference level. Do not assume it is above 900: different
       keypad shields may use a different ladder direction or range. */
    delay(1000);
    return adc_average(32);
}

static uint16_t calibrate_button(PGM_P name, uint16_t none_adc)
{
    char line0[17];
    char line1[17];
    char name_ram[12];
    flash_text_copy(name_ram, sizeof(name_ram), name);
    flash_text_snprintf(line0, sizeof(line0), PSTR("PRESS %-10s"), name_ram);
    lcd_clear();
    lcd_print_line(0, line0);
    lcd_print_line_P(1U, PSTR("WAIT INPUT"));
    while (calibration_adc_diff(keypad_read_raw(), none_adc) <
           CALIBRATION_PRESS_DELTA) delay(20);
    delay(250);
    uint16_t value = adc_average(32);
    flash_text_snprintf(line1, sizeof(line1), PSTR("ADC:%4u"), value);
    lcd_print_line(0U, name_ram);
    lcd_print_line(1U, line1);
    delay(700);
    wait_for_release(none_adc);
    return value;
}

static bool run_keypad_calibration(keypad_calibration_t *calibration)
{
    if (calibration == NULL) return false;
    lcd_clear();
    lcd_print_line_P(0U, PSTR("KEYPAD"));
    lcd_print_line_P(1U, PSTR("CALIBRATION"));
    delay(1200);
    calibration->none = calibrate_none();
    calibration->up = calibrate_button(PSTR("REWIND"), calibration->none);
    calibration->down = calibrate_button(PSTR("FFWD"), calibration->none);
    calibration->left = calibrate_button(PSTR("STOP"), calibration->none);
    calibration->right = calibrate_button(PSTR("RECORD"), calibration->none);
    calibration->select = calibrate_button(PSTR("PLAY"), calibration->none);
    if (!keypad_calibration_is_valid(calibration))
    {
        lcd_print_line_P(0U, PSTR("CAL INVALID"));
        lcd_print_line_P(1U, PSTR("USING DEFAULT"));
        delay(1800);
        return false;
    }
    if (!calibration_store_save_keypad(calibration))
    {
        /* Keep the measured mapping for this session, but make a real EEPROM
           failure visible instead of silently replacing it with defaults. */
        lcd_print_line_P(0U, PSTR("EEPROM ERROR"));
        lcd_print_line_P(1U, PSTR("NOT SAVED"));
        delay(1800);
        return true;
    }
    lcd_print_line_P(0U, PSTR("CALIBRATION"));
    lcd_print_line_P(1U, PSTR("SAVED"));
    delay(700);
    return true;
}

static void app_enter_browser(void)
{
    browser_restore_saved_position();
    current_screen = APP_SCREEN_BROWSER;
    lcd_clear();
}

static void app_enter_play(const char *filename, const char *directory_path)
{
    browser_save_position();
    browser_clear_status();
    play_controller_start_session(filename, directory_path,
                                  menu_get_invert_signal(),
                                  menu_get_loader_mode(),
                                  menu_get_play_control_mode());
    current_screen = APP_SCREEN_PLAY;
    lcd_clear();
}

static void app_enter_record(void)
{
    record_engine_config_t config;
    browser_save_position();
    config.format = record_menu_get_format();
    config.wav_sample_rate = record_menu_get_wav_sample_rate();
    config.control_mode = record_menu_get_control_mode();
    config.autoname = record_menu_get_autoname();
    (void)record_engine_start(recordings_directory, &config);
    current_screen = APP_SCREEN_RECORD;
    lcd_clear();
}

static void app_handle_browser_event(button_event_t event)
{
    browser_action_t action = browser_handle_event(event);
    switch (action)
    {
        case BROWSER_ACTION_FILE_SELECTED:
            app_enter_play(browser_get_selected_name(), browser_get_current_path());
            break;
        case BROWSER_ACTION_RECORD_REQUESTED:
            app_enter_record();
            break;
        case BROWSER_ACTION_PLAY_MENU_REQUESTED:
            browser_save_position();
            current_screen = APP_SCREEN_PLAY_MENU;
            lcd_clear();
            break;
        case BROWSER_ACTION_RECORD_MENU_REQUESTED:
            browser_save_position();
            current_screen = APP_SCREEN_RECORD_MENU;
            lcd_clear();
            break;
        case BROWSER_ACTION_SETTINGS_REQUESTED:
            browser_save_position();
            settings_menu_init();
            current_screen = APP_SCREEN_SETTINGS_MENU;
            lcd_clear();
            break;
        default:
            break;
    }
}

static void app_handle_play_event(button_event_t event)
{
    switch (play_screen_handle_event(event))
    {
        case PLAY_SCREEN_ACTION_TOGGLE_PLAY:
            play_controller_toggle_play_pause();
            break;
        case PLAY_SCREEN_ACTION_BACK:
            play_controller_stop();
            app_enter_browser();
            break;
        default:
            break;
    }
}

static void app_handle_record_event(button_event_t event)
{
    record_screen_action_t action = record_screen_handle_event(event);
    record_engine_state_t state = record_engine_get_state();

    if (action == RECORD_SCREEN_ACTION_TOGGLE_PAUSE)
    {
        if ((state == RECORD_ENGINE_ARMED) ||
            (state == RECORD_ENGINE_RECORDING) ||
            (state == RECORD_ENGINE_PAUSED))
        {
            record_engine_toggle_pause();
        }
        return;
    }
    if (action == RECORD_SCREEN_ACTION_STOP_SAVE)
    {
        if ((state == RECORD_ENGINE_ARMED) ||
            (state == RECORD_ENGINE_RECORDING) ||
            (state == RECORD_ENGINE_PAUSED))
        {
            record_engine_request_stop();
        }
        else if ((state == RECORD_ENGINE_FINISHED) ||
                 (state == RECORD_ENGINE_CANCELLED) ||
                 (state == RECORD_ENGINE_ERROR) ||
                 (state == RECORD_ENGINE_STOPPED))
        {
            browser_refresh();
            app_enter_browser();
        }
        return;
    }
    if (action == RECORD_SCREEN_ACTION_CANCEL_BACK)
    {
        if ((state == RECORD_ENGINE_ARMED) ||
            (state == RECORD_ENGINE_RECORDING) ||
            (state == RECORD_ENGINE_PAUSED) ||
            (state == RECORD_ENGINE_FINALIZING) ||
            (state == RECORD_ENGINE_ERROR))
        {
            /* Keep the cancellation/deletion confirmation on the RECORD screen. */
            record_engine_cancel();
            lcd_clear();
        }
    }
}

void setup()
{
    ram_monitor_init();
    flash_text_copy(recordings_directory, sizeof(recordings_directory), recordings_directory_P);
    monitor_mute_init();
    external_switch_init();
    backlight_init();
    /* Load CMT source and backlight together from the SYSTEM settings block. */
    settings_menu_init();
    lcd_init();
    keypad_init();
    lcd_clear();

    keypad_calibration_t calibration;
    bool calibration_loaded = calibration_store_load_keypad(&calibration);
    if (calibration_loaded && !keypad_calibration_is_valid(&calibration)) calibration_loaded = false;
    if (calibration_loaded)
    {
        /* Decode the boot-time force key through the stored mapping instead
           of assuming that RIGHT must have an ADC value below 50. */
        keypad_set_calibration(&calibration);
    }
    bool force_calibration = calibration_loaded &&
                             (keypad_get_button() == BUTTON_RIGHT);
    if (!calibration_loaded || force_calibration)
    {
        if (!run_keypad_calibration(&calibration)) keypad_get_default_calibration(&calibration);
    }
    keypad_set_calibration(&calibration);

    /* Optional SD probing is intentionally after EEPROM/keypad setup. A
       missing or slow card can no longer precede the first calibration save. */
    sdcard_early_prepare_pins();
    sd_ok = sdcard_init();

    browser_init(sd_ok);
    menu_init();
    record_menu_init();
    play_controller_init();
    record_engine_init();
    current_screen = APP_SCREEN_BROWSER;
    lcd_clear();
}

void loop()
{
    uint16_t now = (uint16_t)millis();

    if (current_screen == APP_SCREEN_PLAY)
    {
        play_controller_service();
        if ((now - last_active_keypad_poll_ms) >= ACTIVE_KEYPAD_POLL_MS)
        {
            last_active_keypad_poll_ms = now;
            button_event_t event = keypad_get_event();
            if (event != BUTTON_EVENT_NONE) app_handle_play_event(event);
        }
        play_controller_service();
        if ((now - last_lcd_update_ms) >= ACTIVE_LCD_UPDATE_MS)
        {
            play_controller_view_t view;
            last_lcd_update_ms = now;
            play_controller_get_view(&view);
            play_screen_render(&view);
        }
        play_controller_service();
        return;
    }

    if (current_screen == APP_SCREEN_RECORD)
    {
        record_engine_service();
        if ((now - last_active_keypad_poll_ms) >= ACTIVE_KEYPAD_POLL_MS)
        {
            last_active_keypad_poll_ms = now;
            button_event_t event = keypad_get_event();
            if (event != BUTTON_EVENT_NONE) app_handle_record_event(event);
        }
        record_engine_service();
        if ((now - last_lcd_update_ms) >= ACTIVE_LCD_UPDATE_MS)
        {
            last_lcd_update_ms = now;
            record_screen_render();
        }
        record_engine_service();
        return;
    }

    browser_service();
    button_event_t event = keypad_get_event();
    if (event != BUTTON_EVENT_NONE)
    {
        if (current_screen == APP_SCREEN_BROWSER)
        {
            app_handle_browser_event(event);
        }
        else if (current_screen == APP_SCREEN_PLAY_MENU)
        {
            if (menu_handle_event(event) == MENU_ACTION_BACK)
            {
                (void)menu_save_if_dirty();
                app_enter_browser();
            }
        }
        else if (current_screen == APP_SCREEN_RECORD_MENU)
        {
            if (record_menu_handle_event(event) == RECORD_MENU_ACTION_BACK)
            {
                (void)record_menu_save_if_dirty();
                app_enter_browser();
            }
        }
        else if (current_screen == APP_SCREEN_SETTINGS_MENU)
        {
            if (settings_menu_handle_event(event) == SETTINGS_MENU_ACTION_BACK)
            {
                (void)settings_menu_save_if_dirty();
                app_enter_browser();
            }
        }
    }

    if ((now - last_lcd_update_ms) >= 120U)
    {
        last_lcd_update_ms = now;
        switch (current_screen)
        {
            case APP_SCREEN_BROWSER: browser_render(); break;
            case APP_SCREEN_PLAY_MENU: menu_render(); break;
            case APP_SCREEN_RECORD_MENU: record_menu_render(); break;
            case APP_SCREEN_SETTINGS_MENU: settings_menu_render(); break;
            case APP_SCREEN_PLAY:
            {
                play_controller_view_t view;
                play_controller_get_view(&view);
                play_screen_render(&view);
                break;
            }
            case APP_SCREEN_RECORD: record_screen_render(); break;
            default: app_enter_browser(); break;
        }
    }
}
