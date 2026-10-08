#include "mzf_loader.h"

#include <Arduino.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "../drivers/flash_text.h"
#include "../drivers/mzio.h"
#include "../drivers/sdcard.h"
#include "../formats/mz_loader_profiles.h"
#include "../streams/wav_sample_stream.h"
#include "mz700_fast3.h"

static mzf_loader_source_seek_t loader_source_seek = sdcard_file_seek;
static mzf_loader_source_read_t loader_source_read = sdcard_file_read;

void mzf_loader_set_source_io(mzf_loader_source_seek_t seek_fn,
                              mzf_loader_source_read_t read_fn)
{
    loader_source_seek = (seek_fn == NULL) ? sdcard_file_seek : seek_fn;
    loader_source_read = (read_fn == NULL) ? sdcard_file_read : read_fn;
}

#define MZF_HEADER_FILE_TYPE_OFFSET 0x00U
#define MZF_HEADER_DATA_LENGTH_OFFSET 0x12U
#define MZF_HEADER_LOAD_ADDRESS_OFFSET 0x14U
#define MZF_HEADER_EXEC_ADDRESS_OFFSET 0x16U
#define MZF_LOADER_MZ800_HEADER_TYPE 0xBBU
#define MZF_LOADER_MZ800_HEADER_OFFSET 0x20U
#define MZF_LOADER_MZ800_HEADER_ADDR 0x1110U
#define MZF_LOADER_MZ800_HEADER_LOAD_ADDR 0x1200U
#define MZF_LOADER_MZ800_HEADER_CAPACITY (128U - MZF_LOADER_MZ800_HEADER_OFFSET)
#define MZF_LOADER_METADATA_OFFSET 0x18U
#define MZF_LOADER_WORKSPACE_RESTORE_BYTES 13U
#define MZF_LOADER_IC_1_1_SPEED_BYTE 0x4DU
#define MZF_LOADER_IC_1_4_SPEED_BYTE 0x11U
#define MZF_LOADER_IC_1_3_SPEED_BYTE 0x16U
#define MZF_LOADER_IC_1_2_SPEED_BYTE 0x20U
#define MZF_LOADER_TC_1_1_SPEED_BYTE 0x52U
#define MZF_LOADER_TC_1_3_SPEED_BYTE 0x1BU
#define MZF_LOADER_TC_1_2_SPEED_BYTE 0x29U
#define MZF_LOADER_TC_LOADER_ADDR 0xD400U
#define MZF_LOADER_TC_LOADER_SIZE 90U
#define MZF_LOADER_TC_SPEED_OFFSET 0x4BU
#define MZF_LOADER_TC_FILE_TYPE_OFFSET 0x4CU
#define MZF_LOADER_TC_WORKSPACE_OFFSET 0x4DU
#define MZF_LOADER_TC_HEADER_TAG_OFFSET 0x18U
#define MZF_LOADER_PREFIX_BYTES 10U
#define MZF_LOADER_MZ800_PREFIX_BYTES 7U
#define MZF_LOADER_MZ800_RELOCATOR_BYTES 14U
#define MZF_LOADER_MZ800_WORKING_RELOCATOR_BYTES 13U
#define MZF_LOADER_MZ800_WORKING_HIGH_STAGE2_BYTES 83U
#define MZF_LOADER_MZ700_UL_PREFIX_BYTES 7U
#define MZF_LOADER_MZ700_UL_LOW_RAM_MAP_BYTES 2U
#define MZF_LOADER_MZ700_UL_DISPLAY_BYTES 9U
#define MZF_LOADER_MZ700_UL_CLS_SOURCE_BYTE 0x16U
#define MZF_LOADER_MZ700_UL_NAME_BYTES 17U
#define MZF_LOADER_MZ700_UL_LOW_RAM_NAME_BYTES 17U
#define MZF_LOADER_MZ700_UL_STATUS_END_BYTES 1U
#define MZF_LOADER_CMT_DEFAULT_BYTES 4U
#define MZF_LOADER_MZ700_LOW_RAM_END 0x1000U
#define MZF_LOADER_HIGH_STACK_SAVE_BYTES 4U
#define MZF_LOADER_HIGH_STACK_SET_BYTES 3U
#define MZF_LOADER_HIGH_STACK_RESTORE_BYTES 4U
#define MZF_LOADER_HIGH_SAVED_SP_BYTES 2U
#define MZF_LOADER_HIGH_STACK_BYTES 16U
#define MZF_LOADER_START_DELAY_MS 100UL
#define MZF_LOADER_HEADER_START_DELAY_MS 700UL
#define MZF_LOADER_HEADER_READY_DELAY_US 1000U
#define MZF_LOADER_HEADER_INITIAL_TIMEOUT_US 5000000UL
#define MZF_LOADER_TIMEOUT_US 500000UL
#define MZF_LOADER_MZ700_START_TIMEOUT_US 5000000UL
#define MZF_LOADER_MZ700_TIMEOUT_US 1000000UL

static bool is_supported_file_type(uint8_t type)
{
    return (type == 0x01U) || (type == 0x76U);
}

static const uint8_t mz800_header_prolog_P[] PROGMEM =
{
    0x3E, 0x08,             /* ld a,8 */
    0xD3, 0xCE,             /* out (0CEh),a */
    0xCD, 0x3E, 0x07,       /* call 073Eh */
    0x36, 0x01,             /* ld (hl),1 */
    0x97,                   /* sub a */
    0x57,                   /* ld d,a */
    0x5F,                   /* ld e,a */
    0xCD, 0x08, 0x03,       /* call 0308h */
    0xCD, 0xBE, 0x02,       /* call 02BEh */
    0xD3, 0xE2,             /* out (0E2h),a */
    0x1A,                   /* ld a,(de) */
    0xD3, 0xE0,             /* out (0E0h),a */
    0x12,                   /* ld (de),a */
    0x13,                   /* inc de */
    0xCB, 0x62,             /* bit 4,d */
    0x28, 0xF5              /* jr z,self-copy */
};

static const uint8_t mz800_high_low_ram_map_P[] PROGMEM =
{
    0x3E, 0x08,             /* ld a,8 */
    0xD3, 0xE0              /* out (0E0h),a */
};

/*
   Classic UL receiver uses the MZ700-compatible memory-mapped PPI at
   $E002/$E003. Force only the MZ700 compatibility mode here. Do not
   reinitialize the 8255 and do not alter the proven receiver/tail.
*/
static const uint8_t generic_ul_mz700_mode_P[] PROGMEM =
{
    0x3E, 0x08,             /* ld a,8 */
    0xD3, 0xCE              /* out ($CE),a: MZ700 compatibility mode */
};

static const uint8_t loader_body_P[] PROGMEM =
{
    0x11, 0x02, 0xE0,
    0xDD, 0x21, 0x03, 0xE0,
    0xAF, 0x3D, 0x20, 0xFD, 0xF3,
    0x1A, 0xE6, 0x20, 0x28, 0xFB,
    0xDD, 0x36, 0x00, 0x03,
    0xC5, 0x01, 0x00, 0x04,
    0x1A, 0xCB, 0x67, 0x28, 0xFB,
    0xE6, 0x20, 0xB1, 0x07, 0x4F,
    0xDD, 0x36, 0x00, 0x02,
    0x1A, 0xCB, 0x67, 0x20, 0xFB,
    0xE6, 0x20, 0xB1, 0x07, 0x4F,
    0xDD, 0x36, 0x00, 0x03,
    0x10, 0xE2,
    0x71, 0xC1, 0x0B, 0x23, 0x79, 0xB0, 0x20, 0xD6,
    0x01, 0x00, 0x01, 0xD9, 0xE1, 0xE9
};

static const uint8_t loader_body_mz800_header_high_exx_P[] PROGMEM =
{
    0x11U, 0x02U, 0xE0U, 0xF3U,
    0x1AU, 0xE6U, 0x20U, 0x28U, 0xFBU,
    0x13U, 0x73U, 0x1BU,
    0x01U, 0x00U, 0x04U,
    0x1AU, 0xCBU, 0x67U, 0x28U, 0xFBU,
    0xE6U, 0x20U, 0xB1U, 0x07U, 0x4FU,
    0x13U, 0x36U, 0x02U, 0x1BU,
    0x1AU, 0xCBU, 0x67U, 0x20U, 0xFBU,
    0xE6U, 0x20U, 0xB1U, 0x07U, 0x4FU,
    0x13U, 0x73U, 0x1BU,
    0x10U, 0xE3U,
    0x71U, 0x23U,
    0xD9U, 0x0BU, 0x79U, 0xB0U, 0xD9U,
    0x20U, 0xD7U,
    0xC3U
};

static_assert((sizeof(mz800_header_prolog_P) +
               MZF_LOADER_MZ800_PREFIX_BYTES +
               sizeof(loader_body_mz800_header_high_exx_P) + 2U +
               MZF_LOADER_CMT_DEFAULT_BYTES) ==
              MZF_LOADER_MZ800_HEADER_CAPACITY,
              "MZ800 LOW header-only UL must fill exactly 96 bytes");

static_assert((MZF_LOADER_MZ800_RELOCATOR_BYTES + 10U + 2U +
               MZF_LOADER_MZ800_PREFIX_BYTES +
               (sizeof(loader_body_mz800_header_high_exx_P) - 1U) +
               MZF_LOADER_CMT_DEFAULT_BYTES + 3U) <=
              MZF_LOADER_MZ800_HEADER_CAPACITY,
              "MZ800 HIGH header-only UL exceeds Description");

static const uint8_t loader_body_ul_working_P[] PROGMEM =
{
    0x11, 0x02, 0xE0, 0xDD, 0x21, 0x03, 0xE0, 0xAF,
    0x3D, 0x20, 0xFD, 0xF3, 0x1A, 0xE6, 0x20, 0x28,
    0xFB, 0xDD, 0x36, 0x00, 0x03, 0xC5, 0x01, 0x00,
    0x04, 0x1A, 0xCB, 0x67, 0x28, 0xFB, 0xE6, 0x20,
    0xB1, 0x07, 0x4F, 0xDD, 0x36, 0x00, 0x02, 0x1A,
    0xCB, 0x67, 0x20, 0xFB, 0xE6, 0x20, 0xB1, 0x07,
    0x4F, 0xDD, 0x36, 0x00, 0x03, 0x10, 0xE2, 0x71,
    0xC1, 0x0B, 0x23, 0x79, 0xB0, 0x20, 0xD6, 0x00,
    0xE1, 0xE9
};

static const uint8_t loader_body_mz800_low_working_P[] PROGMEM =
{
    0x11, 0x02, 0xE0, 0xF3, 0x1A, 0xE6, 0x20, 0x28,
    0xFB, 0x13, 0x7B, 0x12, 0x1B, 0xC5, 0x01, 0x00,
    0x04, 0x1A, 0xCB, 0x67, 0x28, 0xFB, 0xE6, 0x20,
    0xB1, 0x07, 0x4F, 0x7B, 0x13, 0x12, 0x1B, 0x1A,
    0xCB, 0x67, 0x20, 0xFB, 0xE6, 0x20, 0xB1, 0x07,
    0x4F, 0x13, 0x7B, 0x12, 0x1B, 0x10, 0xE2, 0x71,
    0xC1, 0x0B, 0x23, 0x79, 0xB0, 0x20, 0xD6, 0x06,
    0x01, 0xD9, 0xC3
};

static const uint8_t loader_body_mz800_high_working_P[] PROGMEM =
{
    0x11, 0x02, 0xE0, 0xF3, 0x1A, 0xE6, 0x20, 0x28,
    0xFB, 0x13, 0x7B, 0x12, 0x1B, 0x01, 0x00, 0x04,
    0x1A, 0xCB, 0x67, 0x28, 0xFB, 0xE6, 0x20, 0xB1,
    0x07, 0x4F, 0x7B, 0x13, 0x12, 0x1B, 0x1A, 0xCB,
    0x67, 0x20, 0xFB, 0xE6, 0x20, 0xB1, 0x07, 0x4F,
    0x13, 0x7B, 0x12, 0x1B, 0x10, 0xE2, 0x71, 0x23,
    0xD9, 0x0B, 0x79, 0xB0, 0xD9, 0x20, 0xD6, 0x4F,
    0x04, 0xD9, 0xC3
};

static const uint8_t loader_body_mz700_working_P[] PROGMEM =
{
    0x11, 0x02, 0xE0, 0xF3,
    0x7B, 0x13, 0x12, 0x1B,
    0x1A, 0xE6, 0x20, 0x28, 0xFB,
    0x13, 0x7B, 0x12, 0x1B,
    0x01, 0x00, 0x04,
    0x1A, 0xCB, 0x67, 0x28, 0xFB, 0xE6, 0x20, 0xB1,
    0x07, 0x4F, 0x7B, 0x13, 0x12, 0x1B, 0x1A, 0xCB,
    0x67, 0x20, 0xFB, 0xE6, 0x20, 0xB1, 0x07, 0x4F,
    0x13, 0x7B, 0x12, 0x1B, 0x10, 0xE2, 0x71, 0x23,
    0xD9, 0x0B, 0x79, 0xB0, 0xD9, 0x20, 0xD6, 0x4F,
    0x04, 0xD9, 0xC3
};

static_assert(sizeof(loader_body_mz800_low_working_P) == 59U,
              "MZ800 LOW receiver size changed");
static_assert(sizeof(loader_body_mz800_high_working_P) == 59U,
              "MZ800 HIGH receiver size changed");
static_assert(sizeof(loader_body_mz700_working_P) == 63U,
              "MZ700 receiver size changed");
static_assert((MZF_LOADER_MZ700_UL_PREFIX_BYTES +
               MZF_LOADER_MZ700_UL_LOW_RAM_MAP_BYTES +
               MZF_LOADER_MZ700_UL_DISPLAY_BYTES +
               sizeof(loader_body_mz700_working_P) + 2U +
               MZF_LOADER_MZ700_UL_LOW_RAM_NAME_BYTES +
               MZF_LOADER_MZ700_UL_STATUS_END_BYTES) <=
              MZ700_FAST3_RUNTIME_CAPACITY,
              "MZ700 header-only UL runtime exceeds Description");
static_assert((sizeof(mz800_header_prolog_P) + 6U +
               sizeof(loader_body_mz800_low_working_P) + 2U) ==
              MZF_LOADER_MZ800_HEADER_CAPACITY,
              "MZ800 LOW working loader must be exactly 96 bytes");
static_assert((MZF_LOADER_MZ800_WORKING_RELOCATOR_BYTES + 10U + 3U + 2U +
               4U + 3U + sizeof(loader_body_mz800_high_working_P) + 2U) ==
              MZF_LOADER_MZ800_HEADER_CAPACITY,
              "MZ800 HIGH working loader must be exactly 96 bytes");

typedef enum
{
    MZF_LOADER_START_IDLE = 0,
    MZF_LOADER_START_DELAY,
    MZF_LOADER_START_READY_DELAY,
    MZF_LOADER_START_INITIAL_WAIT,
    MZF_LOADER_START_TRANSFER
} mzf_loader_start_state_t;

typedef struct
{
    bool active;
    bool started;
    bool finished;
    bool first_byte_pending;
    bool mz800_header_high;
    mzf_loader_start_state_t start_state;
    mzf_loader_variant_t variant;
    uint16_t loader_address;
    uint16_t loader_size;
    uint16_t data_length;
    uint16_t data_load_address;
    uint16_t exec_address;
    uint8_t file_type;
    uint8_t original_name[MZ700_FAST3_NAME_BYTES];
    uint8_t workspace_restore[MZF_LOADER_WORKSPACE_RESTORE_BYTES];
    uint32_t data_offset;
    uint32_t transferred;
    uint32_t start_time;
    uint8_t first_byte;
    char error_text[17];
} mzf_loader_context_t;

static mzf_loader_context_t context;

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)(value >> 8U);
}

static uint16_t mzf_loader_working_generic_base_size(void)
{
    return (uint16_t)(sizeof(generic_ul_mz700_mode_P) +
                      MZF_LOADER_PREFIX_BYTES +
                      sizeof(loader_body_ul_working_P) + 5U);
}

static uint16_t mzf_loader_build_working_generic(uint8_t *destination)
{
    uint16_t offset = 0U;
    uint16_t high_saved_sp_address = 0U;

    if (context.variant == MZF_LOADER_VARIANT_HIGH)
    {
        high_saved_sp_address =
            (uint16_t)(context.loader_address + context.loader_size);
        destination[offset++] = 0xEDU;
        destination[offset++] = 0x73U;
        write_le16(destination + offset, high_saved_sp_address);
        offset = (uint16_t)(offset + 2U);
        destination[offset++] = 0x31U;
        write_le16(destination + offset,
                   (uint16_t)(high_saved_sp_address +
                              MZF_LOADER_HIGH_SAVED_SP_BYTES +
                              MZF_LOADER_HIGH_STACK_BYTES));
        offset = (uint16_t)(offset + 2U);
    }

    for (uint8_t i = 0U; i < sizeof(generic_ul_mz700_mode_P); ++i)
        destination[offset++] = (uint8_t)pgm_read_byte(generic_ul_mz700_mode_P + i);

    if (context.variant == MZF_LOADER_VARIANT_HIGH)
    {
        for (uint8_t i = 0U; i < sizeof(mz800_high_low_ram_map_P); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(mz800_high_low_ram_map_P + i);
    }

    destination[offset++] = 0x01U;
    write_le16(destination + offset, context.data_length);
    offset = (uint16_t)(offset + 2U);
    destination[offset++] = 0x21U;
    write_le16(destination + offset, context.data_load_address);
    offset = (uint16_t)(offset + 2U);
    destination[offset++] = 0x11U;
    write_le16(destination + offset, context.exec_address);
    offset = (uint16_t)(offset + 2U);
    destination[offset++] = 0xD5U;

    for (uint8_t i = 0U; i < (uint8_t)(sizeof(loader_body_ul_working_P) - 3U); ++i)
        destination[offset++] = (uint8_t)pgm_read_byte(loader_body_ul_working_P + i);

    destination[offset++] = 0xD9U;
    destination[offset++] = 0x01U;
    destination[offset++] = 0x00U;
    destination[offset++] = 0x01U;
    destination[offset++] = 0xD9U;
    destination[offset++] = 0x00U;
    destination[offset++] = 0xE1U;

    if (context.variant == MZF_LOADER_VARIANT_HIGH)
    {
        destination[offset++] = 0xEDU;
        destination[offset++] = 0x7BU;
        write_le16(destination + offset, high_saved_sp_address);
        offset = (uint16_t)(offset + 2U);
    }
    destination[offset++] = 0xE9U;
    return offset;
}

static uint16_t mzf_loader_build_working_mz800(uint8_t *destination)
{
    uint16_t offset = 0U;

    if (!context.mz800_header_high)
    {
        for (uint8_t i = 0U; i < sizeof(mz800_header_prolog_P); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(mz800_header_prolog_P + i);
        destination[offset++] = 0x01U;
        write_le16(destination + offset, context.data_length);
        offset = (uint16_t)(offset + 2U);
        destination[offset++] = 0x21U;
        write_le16(destination + offset, context.data_load_address);
        offset = (uint16_t)(offset + 2U);
        for (uint8_t i = 0U; i < sizeof(loader_body_mz800_low_working_P); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(loader_body_mz800_low_working_P + i);
        write_le16(destination + offset, context.exec_address);
        return (uint16_t)(offset + 2U);
    }

    destination[offset++] = 0x21U;
    write_le16(destination + offset,
               (uint16_t)(MZF_LOADER_MZ800_HEADER_ADDR +
                          MZF_LOADER_MZ800_WORKING_RELOCATOR_BYTES));
    offset = (uint16_t)(offset + 2U);
    destination[offset++] = 0x11U;
    write_le16(destination + offset, MZF_LOADER_HIGH_LOAD_ADDR);
    offset = (uint16_t)(offset + 2U);
    destination[offset++] = 0xD5U;
    destination[offset++] = 0x01U;
    write_le16(destination + offset, MZF_LOADER_MZ800_WORKING_HIGH_STAGE2_BYTES);
    offset = (uint16_t)(offset + 2U);
    destination[offset++] = 0xEDU;
    destination[offset++] = 0xB0U;
    destination[offset++] = 0xC9U;

    destination[offset++] = 0x3EU; destination[offset++] = 0x08U;
    destination[offset++] = 0xD3U; destination[offset++] = 0xCEU;
    destination[offset++] = 0xCDU; destination[offset++] = 0x3EU; destination[offset++] = 0x07U;
    destination[offset++] = 0xCDU; destination[offset++] = 0x08U; destination[offset++] = 0x03U;
    destination[offset++] = 0xCDU; destination[offset++] = 0xBEU; destination[offset++] = 0x02U;
    destination[offset++] = 0xD3U; destination[offset++] = 0xE0U;
    destination[offset++] = 0x01U;
    write_le16(destination + offset, context.data_length);
    offset = (uint16_t)(offset + 2U);
    destination[offset++] = 0xD9U;
    destination[offset++] = 0x21U;
    write_le16(destination + offset, context.data_load_address);
    offset = (uint16_t)(offset + 2U);
    for (uint8_t i = 0U; i < sizeof(loader_body_mz800_high_working_P); ++i)
        destination[offset++] = (uint8_t)pgm_read_byte(loader_body_mz800_high_working_P + i);
    write_le16(destination + offset, context.exec_address);
    return (uint16_t)(offset + 2U);
}

static bool is_ic_mode(loader_mode_t mode)
{
    return (mode == LOADER_MODE_IC_1_1) ||
           (mode == LOADER_MODE_IC_1_4) ||
           (mode == LOADER_MODE_IC_1_3) ||
           (mode == LOADER_MODE_IC_1_2);
}

static bool is_tc_mode(loader_mode_t mode)
{
    return (mode == LOADER_MODE_TC_1_1) ||
           (mode == LOADER_MODE_TC_1_3) ||
           (mode == LOADER_MODE_TC_1_2);
}

static bool is_ic_variant(mzf_loader_variant_t variant)
{
    return (variant == MZF_LOADER_VARIANT_IC_1_1) ||
           (variant == MZF_LOADER_VARIANT_IC_1_4) ||
           (variant == MZF_LOADER_VARIANT_IC_1_3) ||
           (variant == MZF_LOADER_VARIANT_IC_1_2);
}

static bool is_tc_variant(mzf_loader_variant_t variant)
{
    return (variant == MZF_LOADER_VARIANT_TC_1_1) ||
           (variant == MZF_LOADER_VARIANT_TC_1_3) ||
           (variant == MZF_LOADER_VARIANT_TC_1_2);
}

static bool is_mz700_ul_variant(mzf_loader_variant_t variant)
{
    return (variant == MZF_LOADER_VARIANT_MZ700_UL_LOW) ||
           (variant == MZF_LOADER_VARIANT_MZ700_UL_HIGH);
}

static mzf_loader_variant_t mode_to_variant(loader_mode_t mode)
{
    switch (mode)
    {
        case LOADER_MODE_IC_1_1: return MZF_LOADER_VARIANT_IC_1_1;
        case LOADER_MODE_IC_1_4: return MZF_LOADER_VARIANT_IC_1_4;
        case LOADER_MODE_IC_1_3: return MZF_LOADER_VARIANT_IC_1_3;
        case LOADER_MODE_IC_1_2: return MZF_LOADER_VARIANT_IC_1_2;
        case LOADER_MODE_TC_1_1: return MZF_LOADER_VARIANT_TC_1_1;
        case LOADER_MODE_TC_1_3: return MZF_LOADER_VARIANT_TC_1_3;
        case LOADER_MODE_TC_1_2: return MZF_LOADER_VARIANT_TC_1_2;
        default: return MZF_LOADER_VARIANT_NONE;
    }
}

static uint8_t ic_speed_byte(void)
{
    switch (context.variant)
    {
        case MZF_LOADER_VARIANT_IC_1_1: return MZF_LOADER_IC_1_1_SPEED_BYTE;
        case MZF_LOADER_VARIANT_IC_1_2: return MZF_LOADER_IC_1_2_SPEED_BYTE;
        case MZF_LOADER_VARIANT_IC_1_3: return MZF_LOADER_IC_1_3_SPEED_BYTE;
        default: return MZF_LOADER_IC_1_4_SPEED_BYTE;
    }
}

static uint8_t tc_speed_byte(void)
{
    switch (context.variant)
    {
        case MZF_LOADER_VARIANT_TC_1_1: return MZF_LOADER_TC_1_1_SPEED_BYTE;
        case MZF_LOADER_VARIANT_TC_1_2: return MZF_LOADER_TC_1_2_SPEED_BYTE;
        default: return MZF_LOADER_TC_1_3_SPEED_BYTE;
    }
}

static void patch_tc_workspace_restore(uint8_t *destination)
{
    destination[MZF_LOADER_TC_FILE_TYPE_OFFSET] = context.file_type;
    write_le16(destination + MZF_LOADER_TC_WORKSPACE_OFFSET, context.data_length);
    write_le16(destination + MZF_LOADER_TC_WORKSPACE_OFFSET + 2U, context.data_load_address);
    write_le16(destination + MZF_LOADER_TC_WORKSPACE_OFFSET + 4U, context.exec_address);
    memcpy(destination + MZF_LOADER_TC_WORKSPACE_OFFSET + 6U,
           context.workspace_restore + 6U,
           MZF_LOADER_WORKSPACE_RESTORE_BYTES - 6U);
}

static uint16_t mz800_header_low_size(void)
{
    return (uint16_t)(sizeof(mz800_header_prolog_P) +
                      MZF_LOADER_MZ800_PREFIX_BYTES +
                      sizeof(loader_body_mz800_header_high_exx_P) + 2U +
                      MZF_LOADER_CMT_DEFAULT_BYTES);
}

static uint16_t mz800_header_high_stage2_size(void)
{
    return MZF_LOADER_MZ800_WORKING_HIGH_STAGE2_BYTES;
}

static uint16_t mz800_header_high_size(void)
{
    return (uint16_t)(MZF_LOADER_MZ800_WORKING_RELOCATOR_BYTES +
                      mz800_header_high_stage2_size());
}

static uint16_t mz800_header_high_runtime_footprint(void)
{
    return mz800_header_high_stage2_size();
}

static bool mz700_ul_needs_low_ram_map(void)
{
    return context.data_load_address < MZF_LOADER_MZ700_LOW_RAM_END;
}

static uint8_t mz700_ul_name_length(void)
{
    uint8_t length = 0U;
    while ((length < MZ700_FAST3_NAME_BYTES) &&
           (context.original_name[length] != 0x00U) &&
           (context.original_name[length] != 0x0DU)) ++length;
    while ((length != 0U) && (context.original_name[length - 1U] == 0x20U)) --length;
    const uint8_t maximum = mz700_ul_needs_low_ram_map() ?
        MZF_LOADER_MZ700_UL_LOW_RAM_NAME_BYTES : MZF_LOADER_MZ700_UL_NAME_BYTES;
    if (length > maximum) length = maximum;
    return length;
}

static uint8_t mz700_ul_code_size(void)
{
    uint8_t size = (uint8_t)(MZF_LOADER_MZ700_UL_DISPLAY_BYTES +
                             MZF_LOADER_MZ700_UL_PREFIX_BYTES +
                             sizeof(loader_body_mz700_working_P) + 2U);
    if (mz700_ul_needs_low_ram_map())
        size = (uint8_t)(size + MZF_LOADER_MZ700_UL_LOW_RAM_MAP_BYTES);
    return size;
}

static uint8_t mz700_ul_runtime_size(void)
{
    return (uint8_t)(mz700_ul_code_size() + mz700_ul_name_length() +
                     MZF_LOADER_MZ700_UL_STATUS_END_BYTES);
}

static uint16_t loader_size(loader_mode_t mode)
{
    if (!is_ic_mode(mode) && !is_tc_mode(mode) &&
        (mode != LOADER_MODE_UL_MZ800))
        return mzf_loader_working_generic_base_size();
    if (is_ic_mode(mode)) return MZF_LOADER_MZ800_HEADER_CAPACITY;
    if (is_tc_mode(mode)) return MZF_LOADER_TC_LOADER_SIZE;
    if (mode == LOADER_MODE_UL_MZ800) return MZF_LOADER_MZ800_HEADER_CAPACITY;
    return (uint16_t)(MZF_LOADER_PREFIX_BYTES + sizeof(loader_body_P));
}

static uint16_t loader_code_size(mzf_loader_variant_t variant, uint16_t size)
{
    return (variant == MZF_LOADER_VARIANT_HIGH) ?
        (uint16_t)(size + sizeof(mz800_high_low_ram_map_P) +
                   MZF_LOADER_HIGH_STACK_SAVE_BYTES +
                   MZF_LOADER_HIGH_STACK_SET_BYTES +
                   MZF_LOADER_HIGH_STACK_RESTORE_BYTES) : size;
}

static uint16_t loader_footprint(mzf_loader_variant_t variant, uint16_t size)
{
    return (variant == MZF_LOADER_VARIANT_HIGH) ?
        (uint16_t)(loader_code_size(variant, size) +
                   MZF_LOADER_HIGH_SAVED_SP_BYTES + MZF_LOADER_HIGH_STACK_BYTES) : size;
}

static bool ranges_overlap(uint32_t left_start, uint32_t left_length,
                           uint32_t right_start, uint32_t right_length)
{
    uint32_t left_end = left_start + left_length;
    uint32_t right_end = right_start + right_length;
    return (left_start < right_end) && (right_start < left_end);
}

static bool select_loader(uint32_t data_start, uint32_t data_length,
                          uint16_t size, mzf_loader_variant_t *variant,
                          uint16_t *address)
{
    uint16_t footprint = loader_footprint(MZF_LOADER_VARIANT_LOW, size);
    if (!ranges_overlap(MZF_LOADER_LOW_LOAD_ADDR, footprint, data_start, data_length))
    {
        *variant = MZF_LOADER_VARIANT_LOW;
        *address = MZF_LOADER_LOW_LOAD_ADDR;
        return true;
    }
    footprint = loader_footprint(MZF_LOADER_VARIANT_HIGH, size);
    if (!ranges_overlap(MZF_LOADER_HIGH_LOAD_ADDR, footprint, data_start, data_length))
    {
        *variant = MZF_LOADER_VARIANT_HIGH;
        *address = MZF_LOADER_HIGH_LOAD_ADDR;
        return true;
    }
    return false;
}

static void set_error_P(PGM_P text)
{
    flash_text_copy(context.error_text, sizeof(context.error_text), text);
}

static inline uint8_t write_level(void) { return (PINJ & _BV(PJ0)) ? 1U : 0U; }
static inline void read_set(uint8_t level)
{
    if (level != 0U) PORTE |= _BV(PE4); else PORTE &= (uint8_t)~_BV(PE4);
}
static inline void sense_set(uint8_t level)
{
    if (level != 0U) PORTD |= _BV(PD3); else PORTD &= (uint8_t)~_BV(PD3);
}

static bool wait_write_level_legacy(uint8_t expected)
{
    uint32_t start = micros();
    while (write_level() != expected)
    {
        if ((uint32_t)(micros() - start) > MZF_LOADER_TIMEOUT_US)
        {
            set_error_P(PSTR("UL WAIT"));
            return false;
        }
    }
    return true;
}

static char ul_diag_hex(uint8_t value)
{
    value &= 0x0FU;
    return (value < 10U) ? (char)('0' + value) : (char)('A' + (value - 10U));
}

static void ul700_diag_prefix(char *text)
{
    text[0] = 'U'; text[1] = 'L'; text[2] = '7';
    text[3] = (context.variant == MZF_LOADER_VARIANT_MZ700_UL_HIGH) ? 'H' : 'L';
}

static void ul700_diag_append_count(char *text, uint8_t offset)
{
    const uint16_t count = (uint16_t)context.transferred;
    text[offset + 0U] = ul_diag_hex((uint8_t)(count >> 12U));
    text[offset + 1U] = ul_diag_hex((uint8_t)(count >> 8U));
    text[offset + 2U] = ul_diag_hex((uint8_t)(count >> 4U));
    text[offset + 3U] = ul_diag_hex((uint8_t)count);
    text[offset + 4U] = '\0';
}

static void set_ul700_data_error(char edge, uint8_t pair)
{
    ul700_diag_prefix(context.error_text);
    context.error_text[4] = ' '; context.error_text[5] = edge;
    context.error_text[6] = (char)('0' + (pair & 0x03U)); context.error_text[7] = ' ';
    ul700_diag_append_count(context.error_text, 8U);
}

static void set_ul700_end_error(void)
{
    ul700_diag_prefix(context.error_text);
    context.error_text[4] = ' '; context.error_text[5] = 'E';
    context.error_text[6] = 'N'; context.error_text[7] = 'D'; context.error_text[8] = ' ';
    ul700_diag_append_count(context.error_text, 9U);
}

static bool wait_write_level_mz700_data(uint8_t expected, char edge, uint8_t pair)
{
    uint32_t start = micros();
    while (write_level() != expected)
    {
        if ((uint32_t)(micros() - start) > MZF_LOADER_MZ700_TIMEOUT_US)
        {
            set_ul700_data_error(edge, pair);
            return false;
        }
    }
    return true;
}

static bool wait_write_level_mz700_end(uint8_t expected)
{
    uint32_t start = micros();
    while (write_level() != expected)
    {
        if ((uint32_t)(micros() - start) > MZF_LOADER_MZ700_TIMEOUT_US)
        {
            set_ul700_end_error();
            return false;
        }
    }
    return true;
}

static uint8_t rotate_data(uint8_t value) { return (uint8_t)((value << 2U) | (value >> 6U)); }

static bool send_byte_legacy(uint8_t value)
{
    uint8_t data = rotate_data((uint8_t)~value);
    for (uint8_t pair = 0U; pair < 4U; ++pair)
    {
        if (!wait_write_level_legacy(0U)) return false;
        read_set((data & 0x80U) ? 1U : 0U); sense_set(0U);
        if (!wait_write_level_legacy(1U)) return false;
        read_set((data & 0x40U) ? 1U : 0U); sense_set(1U);
        data <<= 2U;
    }
    return true;
}

static bool send_byte_mz700(uint8_t value)
{
    uint8_t data = rotate_data((uint8_t)~value);
    for (uint8_t pair = 0U; pair < 4U; ++pair)
    {
        if (!wait_write_level_mz700_data(0U, 'L', pair)) return false;
        read_set((data & 0x80U) ? 1U : 0U); sense_set(0U);
        if (!wait_write_level_mz700_data(1U, 'H', pair)) return false;
        read_set((data & 0x40U) ? 1U : 0U); sense_set(1U);
        data <<= 2U;
    }
    return true;
}

static bool start_and_send_first_byte_mz700(uint8_t value)
{
    const uint8_t inverted = (uint8_t)~value;
    uint8_t data = (uint8_t)((inverted << 2U) | (inverted >> 6U));
    const uint32_t start = micros();
    sense_set(1U); read_set(1U);
    while (write_level() != 1U)
    {
        if ((uint32_t)(micros() - start) > MZF_LOADER_MZ700_START_TIMEOUT_US)
        { set_error_P(PSTR("UL7 ARM")); return false; }
    }
    read_set(0U);
    while (write_level() != 0U)
    {
        if ((uint32_t)(micros() - start) > MZF_LOADER_MZ700_START_TIMEOUT_US)
        { set_error_P(PSTR("UL7 ACK")); return false; }
    }
    read_set((data & 0x80U) ? 1U : 0U); sense_set(0U);
    if (!wait_write_level_mz700_data(1U, 'H', 0U)) return false;
    read_set((data & 0x40U) ? 1U : 0U); sense_set(1U);
    if (!wait_write_level_mz700_data(0U, 'L', 0U)) return false;
    data <<= 2U;
    for (uint8_t pair = 1U; pair < 4U; ++pair)
    {
        if (!wait_write_level_mz700_data(0U, 'L', pair)) return false;
        read_set((data & 0x80U) ? 1U : 0U); sense_set(0U);
        if (!wait_write_level_mz700_data(1U, 'H', pair)) return false;
        read_set((data & 0x40U) ? 1U : 0U); sense_set(1U);
        data <<= 2U;
    }
    return true;
}

void mzf_loader_reset(void)
{
    memset(&context, 0, sizeof(context));
    context.variant = MZF_LOADER_VARIANT_NONE;
    context.error_text[0] = '\0';
}

bool mzf_loader_prepare(file_format_t format, loader_mode_t mode,
                        const uint8_t *header, uint32_t file_size,
                        uint32_t data_offset)
{
    uint16_t size;
    uint32_t data_end;
    mzf_loader_reset();

    if ((mode == LOADER_MODE_NORMAL_1_1) ||
        (mode == LOADER_MODE_NORMAL_1_2) ||
        (mode == LOADER_MODE_NORMAL_1_3) ||
        (mode == LOADER_MODE_NORMAL_1_4) ||
        (mode == LOADER_MODE_MZ700_1X) ||
        (format != FILE_FORMAT_MZF) || (header == NULL)) return false;

    if (!is_supported_file_type(header[MZF_HEADER_FILE_TYPE_OFFSET])) return false;

    context.file_type = header[MZF_HEADER_FILE_TYPE_OFFSET];
    memcpy(context.original_name, header + 1U, MZ700_FAST3_NAME_BYTES);
    context.data_length = read_le16(header + MZF_HEADER_DATA_LENGTH_OFFSET);
    context.data_load_address = read_le16(header + MZF_HEADER_LOAD_ADDRESS_OFFSET);
    context.exec_address = read_le16(header + MZF_HEADER_EXEC_ADDRESS_OFFSET);
    memcpy(context.workspace_restore, header + MZF_HEADER_DATA_LENGTH_OFFSET,
           MZF_LOADER_WORKSPACE_RESTORE_BYTES);
    context.data_offset = data_offset;
    if (context.data_length == 0U) return false;
    data_end = (uint32_t)context.data_load_address + context.data_length;
    if ((data_end > 0x10000UL) ||
        ((data_offset + (uint32_t)context.data_length) > file_size)) return false;

    size = loader_size(mode);
    if (mode == LOADER_MODE_AUTO) return false;

    if (mode == LOADER_MODE_UL_MZ700)
    {
        const uint8_t runtime_size = mz700_ul_runtime_size();
        if (!ranges_overlap(MZ700_FAST3_RUNTIME_LOW_ADDR, runtime_size,
                            context.data_load_address, context.data_length) &&
            mz700_fast3_stage_is_encodable(MZ700_FAST3_RUNTIME_LOW_ADDR, runtime_size))
        {
            context.variant = MZF_LOADER_VARIANT_MZ700_UL_LOW;
            context.loader_address = MZ700_FAST3_RUNTIME_LOW_ADDR;
        }
        else if (!ranges_overlap(MZ700_FAST3_RUNTIME_HIGH_ADDR, runtime_size,
                                 context.data_load_address, context.data_length) &&
                 mz700_fast3_stage_is_encodable(MZ700_FAST3_RUNTIME_HIGH_ADDR, runtime_size))
        {
            context.variant = MZF_LOADER_VARIANT_MZ700_UL_HIGH;
            context.loader_address = MZ700_FAST3_RUNTIME_HIGH_ADDR;
        }
        else return false;
        context.loader_size = runtime_size; context.active = true; return true;
    }

    if (mode == LOADER_MODE_MZ700_3X)
    {
        const uint8_t runtime_size = mz700_fast3_runtime_size(context.original_name);
        if ((runtime_size == 0U) || (runtime_size > MZ700_FAST3_RUNTIME_CAPACITY)) return false;
        if (!ranges_overlap(MZ700_FAST3_RUNTIME_LOW_ADDR, runtime_size,
                            context.data_load_address, context.data_length) &&
            mz700_fast3_stage_is_encodable(MZ700_FAST3_RUNTIME_LOW_ADDR, runtime_size))
        {
            context.variant = MZF_LOADER_VARIANT_MZ700_FAST3_LOW;
            context.loader_address = MZ700_FAST3_RUNTIME_LOW_ADDR;
        }
        else if (!ranges_overlap(MZ700_FAST3_RUNTIME_HIGH_ADDR, runtime_size,
                                 context.data_load_address, context.data_length) &&
                 mz700_fast3_stage_is_encodable(MZ700_FAST3_RUNTIME_HIGH_ADDR, runtime_size))
        {
            context.variant = MZF_LOADER_VARIANT_MZ700_FAST3_HIGH;
            context.loader_address = MZ700_FAST3_RUNTIME_HIGH_ADDR;
        }
        else return false;
        context.loader_size = runtime_size; context.active = true; return true;
    }
    else if (mode == LOADER_MODE_UL_MZ800)
    {
        const uint16_t low_size = mz800_header_low_size();
        if (!ranges_overlap(MZF_LOADER_MZ800_HEADER_ADDR, low_size,
                            context.data_load_address, context.data_length))
        {
            context.variant = MZF_LOADER_VARIANT_MZ800_HEADER;
            context.mz800_header_high = false;
            context.loader_address = MZF_LOADER_MZ800_HEADER_ADDR;
            context.loader_size = low_size;
        }
        else if (!ranges_overlap(MZF_LOADER_HIGH_LOAD_ADDR,
                                 mz800_header_high_runtime_footprint(),
                                 context.data_load_address, context.data_length))
        {
            context.variant = MZF_LOADER_VARIANT_MZ800_HEADER;
            context.mz800_header_high = true;
            context.loader_address = MZF_LOADER_MZ800_HEADER_ADDR;
            context.loader_size = mz800_header_high_size();
        }
        else return false;
        context.active = true; return true;
    }
    else if (is_ic_mode(mode))
    {
        if ((context.file_type != 0x01U) ||
            (size > MZF_LOADER_MZ800_HEADER_CAPACITY) ||
            ranges_overlap(MZF_LOADER_MZ800_HEADER_ADDR, size,
                           context.data_load_address, context.data_length)) return false;
        context.variant = mode_to_variant(mode);
        context.loader_address = MZF_LOADER_MZ800_HEADER_ADDR;
    }
    else if (is_tc_mode(mode))
    {
        if (ranges_overlap(MZF_LOADER_TC_LOADER_ADDR, size,
                           context.data_load_address, context.data_length)) return false;
        context.variant = mode_to_variant(mode);
        context.loader_address = MZF_LOADER_TC_LOADER_ADDR;
    }
    else if (!select_loader(context.data_load_address, context.data_length,
                            size, &context.variant, &context.loader_address)) return false;

    context.loader_size = loader_code_size(context.variant, size);
    context.active = true;
    return true;
}

bool mzf_loader_is_active(void) { return context.active; }
bool mzf_loader_is_ul_active(void)
{
    return context.active && !is_ic_variant(context.variant) &&
           !is_tc_variant(context.variant) && !mzf_loader_is_mz700_fast3();
}
bool mzf_loader_is_header_only(void)
{
    return context.active && ((context.variant == MZF_LOADER_VARIANT_MZ800_HEADER) ||
                              is_mz700_ul_variant(context.variant));
}
bool mzf_loader_is_mz800_header_high(void)
{
    return context.active && (context.variant == MZF_LOADER_VARIANT_MZ800_HEADER) &&
           context.mz800_header_high;
}
bool mzf_loader_is_mz700_ul(void) { return context.active && is_mz700_ul_variant(context.variant); }
bool mzf_loader_is_mz700_ul_high(void)
{
    return context.active && (context.variant == MZF_LOADER_VARIANT_MZ700_UL_HIGH);
}
bool mzf_loader_is_mz700_fast3(void)
{
    return context.active && ((context.variant == MZF_LOADER_VARIANT_MZ700_FAST3_LOW) ||
                              (context.variant == MZF_LOADER_VARIANT_MZ700_FAST3_HIGH));
}
bool mzf_loader_is_mz700_fast3_high(void)
{
    return context.active && (context.variant == MZF_LOADER_VARIANT_MZ700_FAST3_HIGH);
}
bool mzf_loader_is_ic_turbo(void) { return context.active && is_ic_variant(context.variant); }
bool mzf_loader_is_tc_turbo(void) { return context.active && is_tc_variant(context.variant); }
bool mzf_loader_is_tape_turbo(void)
{
    return context.active && (is_ic_variant(context.variant) || is_tc_variant(context.variant) ||
                              mzf_loader_is_mz700_fast3());
}
mzf_loader_variant_t mzf_loader_get_variant(void) { return context.variant; }
uint16_t mzf_loader_get_loader_size(void) { return context.active ? context.loader_size : 0U; }

bool mzf_loader_patch_loader_header(uint8_t *header)
{
    if ((header == NULL) || !context.active) return false;
    if (mzf_loader_is_mz700_fast3())
        return mz700_fast3_build_header(header, context.loader_address,
                                        (uint8_t)context.loader_size,
                                        context.data_length,
                                        context.data_load_address,
                                        context.exec_address,
                                        context.original_name);
    if (mzf_loader_is_mz700_ul())
    {
        uint8_t runtime[MZ700_FAST3_RUNTIME_CAPACITY];
        uint16_t built = mzf_loader_build_loader(runtime, MZ700_FAST3_RUNTIME_CAPACITY);
        return (built == context.loader_size) &&
               mz700_header_only_build(header, context.loader_address,
                                       runtime, (uint8_t)built);
    }
    if ((context.variant == MZF_LOADER_VARIANT_MZ800_HEADER) || is_ic_variant(context.variant))
    {
        header[MZF_HEADER_FILE_TYPE_OFFSET] = MZF_LOADER_MZ800_HEADER_TYPE;
        write_le16(header + MZF_HEADER_DATA_LENGTH_OFFSET, 0U);
        write_le16(header + MZF_HEADER_LOAD_ADDRESS_OFFSET, MZF_LOADER_MZ800_HEADER_LOAD_ADDR);
        write_le16(header + MZF_HEADER_EXEC_ADDRESS_OFFSET, context.loader_address);
        if (is_ic_variant(context.variant))
        {
            header[MZF_LOADER_METADATA_OFFSET] = 0x01U;
            header[MZF_LOADER_METADATA_OFFSET + 1U] = ic_speed_byte();
        }
        else
        {
            header[MZF_LOADER_METADATA_OFFSET] = context.file_type;
            header[MZF_LOADER_METADATA_OFFSET + 1U] = (uint8_t)(context.data_length >> 8U);
        }
        memcpy(header + MZF_LOADER_METADATA_OFFSET + 2U, context.workspace_restore, 6U);
        (void)mzf_loader_build_loader(header + MZF_LOADER_MZ800_HEADER_OFFSET,
                                     MZF_LOADER_MZ800_HEADER_CAPACITY);
        return true;
    }
    if (is_tc_variant(context.variant))
    {
        write_le16(header + MZF_HEADER_DATA_LENGTH_OFFSET, context.loader_size);
        write_le16(header + MZF_HEADER_LOAD_ADDRESS_OFFSET, context.loader_address);
        write_le16(header + MZF_HEADER_EXEC_ADDRESS_OFFSET, context.loader_address);
        mz_loader_profile_copy_tc_tag(header + MZF_LOADER_TC_HEADER_TAG_OFFSET);
        return true;
    }
    write_le16(header + MZF_HEADER_DATA_LENGTH_OFFSET, context.loader_size);
    write_le16(header + MZF_HEADER_LOAD_ADDRESS_OFFSET, context.loader_address);
    write_le16(header + MZF_HEADER_EXEC_ADDRESS_OFFSET, context.loader_address);
    return true;
}

uint16_t mzf_loader_build_loader(uint8_t *destination, uint16_t capacity)
{
    uint16_t offset = 0U;
    uint16_t high_saved_sp_address = 0U;
    if ((destination == NULL) || !context.active || (capacity < context.loader_size)) return 0U;
    if ((context.variant == MZF_LOADER_VARIANT_LOW) ||
        (context.variant == MZF_LOADER_VARIANT_HIGH)) return mzf_loader_build_working_generic(destination);
    if (context.variant == MZF_LOADER_VARIANT_MZ800_HEADER)
        return mzf_loader_build_working_mz800(destination);
    if (is_ic_variant(context.variant))
    {
        mz_loader_profile_copy_ic_loader(destination);
        return MZF_LOADER_MZ800_HEADER_CAPACITY;
    }
    if (is_tc_variant(context.variant))
    {
        mz_loader_profile_copy_tc_loader(destination);
        destination[MZF_LOADER_TC_SPEED_OFFSET] = tc_speed_byte();
        patch_tc_workspace_restore(destination);
        return MZF_LOADER_TC_LOADER_SIZE;
    }
    if (is_mz700_ul_variant(context.variant))
    {
        const uint8_t name_length = mz700_ul_name_length();
        const uint16_t status_address = (uint16_t)(context.loader_address + mz700_ul_code_size());
        destination[offset++] = 0x3EU; destination[offset++] = MZF_LOADER_MZ700_UL_CLS_SOURCE_BYTE;
        destination[offset++] = 0xCDU; write_le16(destination + offset, 0x0012U); offset += 2U;
        destination[offset++] = 0x11U; write_le16(destination + offset, status_address); offset += 2U;
        destination[offset++] = 0xDFU;
        if (mz700_ul_needs_low_ram_map()) { destination[offset++] = 0xD3U; destination[offset++] = 0xE0U; }
        destination[offset++] = 0x01U; write_le16(destination + offset, context.data_length); offset += 2U;
        destination[offset++] = 0xD9U;
        destination[offset++] = 0x21U; write_le16(destination + offset, context.data_load_address); offset += 2U;
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(loader_body_mz700_working_P) - 1U); ++i)
            destination[offset++] = pgm_read_byte(loader_body_mz700_working_P + i);
        destination[offset++] = 0xC3U; write_le16(destination + offset, context.exec_address); offset += 2U;
        memcpy(destination + offset, context.original_name, name_length); offset += name_length;
        destination[offset++] = 0x0DU;
        return (offset == context.loader_size) ? offset : 0U;
    }

    if (context.variant == MZF_LOADER_VARIANT_HIGH)
    {
        high_saved_sp_address = (uint16_t)(context.loader_address + context.loader_size);
        destination[offset++] = 0xEDU; destination[offset++] = 0x73U;
        write_le16(destination + offset, high_saved_sp_address); offset += 2U;
        destination[offset++] = 0x31U;
        write_le16(destination + offset,
                   (uint16_t)(high_saved_sp_address + MZF_LOADER_HIGH_SAVED_SP_BYTES +
                              MZF_LOADER_HIGH_STACK_BYTES)); offset += 2U;
    }

    if (context.variant == MZF_LOADER_VARIANT_HIGH)
    {
        for (uint8_t i = 0U; i < sizeof(mz800_high_low_ram_map_P); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(mz800_high_low_ram_map_P + i);
    }
    else if ((context.variant == MZF_LOADER_VARIANT_MZ800_HEADER) && !context.mz800_header_high)
    {
        for (uint8_t i = 0U; i < sizeof(mz800_header_prolog_P); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(mz800_header_prolog_P + i);
        destination[offset++] = 0x01U; write_le16(destination + offset, context.data_length); offset += 2U;
        destination[offset++] = 0xD9U;
        destination[offset++] = 0x21U; write_le16(destination + offset, context.data_load_address); offset += 2U;
        for (uint16_t i = 0U; i < (uint16_t)(sizeof(loader_body_mz800_header_high_exx_P) - 1U); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(loader_body_mz800_header_high_exx_P + i);
        destination[offset++] = 0x01U; destination[offset++] = 0x00U; destination[offset++] = 0x01U;
        destination[offset++] = 0xD9U; destination[offset++] = 0xC3U;
        write_le16(destination + offset, context.exec_address); offset += 2U;
        return offset;
    }
    else if ((context.variant == MZF_LOADER_VARIANT_MZ800_HEADER) && context.mz800_header_high)
    {
        const uint16_t stage2_size = mz800_header_high_stage2_size();
        const uint16_t stage2_source = (uint16_t)(MZF_LOADER_MZ800_HEADER_ADDR + MZF_LOADER_MZ800_RELOCATOR_BYTES);
        const uint16_t stage2_address = MZF_LOADER_HIGH_LOAD_ADDR;
        destination[offset++] = 0x21U; write_le16(destination + offset, stage2_source); offset += 2U;
        destination[offset++] = 0x11U; write_le16(destination + offset, stage2_address); offset += 2U;
        destination[offset++] = 0x01U; write_le16(destination + offset, stage2_size); offset += 2U;
        destination[offset++] = 0xEDU; destination[offset++] = 0xB0U;
        destination[offset++] = 0xC3U; write_le16(destination + offset, stage2_address); offset += 2U;
        destination[offset++] = 0x3EU; destination[offset++] = 0x08U;
        destination[offset++] = 0xD3U; destination[offset++] = 0xCEU;
        destination[offset++] = 0xCDU; destination[offset++] = 0x3EU; destination[offset++] = 0x07U;
        destination[offset++] = 0xCDU; destination[offset++] = 0x08U; destination[offset++] = 0x03U;
        destination[offset++] = 0xD3U; destination[offset++] = 0xE0U;
        destination[offset++] = 0x01U; write_le16(destination + offset, context.data_length); offset += 2U;
        destination[offset++] = 0xD9U;
        destination[offset++] = 0x21U; write_le16(destination + offset, context.data_load_address); offset += 2U;
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(loader_body_mz800_header_high_exx_P) - 1U); ++i)
            destination[offset++] = pgm_read_byte(loader_body_mz800_header_high_exx_P + i);
        destination[offset++] = 0x01U; destination[offset++] = 0x00U; destination[offset++] = 0x01U;
        destination[offset++] = 0xD9U; destination[offset++] = 0xC3U;
        write_le16(destination + offset, context.exec_address); offset += 2U;
        return offset;
    }

    destination[offset++] = 0x01U; write_le16(destination + offset, context.data_length); offset += 2U;
    destination[offset++] = 0x21U; write_le16(destination + offset, context.data_load_address); offset += 2U;
    destination[offset++] = 0x11U; write_le16(destination + offset, context.exec_address); offset += 2U;
    destination[offset++] = 0xD5U;
    if (context.variant == MZF_LOADER_VARIANT_HIGH)
    {
        for (uint16_t i = 0U; i < (uint16_t)(sizeof(loader_body_P) - 1U); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(loader_body_P + i);
        destination[offset++] = 0xEDU; destination[offset++] = 0x7BU;
        write_le16(destination + offset, high_saved_sp_address); offset += 2U;
        destination[offset++] = 0xE9U;
    }
    else
    {
        for (uint16_t i = 0U; i < sizeof(loader_body_P); ++i)
            destination[offset++] = (uint8_t)pgm_read_byte(loader_body_P + i);
    }
    return offset;
}

bool mzf_loader_begin(void)
{
    if (!context.active) { set_error_P(PSTR("UL ARG")); return false; }
    if (context.started) return true;
    if (is_mz700_ul_variant(context.variant))
    {
        mz_sense_set_fast(true); mz_read_set_fast(true);
        if (!loader_source_seek(context.data_offset)) { set_error_P(PSTR("UL SEEK")); return false; }
        context.transferred = 0UL; context.finished = false; context.first_byte_pending = false;
        context.start_time = 0UL; context.start_state = MZF_LOADER_START_TRANSFER;
        context.started = true; return true;
    }
    if (!loader_source_seek(context.data_offset)) { set_error_P(PSTR("UL SEEK")); return false; }
    context.transferred = 0UL; context.finished = false; context.first_byte_pending = false;
    context.start_time = 0UL; context.started = true;
    mz_sense_set_fast(true);
    if (context.variant == MZF_LOADER_VARIANT_MZ800_HEADER) mz_read_set_fast(false);
    else mz_read_set_fast(true);
    context.start_time = millis(); context.start_state = MZF_LOADER_START_DELAY;
    return true;
}

static bool mzf_loader_service_startup(void)
{
    int16_t received;
    switch (context.start_state)
    {
        case MZF_LOADER_START_DELAY:
        {
            const uint32_t delay_ms = (context.variant == MZF_LOADER_VARIANT_MZ800_HEADER) ?
                MZF_LOADER_HEADER_START_DELAY_MS : MZF_LOADER_START_DELAY_MS;
            if ((uint32_t)(millis() - context.start_time) < delay_ms) return true;
            if (context.variant == MZF_LOADER_VARIANT_MZ800_HEADER)
            {
                mz_read_set_fast(true); context.start_time = micros();
                context.start_state = MZF_LOADER_START_READY_DELAY;
            }
            else context.start_state = MZF_LOADER_START_TRANSFER;
            return true;
        }
        case MZF_LOADER_START_READY_DELAY:
            if ((uint32_t)(micros() - context.start_time) < MZF_LOADER_HEADER_READY_DELAY_US) return true;
            received = loader_source_read(&context.first_byte, 1U);
            if (received != 1)
            {
                set_error_P((received < 0) ? PSTR("UL READ") : PSTR("UL SHORT"));
                return false;
            }
            context.first_byte_pending = true; context.start_time = micros();
            context.start_state = MZF_LOADER_START_INITIAL_WAIT; return true;
        case MZF_LOADER_START_INITIAL_WAIT:
            if (write_level() == 0U) { context.start_state = MZF_LOADER_START_TRANSFER; return true; }
            if ((uint32_t)(micros() - context.start_time) > MZF_LOADER_HEADER_INITIAL_TIMEOUT_US)
            { set_error_P(PSTR("UL WAIT")); return false; }
            return true;
        default: return true;
    }
}

bool mzf_loader_pump(uint8_t max_bytes)
{
    uint8_t *work;
    uint16_t request;
    int16_t received;
    bool mz700_ul;
    if (!context.active || !context.started) { set_error_P(PSTR("UL ARG")); return false; }
    if (context.finished) return true;
    if (!mzf_loader_service_startup()) return false;
    if (context.start_state != MZF_LOADER_START_TRANSFER) return true;
    if (max_bytes == 0U) max_bytes = 1U;
    if (context.first_byte_pending)
    {
        if (!send_byte_legacy(context.first_byte)) return false;
        context.first_byte_pending = false; context.transferred++; max_bytes--;
    }
    if ((context.transferred < context.data_length) && (max_bytes != 0U))
    {
        request = (uint16_t)((uint32_t)context.data_length - context.transferred);
        if (request > max_bytes) request = max_bytes;
        if (request > WAV_SAMPLE_STREAM_REFILL_BLOCK) request = WAV_SAMPLE_STREAM_REFILL_BLOCK;
        work = wav_sample_stream_get_shared_work_buffer();
        received = loader_source_read(work, request);
        if (received != (int16_t)request)
        {
            set_error_P((received < 0) ? PSTR("UL READ") : PSTR("UL SHORT"));
            return false;
        }
        mz700_ul = is_mz700_ul_variant(context.variant);
        if (mz700_ul)
        {
            for (uint16_t i = 0U; i < request; ++i)
            {
                if (context.transferred == 0UL)
                { if (!start_and_send_first_byte_mz700(work[i])) return false; }
                else if (!send_byte_mz700(work[i])) return false;
                context.transferred++;
            }
        }
        else
        {
            for (uint16_t i = 0U; i < request; ++i)
            { if (!send_byte_legacy(work[i])) return false; context.transferred++; }
        }
    }
    if (context.transferred >= context.data_length)
    {
        mz700_ul = is_mz700_ul_variant(context.variant);
        if (mz700_ul) { if (!wait_write_level_mz700_end(0U)) return false; }
        else if (!wait_write_level_legacy(0U)) return false;
        context.finished = true; mz_read_set_fast(false); mz_sense_set_fast(true);
    }
    return true;
}

bool mzf_loader_is_finished(void) { return context.finished; }
uint8_t mzf_loader_get_progress_percent(void)
{
    uint32_t percent;
    if (!context.active || (context.data_length == 0U)) return 0U;
    percent = (context.transferred * 100UL) / context.data_length;
    return (percent > 100UL) ? 100U : (uint8_t)percent;
}
const char *mzf_loader_get_error_text(void) { return context.error_text; }
