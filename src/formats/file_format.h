#pragma once

typedef enum
{
    FILE_FORMAT_UNKNOWN = 0,
    FILE_FORMAT_MZF,
    FILE_FORMAT_MZT,
    FILE_FORMAT_M12,
    FILE_FORMAT_LEP,
    FILE_FORMAT_L16,
    FILE_FORMAT_WAV,
    FILE_FORMAT_TAP
} file_format_t;

/*
    MZF/M12 contain one Sharp 128-byte tape header followed by the declared
    data block. MZT is a concatenation of complete header+data records.
*/
bool file_format_is_sharp_tape(file_format_t format);

file_format_t file_format_detect_from_name(const char *filename);
