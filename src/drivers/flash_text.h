#ifndef SD2CMT2_FLASH_TEXT_H
#define SD2CMT2_FLASH_TEXT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>

/* Fixed UI/diagnostic strings remain in flash; only rendered 16-char lines
   and active error messages are copied to SRAM. */
static inline void flash_text_copy(char *destination, size_t destination_size,
                                   PGM_P source)
{
    if ((destination == NULL) || (destination_size == 0U)) return;
    if (source == NULL)
    {
        destination[0] = '\0';
        return;
    }
    strncpy_P(destination, source, destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

static inline int flash_text_vsnprintf(char *destination, size_t destination_size,
                                       PGM_P format, va_list args)
{
    return vsnprintf_P(destination, destination_size, format, args);
}

static inline int flash_text_snprintf(char *destination, size_t destination_size,
                                      PGM_P format, ...)
{
    va_list args;
    int result;
    va_start(args, format);
    result = flash_text_vsnprintf(destination, destination_size, format, args);
    va_end(args);
    return result;
}

#endif
