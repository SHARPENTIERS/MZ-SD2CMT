#include "mz_loader_profiles.h"

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <string.h>

static const uint8_t ic_loader_P[MZ_IC_LOADER_BYTES] PROGMEM =
{
    0x3E, 0x08, 0xD3, 0xCE, 0xCD, 0x3E, 0x07, 0x36,
    0x01, 0x97, 0x57, 0x5F, 0xCD, 0x08, 0x03, 0xCD,
    0xBE, 0x02, 0xD3, 0xE2, 0x1A, 0xD3, 0xE0, 0x12,
    0x13, 0xCB, 0x62, 0x28, 0xF5, 0x3E, 0xC3, 0x32,
    0x1F, 0x06, 0x21, 0x5C, 0x11, 0x22, 0x20, 0x06,
    0x2A, 0x08, 0x11, 0x7D, 0x32, 0x12, 0x05, 0x7C,
    0x32, 0x4B, 0x0A, 0x2A, 0x0A, 0x11, 0x22, 0x02,
    0x11, 0xCD, 0xF8, 0x04, 0x01, 0xCF, 0x06, 0xED,
    0x71, 0xD3, 0xE2, 0xDA, 0xAA, 0xE9, 0x21, 0x0A,
    0x11, 0xC3, 0x08, 0xED, 0xC5, 0x3A, 0x10, 0x11,
    0xEE, 0x0C, 0x32, 0x10, 0x11, 0x01, 0xCF, 0x06,
    0xED, 0x79, 0xC1, 0xC9, 0x31, 0x39, 0x38, 0x37
};

static const uint8_t tc_loader_P[MZ_TC_LOADER_BYTES] PROGMEM =
{
    0x3E, 0x08, 0xD3, 0xCE, 0xE5, 0x21, 0x00, 0x00,
    0xD3, 0xE4, 0x7E, 0xD3, 0xE0, 0x77, 0x23, 0x7C,
    0xFE, 0x10, 0x20, 0xF4, 0x3A, 0x4B, 0xD4, 0x32,
    0x4B, 0x0A, 0x3A, 0x4C, 0xD4, 0x32, 0x12, 0x05,
    0x21, 0x4D, 0xD4, 0x11, 0x02, 0x11, 0x01, 0x0D,
    0x00, 0xED, 0xB0, 0xE1, 0x7C, 0xFE, 0xD4, 0x28,
    0x12, 0x2A, 0x04, 0x11, 0xD9, 0x21, 0x00, 0x12,
    0x22, 0x04, 0x11, 0xCD, 0x2A, 0x00, 0xD3, 0xE4,
    0xC3, 0x9A, 0xE9, 0xCD, 0x2A, 0x00, 0xD3, 0xE4,
    0xC3, 0x24, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

static const uint8_t tc_tag_P[8] PROGMEM =
{
    0x5B, 0x96, 0xA5, 0x9D, 0x9A, 0xB7, 0x5D, 0x00
};

static uint16_t read_le16(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

static bool supported_type(uint8_t type)
{
    return (type == 0x01U) || (type == 0x76U);
}

void mz_loader_profile_copy_ic_loader(uint8_t *destination)
{
    if (destination == NULL) return;
    for (uint8_t i = 0U; i < MZ_IC_LOADER_BYTES; ++i)
        destination[i] = pgm_read_byte(ic_loader_P + i);
}

void mz_loader_profile_copy_tc_loader(uint8_t *destination)
{
    if (destination == NULL) return;
    for (uint8_t i = 0U; i < MZ_TC_LOADER_BYTES; ++i)
        destination[i] = pgm_read_byte(tc_loader_P + i);
}

void mz_loader_profile_copy_tc_tag(uint8_t *destination)
{
    if (destination == NULL) return;
    for (uint8_t i = 0U; i < sizeof(tc_tag_P); ++i)
        destination[i] = pgm_read_byte(tc_tag_P + i);
}

bool mz_loader_profile_detect_ic(const uint8_t *header,
                                 mz_copier_profile_t *profile)
{
    mz_copier_profile_t matched;
    bool fastipl_loader_prefix = true;

    if ((header == NULL) || (header[0x18U] != 0x01U)) return false;

    matched = (header[0x19U] == 0x11U) ? MZ_COPIER_IC_1_4 :
              (header[0x19U] == 0x16U) ? MZ_COPIER_IC_1_3 :
              (header[0x19U] == 0x20U) ? MZ_COPIER_IC_1_2 : MZ_COPIER_NONE;
    if (matched == MZ_COPIER_NONE) return false;

    /* READPOINT and BLCOUNT identify the speed, but older compatible
       FASTIPL revisions do not all preserve the synthetic length/address
       fields byte-for-byte.  The stable beginning of the embedded loader
       distinguishes IC from the similar $BB UL envelope without tying
       detection to one complete loader revision. */
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        if (header[0x20U + i] != pgm_read_byte(ic_loader_P + i))
        {
            fastipl_loader_prefix = false;
            break;
        }
    }
    if (!fastipl_loader_prefix) return false;

    if (profile != NULL) *profile = matched;
    return true;
}

bool mz_loader_profile_recognize_ic(const uint8_t *header,
                                    uint8_t *canonical_header,
                                    mz_copier_profile_t *profile)
{
    mz_copier_profile_t matched;
    if ((canonical_header == NULL) ||
        !mz_loader_profile_detect_ic(header, &matched)) return false;

    /* FASTIPL byte $18 is BLCOUNT, not the original MZF type.  A single
       block FASTIPL recording is therefore normalized as machine code. */
    memcpy(canonical_header, header, 128U);
    canonical_header[0] = 0x01U;
    memcpy(canonical_header + 0x12U, header + 0x1AU, 6U);
    memset(canonical_header + 0x18U, 0, 128U - 0x18U);
    if (profile != NULL) *profile = matched;
    return true;
}

bool mz_loader_profile_recognize_tc_header(const uint8_t *header)
{
    if ((header == NULL) || !supported_type(header[0]) ||
        (read_le16(header + 0x12U) != MZ_TC_LOADER_BYTES) ||
        (read_le16(header + 0x14U) != 0xD400U) ||
        (read_le16(header + 0x16U) != 0xD400U)) return false;
    /* Older proven TC/Intercopy headers preserve a workspace value in the
       eighth tag byte (for example $0F) instead of the current $00.  The
       first seven signature bytes identify the candidate; the mandatory
       checksummed 90-byte loader/template match is the final proof. */
    for (uint8_t i = 0U; i < (sizeof(tc_tag_P) - 1U); ++i)
        if (header[0x18U + i] != pgm_read_byte(tc_tag_P + i)) return false;
    return true;
}

bool mz_loader_profile_detect_tc(const uint8_t *loader_header,
                                 const uint8_t *loader_data,
                                 mz_copier_profile_t *profile)
{
    mz_copier_profile_t matched;
    if (!mz_loader_profile_recognize_tc_header(loader_header) ||
        (loader_data == NULL)) return false;
    matched = (loader_data[0x4BU] == 0x1BU) ? MZ_COPIER_TC_1_3 :
              (loader_data[0x4BU] == 0x29U) ? MZ_COPIER_TC_1_2 : MZ_COPIER_NONE;
    if ((matched == MZ_COPIER_NONE) || !supported_type(loader_data[0x4CU]))
        return false;
    for (uint8_t i = 0U; i < MZ_TC_LOADER_BYTES; ++i)
    {
        if ((i >= 0x4BU) && (i <= 0x59U)) continue;
        if (loader_data[i] != pgm_read_byte(tc_loader_P + i)) return false;
    }
    if (profile != NULL) *profile = matched;
    return true;
}

bool mz_loader_profile_normalize_tc(const uint8_t *loader_header,
                                    const uint8_t *loader_data,
                                    uint8_t *canonical_header,
                                    mz_copier_profile_t *profile)
{
    mz_copier_profile_t matched;
    if ((canonical_header == NULL) ||
        !mz_loader_profile_detect_tc(loader_header, loader_data, &matched))
        return false;
    memcpy(canonical_header, loader_header, 128U);
    canonical_header[0] = loader_data[0x4CU];
    memcpy(canonical_header + 0x12U, loader_data + 0x4DU, 13U);
    canonical_header[0x1FU] = 0U;
    if (profile != NULL) *profile = matched;
    return true;
}
