#include "calibration_store.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/eeprom.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CALIBRATION_STORE_ADDR 0
#define PLAY_SETTINGS_STORE_ADDR 32
#define RECORD_SETTINGS_STORE_ADDR 48
#define SYSTEM_SETTINGS_STORE_ADDR 64

#define CALIBRATION_MAGIC 0x5344324BUL
#define CALIBRATION_VERSION 1
#define PLAY_SETTINGS_MAGIC 0x5344504CUL
#define PLAY_SETTINGS_VERSION 1
#define RECORD_SETTINGS_MAGIC 0x53445243UL
#define RECORD_SETTINGS_VERSION 1
#define SYSTEM_SETTINGS_MAGIC 0x53445359UL
#define SYSTEM_SETTINGS_VERSION 1

typedef struct
{
    uint32_t magic;
    uint16_t version;
    keypad_calibration_t keypad;
    uint16_t checksum;
} calibration_record_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    play_settings_data_t settings;
    uint16_t checksum;
} play_settings_record_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    record_settings_data_t settings;
    uint16_t checksum;
} record_settings_record_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    system_settings_data_t settings;
    uint16_t checksum;
} system_settings_record_t;

static_assert(sizeof(keypad_calibration_t) == 12U,
              "Unexpected keypad calibration layout");
static_assert(sizeof(calibration_record_t) == 20U,
              "Unexpected EEPROM calibration record layout");
static_assert(sizeof(play_settings_data_t) == 6U,
              "Unexpected play settings layout");
static_assert(sizeof(record_settings_data_t) == 4U,
              "Unexpected record settings layout");
static_assert(sizeof(system_settings_data_t) == 4U,
              "Unexpected system settings layout");
static_assert(sizeof(play_settings_record_t) == 14U,
              "Unexpected EEPROM play settings record layout");
static_assert(sizeof(record_settings_record_t) == 12U,
              "Unexpected EEPROM record settings record layout");
static_assert(sizeof(system_settings_record_t) == 12U,
              "Unexpected EEPROM system settings record layout");

static uint16_t calibration_store_checksum_bytes(const void *data,
                                                  size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t checksum = 0U;

    for (size_t i = 0U; i < length; ++i)
    {
        checksum = (uint16_t)(checksum + bytes[i]);
    }
    return checksum;
}

static uint16_t calibration_store_checksum_record(const calibration_record_t *record)
{
    return calibration_store_checksum_bytes(
        record, offsetof(calibration_record_t, checksum));
}

bool calibration_store_load_keypad(keypad_calibration_t *calibration)
{
    if (calibration == NULL) return false;

    calibration_record_t record;
    memset(&record, 0, sizeof(record));
    EEPROM.get(CALIBRATION_STORE_ADDR, record);

    if ((record.magic != CALIBRATION_MAGIC) ||
        (record.version != CALIBRATION_VERSION) ||
        (record.checksum != calibration_store_checksum_record(&record)))
    {
        return false;
    }

    *calibration = record.keypad;
    return true;
}

bool calibration_store_save_keypad(const keypad_calibration_t *calibration)
{
    keypad_calibration_t verified;

    if (calibration == NULL) return false;

    calibration_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = CALIBRATION_MAGIC;
    record.version = CALIBRATION_VERSION;
    record.keypad = *calibration;
    record.checksum = calibration_store_checksum_record(&record);

    EEPROM.put(CALIBRATION_STORE_ADDR, record);
    eeprom_busy_wait();

    /* EEPROM.put() has no status result. Read the complete checksummed record
       back so setup never reports a calibration as saved when it is not. */
    if (!calibration_store_load_keypad(&verified)) return false;
    return memcmp(&verified, calibration, sizeof(verified)) == 0;
}

bool calibration_store_load_play_settings(play_settings_data_t *settings)
{
    play_settings_record_t record;

    if (settings == NULL) return false;
    memset(&record, 0, sizeof(record));
    EEPROM.get(PLAY_SETTINGS_STORE_ADDR, record);

    if ((record.magic != PLAY_SETTINGS_MAGIC) ||
        (record.version != PLAY_SETTINGS_VERSION) ||
        (record.checksum != calibration_store_checksum_bytes(
            &record, offsetof(play_settings_record_t, checksum))))
    {
        return false;
    }

    *settings = record.settings;
    return true;
}

bool calibration_store_save_play_settings(const play_settings_data_t *settings)
{
    play_settings_record_t record;
    play_settings_data_t verified;

    if (settings == NULL) return false;
    memset(&record, 0, sizeof(record));
    record.magic = PLAY_SETTINGS_MAGIC;
    record.version = PLAY_SETTINGS_VERSION;
    record.settings = *settings;
    record.checksum = calibration_store_checksum_bytes(
        &record, offsetof(play_settings_record_t, checksum));

    EEPROM.put(PLAY_SETTINGS_STORE_ADDR, record);
    eeprom_busy_wait();

    return calibration_store_load_play_settings(&verified) &&
           (memcmp(&verified, settings, sizeof(verified)) == 0);
}

bool calibration_store_load_record_settings(record_settings_data_t *settings)
{
    record_settings_record_t record;

    if (settings == NULL) return false;
    memset(&record, 0, sizeof(record));
    EEPROM.get(RECORD_SETTINGS_STORE_ADDR, record);

    if ((record.magic != RECORD_SETTINGS_MAGIC) ||
        (record.version != RECORD_SETTINGS_VERSION) ||
        (record.checksum != calibration_store_checksum_bytes(
            &record, offsetof(record_settings_record_t, checksum))))
    {
        return false;
    }

    *settings = record.settings;
    return true;
}

bool calibration_store_save_record_settings(const record_settings_data_t *settings)
{
    record_settings_record_t record;
    record_settings_data_t verified;

    if (settings == NULL) return false;
    memset(&record, 0, sizeof(record));
    record.magic = RECORD_SETTINGS_MAGIC;
    record.version = RECORD_SETTINGS_VERSION;
    record.settings = *settings;
    record.checksum = calibration_store_checksum_bytes(
        &record, offsetof(record_settings_record_t, checksum));

    EEPROM.put(RECORD_SETTINGS_STORE_ADDR, record);
    eeprom_busy_wait();

    return calibration_store_load_record_settings(&verified) &&
           (memcmp(&verified, settings, sizeof(verified)) == 0);
}

bool calibration_store_load_system_settings(system_settings_data_t *settings)
{
    system_settings_record_t record;

    if (settings == NULL) return false;
    memset(&record, 0, sizeof(record));
    EEPROM.get(SYSTEM_SETTINGS_STORE_ADDR, record);

    if ((record.magic != SYSTEM_SETTINGS_MAGIC) ||
        (record.version != SYSTEM_SETTINGS_VERSION) ||
        (record.checksum != calibration_store_checksum_bytes(
            &record, offsetof(system_settings_record_t, checksum))))
    {
        return false;
    }

    *settings = record.settings;
    return true;
}

bool calibration_store_save_system_settings(const system_settings_data_t *settings)
{
    system_settings_record_t record;
    system_settings_data_t verified;

    if (settings == NULL) return false;
    memset(&record, 0, sizeof(record));
    record.magic = SYSTEM_SETTINGS_MAGIC;
    record.version = SYSTEM_SETTINGS_VERSION;
    record.settings = *settings;
    record.checksum = calibration_store_checksum_bytes(
        &record, offsetof(system_settings_record_t, checksum));

    EEPROM.put(SYSTEM_SETTINGS_STORE_ADDR, record);
    eeprom_busy_wait();

    return calibration_store_load_system_settings(&verified) &&
           (memcmp(&verified, settings, sizeof(verified)) == 0);
}
