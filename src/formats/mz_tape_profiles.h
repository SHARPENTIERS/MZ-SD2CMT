#ifndef SD2CMT2_MZ_TAPE_PROFILES_H
#define SD2CMT2_MZ_TAPE_PROFILES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MZ_TAPE_CAP_PLAY = 0x01U,
    MZ_TAPE_CAP_RECORD = 0x02U,
    MZ_TAPE_CAP_AUTONAME = 0x04U,
    MZ_TAPE_CAP_LOADER = 0x08U
} mz_tape_profile_caps_t;

typedef enum
{
    MZ_TAPE_FRAMING_MZ800_NATIVE = 0,
    MZ_TAPE_FRAMING_MZ700_NATIVE,
    MZ_TAPE_FRAMING_IC_TURBO,
    MZ_TAPE_FRAMING_TC_TURBO,
    MZ_TAPE_FRAMING_MZ700_FAST3
} mz_tape_framing_t;

typedef enum
{
    MZ_TAPE_PROFILE_MZ800_NORMAL_1X = 0,
    MZ_TAPE_PROFILE_MZ800_NORMAL_2X,
    MZ_TAPE_PROFILE_MZ800_NORMAL_3X,
    MZ_TAPE_PROFILE_MZ700_NORMAL_1X,
    MZ_TAPE_PROFILE_MZ700_FAST3_3X,
    MZ_TAPE_PROFILE_IC_1_4,
    MZ_TAPE_PROFILE_IC_1_3,
    MZ_TAPE_PROFILE_IC_1_2,
    MZ_TAPE_PROFILE_TC_1_3,
    MZ_TAPE_PROFILE_TC_1_2,
    MZ_TAPE_PROFILE_MZ800_NORMAL_4X,
    /* Append-only so existing profile IDs do not move. */
    MZ_TAPE_PROFILE_IC_1_1,
    MZ_TAPE_PROFILE_TC_1_1,
    MZ_TAPE_PROFILE_COUNT
} mz_tape_profile_id_t;

typedef struct
{
    mz_tape_profile_id_t id;
    mz_tape_framing_t framing;
    uint8_t caps;
    /* Timer3 runs at 16 MHz. These are durations in the logical Sharp/MZ
       8255-side waveform domain: HIGH fields always belong to logical HIGH
       and LOW fields always belong to logical LOW. The READ output layer
       applies the fixed connector inversion without exchanging durations. */
    uint16_t short_high_ticks;
    uint16_t short_low_ticks;
    uint16_t long_high_ticks;
    uint16_t long_low_ticks;
    uint32_t header_leader_short_pulses;
    uint32_t data_leader_short_pulses;
    uint8_t header_mark_long_pulses;
    uint8_t header_mark_short_pulses;
    uint8_t data_mark_long_pulses;
    uint8_t data_mark_short_pulses;
    uint8_t final_mark_long_pulses;
    uint16_t duplicate_gap_short_pulses;
    bool duplicate_header;
    bool duplicate_data;
} mz_tape_profile_t;

/* Descriptors live in flash. This copies one small selected descriptor into
   caller-owned storage; no permanent SRAM table is allocated. */
bool mz_tape_profile_read(mz_tape_profile_id_t id,
                          mz_tape_profile_t *profile);
bool mz_tape_profile_has_capability(mz_tape_profile_id_t id, uint8_t cap);

#endif
