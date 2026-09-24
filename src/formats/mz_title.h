#ifndef SD2CMT2_MZ_TITLE_H
#define SD2CMT2_MZ_TITLE_H

#include <stddef.h>
#include <stdint.h>

/*
   Converts a Sharp MZ 17-byte title (or a shorter compatible title) to the
   printable ASCII subset used by the UI/AutoName.  Native Sharp character
   codes and already-ASCII copier titles are both accepted.  0x00/0x0D end
   the title.  The conversion may be performed in place.
*/
bool mz_title_sanitize_filename(char *text, uint8_t raw_count);

/* Copy + sanitize helper for binary MZ headers. */
bool mz_title_decode_display(const uint8_t *source, uint8_t raw_count,
                             char *destination, size_t destination_size);

#endif
