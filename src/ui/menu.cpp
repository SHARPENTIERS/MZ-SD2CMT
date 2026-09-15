#include <Arduino.h>
#include <string.h>

#include "menu.h"
#include "../drivers/calibration_store.h"
#include "../drivers/lcd.h"
#include "../drivers/flash_text.h"
#include "../play/tap_speed.h"

typedef enum
{
    MENU_ITEM_INVERT_SIGNAL = 0,
    MENU_ITEM_PLAY_CONTROL,
    MENU_ITEM_LOADER,
    MENU_ITEM_SPEED,
    MENU_ITEM_TAP_SPEED,
    MENU_ITEM_COUNT
} menu_item_t;

typedef enum
{
    MENU_LOADER_NORMAL = 0,
    MENU_LOADER_AUTO,
    MENU_LOADER_UL,
    MENU_LOADER_UL_MZ800,
    MENU_LOADER_UL_MZ700,
    MENU_LOADER_MZ700,
    MENU_LOADER_IC,
    MENU_LOADER_TC,
    MENU_LOADER_COUNT
} menu_loader_t;

typedef enum
{
    MENU_SPEED_1_1 = 0,
    MENU_SPEED_1_2,
    MENU_SPEED_1_3,
    MENU_SPEED_1_4,
    MENU_SPEED_COUNT
} menu_speed_t;

typedef enum
{
    MENU_SPEED_SLOT_NORMAL = 0,
    MENU_SPEED_SLOT_MZ700,
    MENU_SPEED_SLOT_IC,
    MENU_SPEED_SLOT_TC,
    MENU_SPEED_SLOT_COUNT
} menu_speed_slot_t;

#define MENU_SPEED_MEMORY_MARKER 0xA5U

static menu_item_t selected_item = MENU_ITEM_INVERT_SIGNAL;
static bool invert_signal = false;
static menu_loader_t loader_mode = MENU_LOADER_NORMAL;
static menu_speed_t loader_speed = MENU_SPEED_1_1;
/* Four two-bit per-loader speeds use the same packed representation in RAM
   and EEPROM. */
static uint8_t loader_speeds_packed;
static play_control_mode_t play_control_mode = PLAY_CONTROL_MOTOR;
static play_settings_data_t saved_settings;
static bool settings_dirty = false;

static const char text_invert[] PROGMEM = ">WAV INVERT";
static const char text_play_control[] PROGMEM = ">PLAY CTRL";
static const char text_loader[] PROGMEM = ">LOADER";
static const char text_speed[] PROGMEM = ">SPEED";
static const char text_tap_speed[] PROGMEM = ">TAP SPEED";
static const char text_on[] PROGMEM = " ON";
static const char text_off[] PROGMEM = " OFF";
static const char text_normal[] PROGMEM = " NORMAL";
static const char text_auto[] PROGMEM = " AUTO";
static const char text_ul[] PROGMEM = " UL";
static const char text_ul_mz800[] PROGMEM = " UL MZ800";
static const char text_ul_mz700[] PROGMEM = " UL MZ700";
static const char text_mz700[] PROGMEM = " MZ700";
static const char text_ic[] PROGMEM = " IC";
static const char text_tc[] PROGMEM = " TC";
static const char text_speed_none[] PROGMEM = " --";
static const char text_speed_1_1[] PROGMEM = " 1:1";
static const char text_speed_1_4[] PROGMEM = " 1:4";
static const char text_speed_1_3[] PROGMEM = " 1:3";
static const char text_speed_1_2[] PROGMEM = " 1:2";
static const char text_tap_1400[] PROGMEM = " 1400 Bd";
static const char text_tap_1800[] PROGMEM = " 1800 Bd";
static const char text_tap_2100[] PROGMEM = " 2100 Bd";
static const char text_tap_2500[] PROGMEM = " 2500 Bd";
static const char text_motor[] PROGMEM = " MOTOR";
static const char text_manual[] PROGMEM = " MANUAL";
static const char text_motor_label[] PROGMEM = "MOTOR";
static const char text_manual_label[] PROGMEM = "MANUAL";

static const char * const loader_labels_P[MENU_LOADER_COUNT] PROGMEM =
{
    text_normal, text_auto, text_ul, text_ul_mz800,
    text_ul_mz700, text_mz700, text_ic, text_tc
};
static const char * const speed_labels_P[MENU_SPEED_COUNT] PROGMEM =
{
    text_speed_1_1, text_speed_1_2, text_speed_1_3, text_speed_1_4
};
static const char * const tap_speed_labels_P[TAP_SPEED_COUNT] PROGMEM =
{
    text_tap_1400, text_tap_1800, text_tap_2100, text_tap_2500
};

static void lcd_print_fixed_P(uint8_t row, PGM_P text)
{
    char line[17];
    uint8_t length;

    flash_text_copy(line, sizeof(line), text);
    for (length = 0U; (length < 16U) && (line[length] != '\0'); ++length) {}
    while (length < 16U) line[length++] = ' ';
    line[16] = '\0';
    lcd_set_cursor(0, row);
    lcd_print(line);
}

PGM_P play_control_mode_label_P(play_control_mode_t mode)
{
    return (mode == PLAY_CONTROL_MANUAL) ? text_manual_label : text_motor_label;
}

static bool loader_uses_speed(void)
{
    return (loader_mode == MENU_LOADER_NORMAL) ||
           (loader_mode == MENU_LOADER_MZ700) ||
           (loader_mode == MENU_LOADER_IC) ||
           (loader_mode == MENU_LOADER_TC);
}

static bool loader_speed_slot(menu_loader_t mode, menu_speed_slot_t *slot)
{
    if (slot == NULL) return false;
    switch (mode)
    {
        case MENU_LOADER_NORMAL: *slot = MENU_SPEED_SLOT_NORMAL; return true;
        case MENU_LOADER_MZ700: *slot = MENU_SPEED_SLOT_MZ700; return true;
        case MENU_LOADER_IC: *slot = MENU_SPEED_SLOT_IC; return true;
        case MENU_LOADER_TC: *slot = MENU_SPEED_SLOT_TC; return true;
        default: return false;
    }
}

static menu_speed_t normalized_speed(menu_loader_t mode, menu_speed_t speed)
{
    if ((uint8_t)speed >= (uint8_t)MENU_SPEED_COUNT)
        speed = MENU_SPEED_1_1;

    if ((mode == MENU_LOADER_MZ700) &&
        (speed != MENU_SPEED_1_1) && (speed != MENU_SPEED_1_3))
        return MENU_SPEED_1_1;
    if ((mode == MENU_LOADER_IC) && (speed == MENU_SPEED_1_1))
        return MENU_SPEED_1_4;
    if ((mode == MENU_LOADER_TC) &&
        (speed != MENU_SPEED_1_2) && (speed != MENU_SPEED_1_3))
        return MENU_SPEED_1_3;
    return speed;
}

static menu_loader_t loader_mode_for_speed_slot(menu_speed_slot_t slot)
{
    if (slot == MENU_SPEED_SLOT_NORMAL) return MENU_LOADER_NORMAL;
    return (menu_loader_t)((uint8_t)MENU_LOADER_MZ700 +
                           (uint8_t)slot - 1U);
}

static menu_speed_t stored_loader_speed(menu_speed_slot_t slot)
{
    return (menu_speed_t)((loader_speeds_packed >>
                           ((uint8_t)slot * 2U)) & 0x03U);
}

static void store_loader_speed(menu_speed_slot_t slot, menu_speed_t speed)
{
    const uint8_t shift = (uint8_t)slot * 2U;
    const uint8_t mask = (uint8_t)(0x03U << shift);
    loader_speeds_packed =
        (uint8_t)((loader_speeds_packed & (uint8_t)~mask) |
                  ((uint8_t)speed << shift));
}

static void reset_loader_speeds(void)
{
    loader_speeds_packed = 0U;
    store_loader_speed(MENU_SPEED_SLOT_NORMAL, MENU_SPEED_1_1);
    store_loader_speed(MENU_SPEED_SLOT_MZ700, MENU_SPEED_1_1);
    store_loader_speed(MENU_SPEED_SLOT_IC, MENU_SPEED_1_4);
    store_loader_speed(MENU_SPEED_SLOT_TC, MENU_SPEED_1_3);
}

static void remember_loader_speed(void)
{
    menu_speed_slot_t slot;
    if (!loader_speed_slot(loader_mode, &slot)) return;
    loader_speed = normalized_speed(loader_mode, loader_speed);
    store_loader_speed(slot, loader_speed);
}

static void restore_loader_speed(void)
{
    menu_speed_slot_t slot;
    if (!loader_speed_slot(loader_mode, &slot))
    {
        loader_speed = MENU_SPEED_1_1;
        return;
    }
    loader_speed = normalized_speed(loader_mode,
                                    stored_loader_speed(slot));
}

static uint8_t pack_loader_speeds(void)
{
    return loader_speeds_packed;
}

static void unpack_loader_speeds(uint8_t packed)
{
    loader_speeds_packed = 0U;
    for (uint8_t slot = 0U; slot < (uint8_t)MENU_SPEED_SLOT_COUNT; ++slot)
    {
        const menu_speed_slot_t speed_slot = (menu_speed_slot_t)slot;
        const menu_loader_t mode = loader_mode_for_speed_slot(speed_slot);
        const menu_speed_t speed =
            (menu_speed_t)((packed >> (slot * 2U)) & 0x03U);
        store_loader_speed(speed_slot, normalized_speed(mode, speed));
    }
}

static PGM_P loader_label_P(void)
{
    const uint8_t index = ((uint8_t)loader_mode < (uint8_t)MENU_LOADER_COUNT) ?
        (uint8_t)loader_mode : (uint8_t)MENU_LOADER_NORMAL;
    return (PGM_P)pgm_read_word(&loader_labels_P[index]);
}

static PGM_P speed_label_P(void)
{
    if (!loader_uses_speed()) return text_speed_none;
    const uint8_t index = ((uint8_t)loader_speed < (uint8_t)MENU_SPEED_COUNT) ?
        (uint8_t)loader_speed : (uint8_t)MENU_SPEED_1_3;
    return (PGM_P)pgm_read_word(&speed_labels_P[index]);
}

static PGM_P tap_speed_label_P(void)
{
    const uint8_t speed = (uint8_t)tap_speed_get();
    const uint8_t index = (speed < (uint8_t)TAP_SPEED_COUNT) ? speed : 0U;
    return (PGM_P)pgm_read_word(&tap_speed_labels_P[index]);
}

static void cycle_loader(void)
{
    remember_loader_speed();
    loader_mode = (menu_loader_t)(((uint8_t)loader_mode + 1U) %
                                  (uint8_t)MENU_LOADER_COUNT);
    restore_loader_speed();
}

static void cycle_speed(void)
{
    if (loader_mode == MENU_LOADER_NORMAL)
    {
        if (loader_speed == MENU_SPEED_1_1)
            loader_speed = MENU_SPEED_1_2;
        else if (loader_speed == MENU_SPEED_1_2)
            loader_speed = MENU_SPEED_1_3;
        else if (loader_speed == MENU_SPEED_1_3)
            loader_speed = MENU_SPEED_1_4;
        else
            loader_speed = MENU_SPEED_1_1;
    }
    else if (loader_mode == MENU_LOADER_MZ700)
    {
        loader_speed = (loader_speed == MENU_SPEED_1_1) ?
            MENU_SPEED_1_3 : MENU_SPEED_1_1;
    }
    else if (loader_mode == MENU_LOADER_IC)
    {
        if (loader_speed == MENU_SPEED_1_4)
            loader_speed = MENU_SPEED_1_3;
        else if (loader_speed == MENU_SPEED_1_3)
            loader_speed = MENU_SPEED_1_2;
        else
            loader_speed = MENU_SPEED_1_4;
    }
    else if (loader_mode == MENU_LOADER_TC)
    {
        if (loader_speed == MENU_SPEED_1_3)
            loader_speed = MENU_SPEED_1_2;
        else
            loader_speed = MENU_SPEED_1_3;
    }
    remember_loader_speed();
}

static void move_selection(int8_t direction)
{
    int8_t next = (int8_t)selected_item + direction;
    if (next < 0) next = (int8_t)MENU_ITEM_COUNT - 1;
    if (next >= (int8_t)MENU_ITEM_COUNT) next = 0;
    selected_item = (menu_item_t)next;
}

static void apply_loader_mode(loader_mode_t mode)
{
    switch (mode)
    {
        case LOADER_MODE_NORMAL_1_2:
            loader_mode = MENU_LOADER_NORMAL;
            loader_speed = MENU_SPEED_1_2;
            break;
        case LOADER_MODE_NORMAL_1_3:
            loader_mode = MENU_LOADER_NORMAL;
            loader_speed = MENU_SPEED_1_3;
            break;
        case LOADER_MODE_NORMAL_1_4:
            loader_mode = MENU_LOADER_NORMAL;
            loader_speed = MENU_SPEED_1_4;
            break;
        case LOADER_MODE_AUTO:
            loader_mode = MENU_LOADER_AUTO;
            loader_speed = MENU_SPEED_1_1;
            break;
        case LOADER_MODE_UL:
            loader_mode = MENU_LOADER_UL;
            loader_speed = MENU_SPEED_1_1;
            break;
        case LOADER_MODE_UL_MZ800:
            loader_mode = MENU_LOADER_UL_MZ800;
            loader_speed = MENU_SPEED_1_1;
            break;
        case LOADER_MODE_UL_MZ700:
            loader_mode = MENU_LOADER_UL_MZ700;
            loader_speed = MENU_SPEED_1_1;
            break;
        case LOADER_MODE_MZ700_3X:
            loader_mode = MENU_LOADER_MZ700;
            loader_speed = MENU_SPEED_1_3;
            break;
        case LOADER_MODE_MZ700_1X:
            loader_mode = MENU_LOADER_MZ700;
            loader_speed = MENU_SPEED_1_1;
            break;
        case LOADER_MODE_IC_1_4:
            loader_mode = MENU_LOADER_IC;
            loader_speed = MENU_SPEED_1_4;
            break;
        case LOADER_MODE_IC_1_3:
            loader_mode = MENU_LOADER_IC;
            loader_speed = MENU_SPEED_1_3;
            break;
        case LOADER_MODE_IC_1_2:
            loader_mode = MENU_LOADER_IC;
            loader_speed = MENU_SPEED_1_2;
            break;
        case LOADER_MODE_TC_1_2:
            loader_mode = MENU_LOADER_TC;
            loader_speed = MENU_SPEED_1_2;
            break;
        case LOADER_MODE_TC_1_3:
            loader_mode = MENU_LOADER_TC;
            loader_speed = MENU_SPEED_1_3;
            break;
        case LOADER_MODE_NORMAL_1_1:
        default:
            loader_mode = MENU_LOADER_NORMAL;
            loader_speed = MENU_SPEED_1_1;
            break;
    }
}

loader_mode_t menu_get_loader_mode(void)
{
    switch (loader_mode)
    {
        case MENU_LOADER_NORMAL:
            if (loader_speed == MENU_SPEED_1_2) return LOADER_MODE_NORMAL_1_2;
            if (loader_speed == MENU_SPEED_1_3) return LOADER_MODE_NORMAL_1_3;
            if (loader_speed == MENU_SPEED_1_4) return LOADER_MODE_NORMAL_1_4;
            return LOADER_MODE_NORMAL_1_1;
        case MENU_LOADER_AUTO:
            return LOADER_MODE_AUTO;
        case MENU_LOADER_UL:
            return LOADER_MODE_UL;
        case MENU_LOADER_UL_MZ800:
            return LOADER_MODE_UL_MZ800;
        case MENU_LOADER_UL_MZ700:
            return LOADER_MODE_UL_MZ700;
        case MENU_LOADER_MZ700:
            return (loader_speed == MENU_SPEED_1_3) ?
                LOADER_MODE_MZ700_3X : LOADER_MODE_MZ700_1X;
        case MENU_LOADER_IC:
            if (loader_speed == MENU_SPEED_1_2) return LOADER_MODE_IC_1_2;
            if (loader_speed == MENU_SPEED_1_3) return LOADER_MODE_IC_1_3;
            return LOADER_MODE_IC_1_4;
        case MENU_LOADER_TC:
            if (loader_speed == MENU_SPEED_1_2) return LOADER_MODE_TC_1_2;
            return LOADER_MODE_TC_1_3;
        default:
            return LOADER_MODE_NORMAL_1_1;
    }
}

static void collect_settings(play_settings_data_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->invert_signal = invert_signal ? 1U : 0U;
    settings->loader_mode = (uint8_t)menu_get_loader_mode();
    settings->play_control_mode = (uint8_t)play_control_mode;
    settings->tap_speed = (uint8_t)tap_speed_get();
    settings->reserved[0] = MENU_SPEED_MEMORY_MARKER;
    settings->reserved[1] = pack_loader_speeds();
}

static bool stored_settings_valid(const play_settings_data_t *settings)
{
    return (settings != NULL) &&
           (settings->invert_signal <= 1U) &&
           (settings->loader_mode < (uint8_t)LOADER_MODE_COUNT) &&
           (settings->play_control_mode <= (uint8_t)PLAY_CONTROL_MANUAL) &&
           (settings->tap_speed < (uint8_t)TAP_SPEED_COUNT);
}

void menu_init(void)
{
    play_settings_data_t stored;

    selected_item = MENU_ITEM_INVERT_SIGNAL;
    invert_signal = false;
    loader_mode = MENU_LOADER_NORMAL;
    loader_speed = MENU_SPEED_1_1;
    reset_loader_speeds();
    play_control_mode = PLAY_CONTROL_MOTOR;
    (void)tap_speed_set(TAP_SPEED_1400);

    if (calibration_store_load_play_settings(&stored) &&
        stored_settings_valid(&stored))
    {
        invert_signal = stored.invert_signal != 0U;
        apply_loader_mode((loader_mode_t)stored.loader_mode);
        if (stored.reserved[0] == MENU_SPEED_MEMORY_MARKER)
        {
            unpack_loader_speeds(stored.reserved[1]);
            restore_loader_speed();
        }
        else
        {
            /* Version-1 records stored only the selected loader/speed pair. */
            remember_loader_speed();
        }
        play_control_mode = (play_control_mode_t)stored.play_control_mode;
        (void)tap_speed_set((tap_speed_t)stored.tap_speed);
    }

    collect_settings(&saved_settings);
    settings_dirty = false;
}

bool menu_save_if_dirty(void)
{
    play_settings_data_t current;

    if (!settings_dirty) return true;
    collect_settings(&current);
    if (memcmp(&current, &saved_settings, sizeof(current)) == 0)
    {
        settings_dirty = false;
        return true;
    }
    if (!calibration_store_save_play_settings(&current)) return false;
    saved_settings = current;
    settings_dirty = false;
    return true;
}

menu_action_t menu_handle_event(button_event_t event)
{
    switch (event)
    {
        case BUTTON_EVENT_UP_PRESS:
        case BUTTON_EVENT_UP_REPEAT:
            move_selection(-1);
            break;
        case BUTTON_EVENT_DOWN_PRESS:
        case BUTTON_EVENT_DOWN_REPEAT:
            move_selection(1);
            break;
        case BUTTON_EVENT_SELECT_SHORT:
            if (selected_item == MENU_ITEM_INVERT_SIGNAL)
            {
                invert_signal = !invert_signal;
            }
            else if (selected_item == MENU_ITEM_PLAY_CONTROL)
            {
                play_control_mode = (play_control_mode == PLAY_CONTROL_MOTOR) ?
                    PLAY_CONTROL_MANUAL : PLAY_CONTROL_MOTOR;
            }
            else if (selected_item == MENU_ITEM_LOADER)
            {
                cycle_loader();
            }
            else if (selected_item == MENU_ITEM_SPEED)
            {
                cycle_speed();
            }
            else
            {
                tap_speed_cycle();
            }
            settings_dirty = true;
            break;
        case BUTTON_EVENT_LEFT_SHORT:
        case BUTTON_EVENT_LEFT_LONG:
            return MENU_ACTION_BACK;
        default:
            break;
    }
    return MENU_ACTION_NONE;
}

void menu_render(void)
{
    if (selected_item == MENU_ITEM_INVERT_SIGNAL)
    {
        lcd_print_fixed_P(0U, text_invert);
        lcd_print_fixed_P(1U, invert_signal ? text_on : text_off);
        return;
    }

    if (selected_item == MENU_ITEM_PLAY_CONTROL)
    {
        lcd_print_fixed_P(0U, text_play_control);
        lcd_print_fixed_P(1U, play_control_mode == PLAY_CONTROL_MANUAL ?
                          text_manual : text_motor);
        return;
    }

    if (selected_item == MENU_ITEM_LOADER)
    {
        lcd_print_fixed_P(0U, text_loader);
        lcd_print_fixed_P(1U, loader_label_P());
        return;
    }

    if (selected_item == MENU_ITEM_SPEED)
    {
        lcd_print_fixed_P(0U, text_speed);
        lcd_print_fixed_P(1U, speed_label_P());
        return;
    }

    lcd_print_fixed_P(0U, text_tap_speed);
    lcd_print_fixed_P(1U, tap_speed_label_P());
}

bool menu_get_invert_signal(void) { return invert_signal; }
play_control_mode_t menu_get_play_control_mode(void) { return play_control_mode; }
