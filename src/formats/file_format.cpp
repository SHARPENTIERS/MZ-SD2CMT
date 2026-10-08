#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "file_format.h"

static bool chars_equal_ignore_case(char a, char b)
{
    return (char)toupper((unsigned char)a) == (char)toupper((unsigned char)b);
}

static bool extension_equals_P(const char *filename, PGM_P extension)
{
    size_t filename_len;
    size_t extension_len;
    const char *filename_extension;

    if ((filename == NULL) || (extension == NULL))
    {
        return false;
    }

    filename_len = strlen(filename);
    extension_len = strlen_P(extension);
    if (filename_len < extension_len)
    {
        return false;
    }

    filename_extension = filename + filename_len - extension_len;
    for (size_t i = 0U; i < extension_len; ++i)
    {
        if (!chars_equal_ignore_case(filename_extension[i],
                                     (char)pgm_read_byte(extension + i)))
        {
            return false;
        }
    }
    return true;
}

bool file_format_is_sharp_tape(file_format_t format)
{
    return (format == FILE_FORMAT_MZF) ||
           (format == FILE_FORMAT_MZT) ||
           (format == FILE_FORMAT_MZQ) ||
           (format == FILE_FORMAT_M12);
}

bool file_format_is_record_container(file_format_t format)
{
    return (format == FILE_FORMAT_MZT) || (format == FILE_FORMAT_MZQ);
}

file_format_t file_format_detect_from_name(const char *filename)
{
    if (extension_equals_P(filename, PSTR(".MZF"))) return FILE_FORMAT_MZF;
    if (extension_equals_P(filename, PSTR(".MZT"))) return FILE_FORMAT_MZT;
    if (extension_equals_P(filename, PSTR(".MZQ"))) return FILE_FORMAT_MZQ;
    if (extension_equals_P(filename, PSTR(".QD"))) return FILE_FORMAT_MZQ;
    if (extension_equals_P(filename, PSTR(".QDF"))) return FILE_FORMAT_MZQ;
    if (extension_equals_P(filename, PSTR(".M12"))) return FILE_FORMAT_M12;
    if (extension_equals_P(filename, PSTR(".LEP"))) return FILE_FORMAT_LEP;
    if (extension_equals_P(filename, PSTR(".L16"))) return FILE_FORMAT_L16;
    if (extension_equals_P(filename, PSTR(".WAV"))) return FILE_FORMAT_WAV;
    if (extension_equals_P(filename, PSTR(".TAP"))) return FILE_FORMAT_TAP;
    return FILE_FORMAT_UNKNOWN;
}
