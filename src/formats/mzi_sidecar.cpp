#include "mzi_sidecar.h"

#include <Arduino.h>
#include <string.h>

#include "../drivers/flash_text.h"
#include "../drivers/sdcard.h"
#include "../record/record_path_buffer.h"
#include "../streams/cmt_mode_scratch.h"
#include "../streams/wav_sample_stream.h"

#define MFI_TEXT_MAX 128U

static const char mzi_normal_1_1_P[] PROGMEM =
    "TYPE=NORMAL\nSPEED=1:1\n";
static const char mzi_normal_1_2_P[] PROGMEM =
    "TYPE=NORMAL\nSPEED=1:2\n";
static const char mzi_normal_1_3_P[] PROGMEM =
    "TYPE=NORMAL\nSPEED=1:3\n";
static const char mzi_normal_1_4_P[] PROGMEM =
    "TYPE=NORMAL\nSPEED=1:4\n";
static const char mzi_mz700_1_1_P[] PROGMEM =
    "TYPE=MZ700\nSPEED=1:1\n";
static const char mzi_mz700_1_3_P[] PROGMEM =
    "TYPE=MZ700\nSPEED=1:3\n";
static const char mzi_ic_1_4_P[] PROGMEM =
    "TYPE=IC\nSPEED=1:4\n";
static const char mzi_ic_1_3_P[] PROGMEM =
    "TYPE=IC\nSPEED=1:3\n";
static const char mzi_ic_1_2_P[] PROGMEM =
    "TYPE=IC\nSPEED=1:2\n";
static const char mzi_tc_1_3_P[] PROGMEM =
    "TYPE=TC\nSPEED=1:3\n";
static const char mzi_tc_1_4_P[] PROGMEM =
    "TYPE=TC\nSPEED=1:4\n";
static const char mzi_tc_1_2_P[] PROGMEM =
    "TYPE=TC\nSPEED=1:2\n";
static const char mzi_ul_P[] PROGMEM =
    "TYPE=UL\n";
static const char mzi_ul_mz800_P[] PROGMEM =
    "TYPE=UL_MZ800\n";
static const char mzi_ul_mz700_P[] PROGMEM =
    "TYPE=UL_MZ700\n";

/* AVR-GCC otherwise builds the switch return table in .data.  Store these
   immutable flash pointers in flash as well.  Order follows loader_mode_t. */
static const char * const mzi_loader_texts_P[LOADER_MODE_COUNT] PROGMEM =
{
    mzi_normal_1_1_P, NULL, mzi_ul_P, mzi_ul_mz800_P,
    mzi_ul_mz700_P, mzi_mz700_1_3_P, mzi_ic_1_4_P, mzi_ic_1_3_P,
    mzi_ic_1_2_P, mzi_tc_1_3_P, mzi_tc_1_2_P, mzi_normal_1_2_P,
    mzi_normal_1_3_P, mzi_mz700_1_1_P, mzi_normal_1_4_P, mzi_tc_1_4_P
};

static char ascii_upper(char value)
{
    if ((value >= 'a') && (value <= 'z'))
        return (char)(value - ('a' - 'A'));
    return value;
}

static bool text_equals_ci_P(const char *left, PGM_P right)
{
    if ((left == NULL) || (right == NULL)) return false;
    for (;;)
    {
        const char right_value = (char)pgm_read_byte(right);
        if (ascii_upper(*left) != ascii_upper(right_value)) return false;
        if (*left == '\0') return true;
        ++left;
        ++right;
    }
}

static bool make_info_path(char *destination, size_t destination_size,
                           const char *source_path, char source_suffix,
                           char info_middle)
{
    size_t length;

    if ((destination == NULL) || (destination_size == 0U) ||
        (source_path == NULL)) return false;

    length = strlen(source_path);
    if ((length < 4U) || (length >= destination_size) ||
        (source_path[length - 4U] != '.') ||
        (ascii_upper(source_path[length - 3U]) != 'M') ||
        (ascii_upper(source_path[length - 2U]) != 'Z') ||
        (ascii_upper(source_path[length - 1U]) != source_suffix))
    {
        return false;
    }

    memcpy(destination, source_path, length + 1U);
    destination[length - 3U] = 'M';
    destination[length - 2U] = info_middle;
    destination[length - 1U] = 'I';
    return true;
}

static bool make_mfi_path(char *destination, size_t destination_size,
                          const char *mzf_path)
{
    return make_info_path(destination, destination_size, mzf_path, 'F', 'F');
}

static bool make_mti_path(char *destination, size_t destination_size,
                          const char *mzt_path)
{
    return make_info_path(destination, destination_size, mzt_path, 'T', 'T');
}

static bool mzi_read_field_P(const char *text, PGM_P key,
                             char *value, size_t value_size)
{
    const char *line;
    size_t key_length;

    if ((text == NULL) || (key == NULL) || (value == NULL) ||
        (value_size == 0U)) return false;

    key_length = strlen_P(key);
    line = text;

    while (*line != '\0')
    {
        const char *end;
        size_t line_length;
        size_t field_length;

        while ((*line == '\r') || (*line == '\n')) ++line;
        if (*line == '\0') break;

        end = line;
        while ((*end != '\0') && (*end != '\r') && (*end != '\n')) ++end;
        line_length = (size_t)(end - line);

        if ((line_length > (key_length + 1U)) &&
            (memcmp_P(line, key, key_length) == 0) &&
            (line[key_length] == '='))
        {
            field_length = line_length - key_length - 1U;
            if (field_length >= value_size) return false;
            memcpy(value, line + key_length + 1U, field_length);
            value[field_length] = '\0';
            return true;
        }

        line = end;
    }

    return false;
}

static PGM_P loader_text_P(loader_mode_t loader_mode)
{
    if ((uint8_t)loader_mode >= (uint8_t)LOADER_MODE_COUNT) return NULL;
    return (PGM_P)pgm_read_word(&mzi_loader_texts_P[(uint8_t)loader_mode]);
}

static bool loader_from_fields(const char *type, const char *speed,
                               loader_mode_t *loader_mode)
{
    if ((type == NULL) || (loader_mode == NULL)) return false;

    if (text_equals_ci_P(type, PSTR("UL")))
    {
        *loader_mode = LOADER_MODE_UL;
        return true;
    }
    if (text_equals_ci_P(type, PSTR("UL_MZ800")))
    {
        *loader_mode = LOADER_MODE_UL_MZ800;
        return true;
    }
    if (text_equals_ci_P(type, PSTR("UL_MZ700")))
    {
        *loader_mode = LOADER_MODE_UL_MZ700;
        return true;
    }

    if ((speed == NULL) || (speed[0] == '\0')) return false;

    if (text_equals_ci_P(type, PSTR("NORMAL")))
    {
        if (strcmp_P(speed, PSTR("1:1")) == 0) *loader_mode = LOADER_MODE_NORMAL_1_1;
        else if (strcmp_P(speed, PSTR("1:2")) == 0) *loader_mode = LOADER_MODE_NORMAL_1_2;
        else if (strcmp_P(speed, PSTR("1:3")) == 0) *loader_mode = LOADER_MODE_NORMAL_1_3;
        else if (strcmp_P(speed, PSTR("1:4")) == 0) *loader_mode = LOADER_MODE_NORMAL_1_4;
        else return false;
        return true;
    }

    if (text_equals_ci_P(type, PSTR("MZ700")))
    {
        if (strcmp_P(speed, PSTR("1:1")) == 0) *loader_mode = LOADER_MODE_MZ700_1X;
        else if (strcmp_P(speed, PSTR("1:3")) == 0) *loader_mode = LOADER_MODE_MZ700_3X;
        else return false;
        return true;
    }

    if (text_equals_ci_P(type, PSTR("IC")))
    {
        if (strcmp_P(speed, PSTR("1:4")) == 0) *loader_mode = LOADER_MODE_IC_1_4;
        else if (strcmp_P(speed, PSTR("1:3")) == 0) *loader_mode = LOADER_MODE_IC_1_3;
        else if (strcmp_P(speed, PSTR("1:2")) == 0) *loader_mode = LOADER_MODE_IC_1_2;
        else return false;
        return true;
    }

    if (text_equals_ci_P(type, PSTR("TC")))
    {
        if (strcmp_P(speed, PSTR("1:4")) == 0) *loader_mode = LOADER_MODE_TC_1_4;
        else if (strcmp_P(speed, PSTR("1:3")) == 0) *loader_mode = LOADER_MODE_TC_1_3;
        else if (strcmp_P(speed, PSTR("1:2")) == 0) *loader_mode = LOADER_MODE_TC_1_2;
        else return false;
        return true;
    }

    return false;
}

static bool loader_from_text(const char *text, loader_mode_t *loader_mode)
{
    char type[12];
    char speed[5];

    if ((loader_mode == NULL) ||
        !mzi_read_field_P(text, PSTR("TYPE"), type, sizeof(type)))
    {
        return false;
    }

    speed[0] = '\0';
    (void)mzi_read_field_P(text, PSTR("SPEED"), speed, sizeof(speed));
    return loader_from_fields(type, speed, loader_mode);
}

static bool parse_record_number(const char *text, uint16_t *value)
{
    uint16_t result = 0U;

    if ((text == NULL) || (value == NULL) || (*text == '\0')) return false;
    while (*text != '\0')
    {
        uint8_t digit;
        if ((*text < '0') || (*text > '9')) return false;
        digit = (uint8_t)(*text - '0');
        if ((result > 6553U) || ((result == 6553U) && (digit > 5U)))
            return false;
        result = (uint16_t)(result * 10U + digit);
        ++text;
    }
    if (result == 0U) return false;
    *value = result;
    return true;
}

static bool mti_accept_line(char *line, uint16_t wanted_record,
                            bool *target_active, char *type, size_t type_size,
                            char *speed, size_t speed_size,
                            loader_mode_t *loader_mode, bool *done)
{
    uint16_t record;

    if ((line == NULL) || (target_active == NULL) || (type == NULL) ||
        (speed == NULL) || (loader_mode == NULL) || (done == NULL))
        return false;

    if (strncmp_P(line, PSTR("RECORD="), 7U) == 0)
    {
        if (*target_active)
        {
            *done = true;
            return loader_from_fields(type, speed, loader_mode);
        }
        if (!parse_record_number(line + 7U, &record)) return true;
        *target_active = (record == wanted_record);
        if (*target_active)
        {
            type[0] = '\0';
            speed[0] = '\0';
        }
        return true;
    }

    if (!*target_active) return true;

    if (strncmp_P(line, PSTR("TYPE="), 5U) == 0)
    {
        size_t length = strlen(line + 5U);
        if (length >= type_size) return false;
        memcpy(type, line + 5U, length + 1U);
    }
    else if (strncmp_P(line, PSTR("SPEED="), 6U) == 0)
    {
        size_t length = strlen(line + 6U);
        if (length >= speed_size) return false;
        memcpy(speed, line + 6U, length + 1U);
    }
    return true;
}

bool mzi_record_profile_get_loader_mode(mzi_record_profile_t profile,
                                        loader_mode_t *loader_mode)
{
    if ((loader_mode == NULL) ||
        ((uint8_t)profile >= (uint8_t)MZI_RECORD_PROFILE_COUNT))
    {
        return false;
    }

    switch (profile)
    {
        case MZI_RECORD_PROFILE_NORMAL_1_2: *loader_mode = LOADER_MODE_NORMAL_1_2; break;
        case MZI_RECORD_PROFILE_NORMAL_1_3: *loader_mode = LOADER_MODE_NORMAL_1_3; break;
        case MZI_RECORD_PROFILE_NORMAL_1_4: *loader_mode = LOADER_MODE_NORMAL_1_4; break;
        case MZI_RECORD_PROFILE_IC_1_4: *loader_mode = LOADER_MODE_IC_1_4; break;
        case MZI_RECORD_PROFILE_IC_1_3: *loader_mode = LOADER_MODE_IC_1_3; break;
        case MZI_RECORD_PROFILE_IC_1_2: *loader_mode = LOADER_MODE_IC_1_2; break;
        case MZI_RECORD_PROFILE_TC_1_3: *loader_mode = LOADER_MODE_TC_1_3; break;
        case MZI_RECORD_PROFILE_TC_1_4: *loader_mode = LOADER_MODE_TC_1_4; break;
        case MZI_RECORD_PROFILE_TC_1_2: *loader_mode = LOADER_MODE_TC_1_2; break;
        case MZI_RECORD_PROFILE_NORMAL_1_1:
        default: *loader_mode = LOADER_MODE_NORMAL_1_1; break;
    }
    return true;
}

bool mzi_sidecar_write_loader_for_mzf(const char *mzf_path,
                                      loader_mode_t loader_mode)
{
    uint8_t *workspace = wav_sample_stream_get_shared_work_buffer();
    char *mfi_path = (char *)workspace;
    char *text = (char *)(workspace + RECORD_PATH_BUFFER_MAX);
    PGM_P source_text = loader_text_P(loader_mode);
    size_t text_length;
    bool ok;

    if (source_text == NULL) return false;
    if (!make_mfi_path(mfi_path, RECORD_PATH_BUFFER_MAX, mzf_path)) return false;

    flash_text_copy(text, MFI_TEXT_MAX, source_text);
    text_length = strlen(text);

    /* O_TRUNC makes the sidecar content authoritative for this basename. */
    if (!sdcard_file_open_write(mfi_path)) return false;
    ok = (sdcard_file_write(text, (uint16_t)text_length) ==
          (int16_t)text_length) && sdcard_file_sync();
    sdcard_file_close();
    return ok;
}

bool mzi_sidecar_write_for_mzf(const char *mzf_path,
                               mzi_record_profile_t profile)
{
    loader_mode_t loader_mode;
    return mzi_record_profile_get_loader_mode(profile, &loader_mode) &&
           mzi_sidecar_write_loader_for_mzf(mzf_path, loader_mode);
}

bool mzi_sidecar_exists_for_mzf(const char *mzf_path)
{
    uint8_t *workspace = wav_sample_stream_get_shared_work_buffer();
    char *mfi_path = (char *)workspace;

    return make_mfi_path(mfi_path, RECORD_PATH_BUFFER_MAX, mzf_path) &&
           sdcard_file_exists(mfi_path);
}

bool mzi_sidecar_exists_for_mzt(const char *mzt_path)
{
    uint8_t *workspace = wav_sample_stream_get_shared_work_buffer();
    char *mti_path = (char *)workspace;

    return make_mti_path(mti_path, RECORD_PATH_BUFFER_MAX, mzt_path) &&
           sdcard_file_exists(mti_path);
}

bool mzi_sidecar_read_loader_for_mzf(const char *mzf_path,
                                     loader_mode_t *loader_mode)
{
    uint8_t *workspace = wav_sample_stream_get_shared_work_buffer();
    char *mfi_path = (char *)workspace;
    char *text = (char *)(workspace + RECORD_PATH_BUFFER_MAX);
    uint32_t file_size;
    int16_t bytes_read;
    bool ok;

    if (loader_mode == NULL) return false;
    if (!make_mfi_path(mfi_path, RECORD_PATH_BUFFER_MAX, mzf_path)) return false;
    if (!sdcard_file_exists(mfi_path)) return false;
    if (!sdcard_file_open_read(mfi_path)) return false;

    file_size = sdcard_file_size();
    if ((file_size == 0UL) || (file_size >= MFI_TEXT_MAX))
    {
        sdcard_file_close();
        return false;
    }

    bytes_read = sdcard_file_read(text, (uint16_t)file_size);
    sdcard_file_close();
    if (bytes_read != (int16_t)file_size) return false;

    text[file_size] = '\0';
    ok = loader_from_text(text, loader_mode);
    return ok && (*loader_mode != LOADER_MODE_AUTO);
}

bool mzi_sidecar_read_loader_for_mzt_record(const char *mzt_path,
                                            uint16_t record_index,
                                            loader_mode_t *loader_mode)
{
    uint8_t *workspace = wav_sample_stream_get_shared_work_buffer();
    char *mti_path = (char *)workspace;
    char line[24];
    char type[12];
    char speed[5];
    uint8_t line_length = 0U;
    bool line_overflow = false;
    bool target_active = false;
    bool done = false;
    bool ok = true;

    if ((loader_mode == NULL) || (record_index == 0U)) return false;
    if (!make_mti_path(mti_path, RECORD_PATH_BUFFER_MAX, mzt_path)) return false;
    if (!sdcard_file_exists(mti_path)) return false;
    if (!sdcard_file_open_read(mti_path)) return false;

    type[0] = '\0';
    speed[0] = '\0';

    for (;;)
    {
        int16_t received = sdcard_file_read(workspace, WAV_SAMPLE_STREAM_REFILL_BLOCK);
        if (received < 0)
        {
            ok = false;
            break;
        }
        if (received == 0) break;

        for (int16_t index = 0; index < received; ++index)
        {
            char ch = (char)workspace[index];
            if ((ch == '\r') || (ch == '\n'))
            {
                /* Fail closed if a line inside the requested record cannot be
                   represented.  In particular, never let an overlong malformed
                   RECORD= line hide a section boundary and leak fields from the
                   following record into the requested one. */
                if (line_overflow && target_active)
                {
                    ok = false;
                    break;
                }
                if ((line_length != 0U) && !line_overflow)
                {
                    line[line_length] = '\0';
                    ok = mti_accept_line(line, record_index, &target_active,
                                         type, sizeof(type), speed, sizeof(speed),
                                         loader_mode, &done);
                    if (!ok || done) break;
                }
                line_length = 0U;
                line_overflow = false;
                continue;
            }

            if (!line_overflow)
            {
                if (line_length < (uint8_t)(sizeof(line) - 1U))
                    line[line_length++] = ch;
                else
                    line_overflow = true;
            }
        }
        if (!ok || done) break;
    }

    if (ok && !done && line_overflow && target_active)
    {
        ok = false;
    }
    if (ok && !done && (line_length != 0U) && !line_overflow)
    {
        line[line_length] = '\0';
        ok = mti_accept_line(line, record_index, &target_active,
                             type, sizeof(type), speed, sizeof(speed),
                             loader_mode, &done);
    }
    if (ok && !done && target_active)
    {
        ok = loader_from_fields(type, speed, loader_mode);
        done = ok;
    }

    sdcard_file_close();
    return ok && done && (*loader_mode != LOADER_MODE_AUTO);
}

bool mzi_sidecar_relocate_for_mzf(const char *old_mzf_path,
                                  const char *new_mzf_path)
{
    uint8_t *workspace = cmt_mode_scratch.edge_record_stage_bytes;
    char *old_mfi_path = (char *)workspace;
    char *new_mfi_path = (char *)(workspace + RECORD_PATH_BUFFER_MAX);

    if (!make_mfi_path(old_mfi_path, RECORD_PATH_BUFFER_MAX, old_mzf_path) ||
        !make_mfi_path(new_mfi_path, RECORD_PATH_BUFFER_MAX, new_mzf_path))
    {
        return false;
    }
    if (strcmp(old_mfi_path, new_mfi_path) == 0) return true;
    if (!sdcard_file_exists(old_mfi_path)) return false;

    /* The sidecar follows the canonical recording name; an existing
       destination sidecar with that basename is replaced. */
    if (sdcard_file_exists(new_mfi_path) &&
        !sdcard_file_remove(new_mfi_path))
    {
        return false;
    }
    return sdcard_file_rename(old_mfi_path, new_mfi_path);
}
