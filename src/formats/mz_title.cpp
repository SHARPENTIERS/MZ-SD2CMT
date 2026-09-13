#include "mz_title.h"

#include <avr/pgmspace.h>

/* Complete Sharp MZ-80A character-code to ASCII map. Unsupported graphical
   characters become spaces. Kept in flash so title conversion uses no 256B
   SRAM table. Source: zSoft common/tranzputer.c (GPLv3), Philip Smart. */
static const uint8_t sharp_mz_to_ascii_P[256] PROGMEM = {
    /* 00 */ 0x00U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x00U, 0x20U, 0x20U,
    /* 10 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
    /* 20 */ 0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U,
             0x28U, 0x29U, 0x2AU, 0x2BU, 0x2CU, 0x2DU, 0x2EU, 0x2FU,
    /* 30 */ 0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U,
             0x38U, 0x39U, 0x3AU, 0x3BU, 0x3CU, 0x3DU, 0x3EU, 0x3FU,
    /* 40 */ 0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U, 0x46U, 0x47U,
             0x48U, 0x49U, 0x4AU, 0x4BU, 0x4CU, 0x4DU, 0x4EU, 0x4FU,
    /* 50 */ 0x50U, 0x51U, 0x52U, 0x53U, 0x54U, 0x55U, 0x56U, 0x57U,
             0x58U, 0x59U, 0x5AU, 0x5BU, 0x5CU, 0x5DU, 0x5EU, 0x5FU,
    /* 60 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
    /* 70 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
    /* 80 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
    /* 90 */ 0x20U, 0x20U, 0x65U, 0x20U, 0x20U, 0x20U, 0x74U, 0x67U,
             0x68U, 0x20U, 0x62U, 0x78U, 0x64U, 0x72U, 0x70U, 0x63U,
    /* A0 */ 0x71U, 0x61U, 0x7AU, 0x77U, 0x73U, 0x75U, 0x69U, 0x20U,
             0x4FU, 0x6BU, 0x66U, 0x76U, 0x20U, 0x75U, 0x42U, 0x6AU,
    /* B0 */ 0x6EU, 0x20U, 0x55U, 0x6DU, 0x20U, 0x20U, 0x20U, 0x6FU,
             0x6CU, 0x41U, 0x6FU, 0x61U, 0x20U, 0x79U, 0x20U, 0x20U,
    /* C0 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
    /* D0 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
    /* E0 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
    /* F0 */ 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U,
             0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U, 0x20U
};

static_assert(sizeof(sharp_mz_to_ascii_P) == 256U,
              "Sharp MZ character map must contain all 256 codes");

static bool mz_title_safe_ascii(uint8_t value, bool display_mode)
{
    return ((value >= 'A') && (value <= 'Z')) ||
           ((value >= 'a') && (value <= 'z')) ||
           ((value >= '0') && (value <= '9')) ||
           (value == ' ') || (value == '-') || (value == '_') ||
           (value == '(') || (value == ')') || (value == '.') ||
           (value == '+') || (display_mode && (value == '/'));
}

static bool mz_title_is_alnum(uint8_t value)
{
    return ((value >= 'A') && (value <= 'Z')) ||
           ((value >= 'a') && (value <= 'z')) ||
           ((value >= '0') && (value <= '9'));
}

static uint8_t mz_title_score(const char *text, uint8_t raw_count,
                              bool direct_ascii, bool display_mode)
{
    uint8_t score = 0U;
    if (text == NULL) return 0U;
    for (uint8_t input = 0U; input < raw_count; ++input)
    {
        uint8_t raw_value = (uint8_t)text[input];
        uint8_t value;
        if ((raw_value == 0U) || (raw_value == 0x0DU)) break;
        value = direct_ascii ? raw_value :
            pgm_read_byte(&sharp_mz_to_ascii_P[raw_value]);
        if (mz_title_is_alnum(value))
        {
            if (score <= 253U) score = (uint8_t)(score + 2U);
        }
        else if (mz_title_safe_ascii(value, display_mode) && (value != ' '))
        {
            if (score != 0xFFU) score++;
        }
    }
    return score;
}

static bool mz_title_sanitize_mode(char *text, uint8_t raw_count,
                                   bool display_mode)
{
    uint8_t input = 0U;
    uint8_t output = 0U;
    bool have_alnum = false;
    bool previous_replacement = false;
    const bool direct_ascii =
        mz_title_score(text, raw_count, true, display_mode) >=
        mz_title_score(text, raw_count, false, display_mode);

    if (text == NULL) return false;
    while (input < raw_count)
    {
        uint8_t raw_value = (uint8_t)text[input++];
        uint8_t value;

        if ((raw_value == 0U) || (raw_value == 0x0DU)) break;
        value = direct_ascii ? raw_value :
            pgm_read_byte(&sharp_mz_to_ascii_P[raw_value]);
        if (((value == ' ') || (value == '.')) && (output == 0U)) continue;

        if (!mz_title_safe_ascii(value, display_mode))
        {
            if (previous_replacement) continue;
            value = '_';
            previous_replacement = true;
        }
        else
        {
            previous_replacement = false;
        }

        if (mz_title_is_alnum(value)) have_alnum = true;
        text[output++] = (char)value;
    }

    while ((output != 0U) &&
           ((text[output - 1U] == ' ') || (text[output - 1U] == '.')))
    {
        output--;
    }
    text[output] = '\0';
    return have_alnum && (output != 0U);
}

bool mz_title_sanitize_filename(char *text, uint8_t raw_count)
{
    return mz_title_sanitize_mode(text, raw_count, false);
}

bool mz_title_decode_display(const uint8_t *source, uint8_t raw_count,
                     char *destination, size_t destination_size)
{
    size_t copy_count;

    if ((source == NULL) || (destination == NULL) ||
        (destination_size == 0U))
    {
        return false;
    }

    copy_count = raw_count;
    if (copy_count >= destination_size)
    {
        copy_count = destination_size - 1U;
    }
    for (size_t index = 0U; index < copy_count; ++index)
    {
        destination[index] = (char)source[index];
    }
    destination[copy_count] = '\0';
    return mz_title_sanitize_mode(destination, (uint8_t)copy_count, true);
}
