#include "mz700_fast3.h"

#include <avr/pgmspace.h>
#include <string.h>

#define MZ700_FAST3_HEADER_TYPE_OFFSET 0x00U
#define MZ700_FAST3_HEADER_NAME_OFFSET 0x01U
#define MZ700_FAST3_HEADER_NAME_END_OFFSET 0x0FU
#define MZ700_FAST3_HEADER_SIZE_OFFSET 0x12U
#define MZ700_FAST3_HEADER_LOAD_OFFSET 0x14U
#define MZ700_FAST3_HEADER_EXEC_OFFSET 0x16U
#define MZ700_FAST3_HEADER_RUNTIME_OFFSET 0x18U
#define MZ700_FAST3_HEADER_TYPE 0x01U
#define MZ700_FAST3_HEADER_EXEC 0xD080U
#define MZ700_FAST3_STAGE_BYTES 14U
#define MZ700_FAST3_RUNTIME_SOURCE_ADDR 0x1108U
#define MZ700_FAST3_DISPLAY_CODE_BYTES 24U
#define MZ700_FAST3_CORE_BYTES 49U

/* Exact 1Z-009A QADCN table at ROM $0A92-$0B91.
   ROM SHA-256: B0D16889AC3E2A80CC3BC9445BC95BC9988DF7B6115124F284850667CF45FF9F */
static const uint8_t mz700_qadcn_P[256] PROGMEM =
{
    0xF0U, 0xF0U, 0xF0U, 0xF3U, 0xF0U, 0xF5U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U,
    0xF0U, 0xC1U, 0xC2U, 0xC3U, 0xC4U, 0xC5U, 0xC6U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U, 0xF0U,
    0x00U, 0x61U, 0x62U, 0x63U, 0x64U, 0x65U, 0x66U, 0x67U, 0x68U, 0x69U, 0x6BU, 0x6AU, 0x2FU, 0x2AU, 0x2EU, 0x2DU,
    0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U, 0x28U, 0x29U, 0x4FU, 0x2CU, 0x51U, 0x2BU, 0x57U, 0x49U,
    0x55U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
    0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U, 0x19U, 0x1AU, 0x52U, 0x59U, 0x54U, 0x50U, 0x45U,
    0xC7U, 0xC8U, 0xC9U, 0xCAU, 0xCBU, 0xCCU, 0xCDU, 0xCEU, 0xCFU, 0xDFU, 0xE7U, 0xE8U, 0xE9U, 0xEAU, 0xECU, 0xEDU,
    0xD0U, 0xD1U, 0xD2U, 0xD3U, 0xD4U, 0xD5U, 0xD6U, 0xD7U, 0xD8U, 0xD9U, 0xDAU, 0xDBU, 0xDCU, 0xDDU, 0xDEU, 0xC0U,
    0x80U, 0xBDU, 0x9DU, 0xB1U, 0xB5U, 0xB9U, 0xB4U, 0x9EU, 0xB2U, 0xB6U, 0xBAU, 0xBEU, 0x9FU, 0xB3U, 0xB7U, 0xBBU,
    0xBFU, 0xA3U, 0x85U, 0xA4U, 0xA5U, 0xA6U, 0x94U, 0x87U, 0x88U, 0x9CU, 0x82U, 0x98U, 0x84U, 0x92U, 0x90U, 0x83U,
    0x91U, 0x81U, 0x9AU, 0x97U, 0x93U, 0x95U, 0x89U, 0xA1U, 0xAFU, 0x8BU, 0x86U, 0x96U, 0xA2U, 0xABU, 0xAAU, 0x8AU,
    0x8EU, 0xB0U, 0xADU, 0x8DU, 0xA7U, 0xA8U, 0xA9U, 0x8FU, 0x8CU, 0xAEU, 0xACU, 0x9BU, 0xA0U, 0x99U, 0xBCU, 0xB8U,
    0x40U, 0x3BU, 0x3AU, 0x70U, 0x3CU, 0x71U, 0x5AU, 0x3DU, 0x43U, 0x56U, 0x3FU, 0x1EU, 0x4AU, 0x1CU, 0x5DU, 0x3EU,
    0x5CU, 0x1FU, 0x5FU, 0x5EU, 0x37U, 0x7BU, 0x7FU, 0x36U, 0x7AU, 0x7EU, 0x33U, 0x4BU, 0x4CU, 0x1DU, 0x6CU, 0x5BU,
    0x78U, 0x41U, 0x35U, 0x34U, 0x74U, 0x30U, 0x38U, 0x75U, 0x39U, 0x4DU, 0x6FU, 0x6EU, 0x32U, 0x77U, 0x76U, 0x72U,
    0x73U, 0x47U, 0x7CU, 0x53U, 0x31U, 0x4EU, 0x6DU, 0x48U, 0x46U, 0x7DU, 0x44U, 0x1BU, 0x58U, 0x79U, 0x42U, 0x60U
};

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)(value >> 8U);
}

static uint8_t trimmed_name_length(const uint8_t *name)
{
    uint8_t length = 0U;

    while ((length < MZ700_FAST3_NAME_BYTES) &&
           (name[length] != 0x00U) && (name[length] != 0x0DU))
    {
        ++length;
    }
    while ((length != 0U) && (name[length - 1U] == 0x20U))
    {
        --length;
    }
    return length;
}

static uint8_t displayed_name_length(const uint8_t *name)
{
    const uint8_t length = trimmed_name_length(name);
    return (length != 0U) ? length : 1U;
}

uint8_t mz700_fast3_runtime_size(const uint8_t *original_name)
{
    uint8_t name_length;

    if (original_name == NULL) return 0U;
    name_length = displayed_name_length(original_name);
    return (uint8_t)(MZ700_FAST3_DISPLAY_CODE_BYTES +
                     MZ700_FAST3_CORE_BYTES +
                     name_length);
}

static bool build_stage(uint16_t runtime_address, uint8_t runtime_size,
                        uint8_t stage[MZ700_FAST3_STAGE_BYTES])
{
    if ((runtime_size == 0U) ||
        (runtime_size > MZ700_FAST3_RUNTIME_CAPACITY))
    {
        return false;
    }

    if (runtime_address == MZ700_FAST3_RUNTIME_LOW_ADDR)
    {
        /* The complete runtime already resides in the header Description
           workspace at $1108.  Execute it there; no LOW relocation/copy. */
        memset(stage, 0, MZ700_FAST3_STAGE_BYTES);
        stage[0] = 0xC3U; /* jp $1108 */
        write_le16(stage + 1U, MZ700_FAST3_RUNTIME_SOURCE_ADDR);
        return true;
    }

    if (runtime_address == MZ700_FAST3_RUNTIME_HIGH_ADDR)
    {
        stage[0] = 0x21U;
        write_le16(stage + 1U, MZ700_FAST3_RUNTIME_SOURCE_ADDR);
        stage[3] = 0x11U;
        write_le16(stage + 4U, runtime_address);
        stage[6] = 0x01U;
        write_le16(stage + 7U, runtime_size);
        stage[9] = 0xEDU;
        stage[10] = 0xB0U; /* LDIR: HIGH destination does not overlap. */
        stage[11] = 0xC3U;
        write_le16(stage + 12U, runtime_address);
        return true;
    }

    return false;
}

static bool qadcn_encode(uint8_t display_byte, uint8_t *source_byte)
{
    uint16_t source;

    if (source_byte == NULL) return false;
    for (source = 0U; source < 256U; ++source)
    {
        if ((source != 0x0DU) &&
            (pgm_read_byte(mz700_qadcn_P + source) == display_byte))
        {
            *source_byte = (uint8_t)source;
            return true;
        }
    }
    return false;
}

bool mz700_fast3_stage_is_encodable(uint16_t runtime_address,
                                    uint8_t runtime_size)
{
    uint8_t stage[MZ700_FAST3_STAGE_BYTES];
    uint8_t encoded;

    if (!build_stage(runtime_address, runtime_size, stage)) return false;
    for (uint8_t i = 0U; i < MZ700_FAST3_STAGE_BYTES; ++i)
    {
        if (!qadcn_encode(stage[i], &encoded)) return false;
        if (pgm_read_byte(mz700_qadcn_P + encoded) != stage[i]) return false;
    }
    return true;
}

static uint8_t build_status_display(const uint8_t *original_name,
                                    uint8_t *destination)
{
    uint8_t offset = 0U;
    uint8_t name_length = trimmed_name_length(original_name);

    if (name_length == 0U)
    {
        destination[offset++] = pgm_read_byte(mz700_qadcn_P + 0x20U);
        return offset;
    }

    for (uint8_t i = 0U; i < name_length; ++i)
    {
        destination[offset++] = pgm_read_byte(mz700_qadcn_P + original_name[i]);
    }
    return offset;
}

static uint8_t build_runtime(uint8_t *destination,
                             uint16_t runtime_address,
                             uint16_t payload_size,
                             uint16_t payload_load,
                             uint16_t payload_exec,
                             const uint8_t *original_name)
{
    uint8_t offset = 0U;
    uint8_t status_length = displayed_name_length(original_name);
    uint16_t status_address = (uint16_t)(runtime_address +
                              MZ700_FAST3_DISPLAY_CODE_BYTES +
                              MZ700_FAST3_CORE_BYTES);

    /* Clear the 40x25 character VRAM only after stage 1 has relocated us. */
    destination[offset++] = 0xAFU;             /* xor a */
    destination[offset++] = 0x21U;             /* ld hl,$D000 */
    write_le16(destination + offset, 0xD000U); offset += 2U;
    destination[offset++] = 0x77U;             /* ld (hl),a */
    destination[offset++] = 0x11U;             /* ld de,$D001 */
    write_le16(destination + offset, 0xD001U); offset += 2U;
    destination[offset++] = 0x01U;             /* ld bc,$03E7 */
    write_le16(destination + offset, 0x03E7U); offset += 2U;
    destination[offset++] = 0xEDU;
    destination[offset++] = 0xB0U;             /* ldir */

    destination[offset++] = 0x21U;             /* ld hl,status */
    write_le16(destination + offset, status_address); offset += 2U;
    destination[offset++] = 0x11U;             /* ld de,$D000 */
    write_le16(destination + offset, 0xD000U); offset += 2U;
    destination[offset++] = 0x01U;             /* ld bc,status length */
    write_le16(destination + offset, status_length); offset += 2U;
    destination[offset++] = 0xEDU;
    destination[offset++] = 0xB0U;             /* ldir */

    destination[offset++] = 0x16U; destination[offset++] = 0xE4U;
    destination[offset++] = 0x1EU; destination[offset++] = 0xE0U;
    destination[offset++] = 0x21U;             /* ld hl,$0000 */
    write_le16(destination + offset, 0x0000U); offset += 2U;
    destination[offset++] = 0x4AU;             /* copy: ld c,d */
    destination[offset++] = 0xEDU; destination[offset++] = 0x79U;
    destination[offset++] = 0x7EU;
    destination[offset++] = 0x4BU;             /* ld c,e */
    destination[offset++] = 0xEDU; destination[offset++] = 0x79U;
    destination[offset++] = 0x77U;
    destination[offset++] = 0x23U;
    destination[offset++] = 0xCBU; destination[offset++] = 0x64U;
    destination[offset++] = 0x28U; destination[offset++] = 0xF3U;

    destination[offset++] = 0x3EU; destination[offset++] = 0x15U;
    destination[offset++] = 0x32U;             /* ld ($0A4B),a */
    write_le16(destination + offset, 0x0A4BU); offset += 2U;
    destination[offset++] = 0x21U;             /* restore SIZE */
    write_le16(destination + offset, payload_size); offset += 2U;
    destination[offset++] = 0x22U;
    write_le16(destination + offset, 0x1102U); offset += 2U;
    destination[offset++] = 0x21U;             /* restore LOAD */
    write_le16(destination + offset, payload_load); offset += 2U;
    destination[offset++] = 0x22U;
    write_le16(destination + offset, 0x1104U); offset += 2U;
    destination[offset++] = 0xCDU;             /* call RDDAT */
    write_le16(destination + offset, 0x002AU); offset += 2U;
    /* RDDAT returns Carry with A=1/2 on a tape error.  The original monitor
       LOAD command branches to $00FE, which reports the matching CMT error.
       Do the same instead of executing an unread/partial payload. */
    destination[offset++] = 0xDAU;             /* jp c,$00FE */
    write_le16(destination + offset, 0x00FEU); offset += 2U;
    destination[offset++] = 0xAFU;             /* xor a */
    destination[offset++] = 0xD3U; destination[offset++] = 0xE4U;
    destination[offset++] = 0xC3U;             /* jp original EXEC */
    write_le16(destination + offset, payload_exec); offset += 2U;

    offset = (uint8_t)(offset + build_status_display(original_name,
                                                       destination + offset));
    return offset;
}

static bool build_header_prefix(uint8_t *header,
                                uint16_t runtime_address,
                                uint8_t runtime_size)
{
    uint8_t stage[MZ700_FAST3_STAGE_BYTES];
    uint8_t encoded[MZ700_FAST3_STAGE_BYTES];

    if ((header == NULL) ||
        !build_stage(runtime_address, runtime_size, stage))
    {
        return false;
    }
    for (uint8_t i = 0U; i < MZ700_FAST3_STAGE_BYTES; ++i)
    {
        if (!qadcn_encode(stage[i], encoded + i)) return false;
    }

    memset(header, 0, MZ700_FAST3_HEADER_BYTES);
    header[MZ700_FAST3_HEADER_TYPE_OFFSET] = MZ700_FAST3_HEADER_TYPE;
    memcpy(header + MZ700_FAST3_HEADER_NAME_OFFSET, encoded,
           MZ700_FAST3_STAGE_BYTES);
    header[MZ700_FAST3_HEADER_NAME_END_OFFSET] = 0x0DU;
    write_le16(header + MZ700_FAST3_HEADER_SIZE_OFFSET, 0U);
    write_le16(header + MZ700_FAST3_HEADER_LOAD_OFFSET, 0U);
    write_le16(header + MZ700_FAST3_HEADER_EXEC_OFFSET, MZ700_FAST3_HEADER_EXEC);
    return true;
}

bool mz700_header_only_build(uint8_t *header,
                             uint16_t runtime_address,
                             const uint8_t *runtime,
                             uint8_t runtime_size)
{
    if ((runtime == NULL) ||
        !build_header_prefix(header, runtime_address, runtime_size))
    {
        return false;
    }

    memcpy(header + MZ700_FAST3_HEADER_RUNTIME_OFFSET,
           runtime, runtime_size);
    return true;
}

bool mz700_fast3_build_header(uint8_t *header,
                              uint16_t runtime_address,
                              uint8_t runtime_size,
                              uint16_t payload_size,
                              uint16_t payload_load,
                              uint16_t payload_exec,
                              const uint8_t *original_name)
{
    uint8_t built_size;

    if ((original_name == NULL) ||
        !build_header_prefix(header, runtime_address, runtime_size))
    {
        return false;
    }

    built_size = build_runtime(header + MZ700_FAST3_HEADER_RUNTIME_OFFSET,
                               runtime_address, payload_size, payload_load,
                               payload_exec, original_name);
    return built_size == runtime_size;
}
