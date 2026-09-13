#ifndef SD2CMT2_CALIBRATION_STORE_H
#define SD2CMT2_CALIBRATION_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "keypad.h"

typedef struct
{
    uint8_t invert_signal;
    uint8_t loader_mode;
    uint8_t play_control_mode;
    uint8_t tap_speed;
    uint8_t reserved[2];
} play_settings_data_t;

typedef struct
{
    uint8_t format;
    uint8_t wav_rate_44k;
    uint8_t control_mode;
    uint8_t autoname;
} record_settings_data_t;

typedef struct
{
    uint8_t cmt_source;
    uint8_t backlight_percent;
    uint8_t reserved[2];
} system_settings_data_t;

bool calibration_store_load_keypad(keypad_calibration_t *calibration);
bool calibration_store_save_keypad(const keypad_calibration_t *calibration);

bool calibration_store_load_play_settings(play_settings_data_t *settings);
bool calibration_store_save_play_settings(const play_settings_data_t *settings);

bool calibration_store_load_record_settings(record_settings_data_t *settings);
bool calibration_store_save_record_settings(const record_settings_data_t *settings);

bool calibration_store_load_system_settings(system_settings_data_t *settings);
bool calibration_store_save_system_settings(const system_settings_data_t *settings);

#endif
