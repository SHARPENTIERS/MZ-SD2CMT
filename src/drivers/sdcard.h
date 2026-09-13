#ifndef SD2CMT2_SDCARD_H
#define SD2CMT2_SDCARD_H

#include <stdbool.h>
#include <stdint.h>

#define SDCARD_ENTRY_NAME_MAX 64U

typedef struct
{
    char name[SDCARD_ENTRY_NAME_MAX];
    bool is_dir;
    /* SdFat returns an empty string when the complete LFN does not fit. */
    bool name_too_long;
    uint32_t size;

    /* Physical order in the directory scan. It is only a stable tie-breaker
       when two entries compare equal by name and type. */
    uint16_t source_index;

} sdcard_entry_t;

void sdcard_early_prepare_pins(void);

bool sdcard_init(void);
bool sdcard_is_inserted(void);
bool sdcard_detect_poll(void);
bool sdcard_detect_consume_inserted_edge(void);
bool sdcard_detect_removed_edge(void);

/* Force a full SdFat/SPI initialization even if the card was mounted before.
   Used after card insertion/retry before the browser reopens root. */
bool sdcard_reinitialize(void);

bool sdcard_is_mounted(void);

uint16_t sdcard_count_entries(const char *path, uint16_t max_entries);

bool sdcard_read_entry_by_index(const char *path, uint16_t index, sdcard_entry_t *entry);

/*
    Single-pass browser bootstrap: opens path once, counts up to max_entries and
    returns the first alphabetical item from that same directory scan.  An empty
    directory is successful with *entry_count == 0 and an empty *first_entry.
    This avoids opening root once for counting and again for selection directly
    after SD-card initialization.
*/
bool sdcard_scan_directory_first_sorted(const char *path,
                                        uint16_t max_entries,
                                        uint16_t *entry_count,
                                        sdcard_entry_t *first_entry);

/*
    Memory-efficient alphabetical directory access for the browser.
    Directories sort before files; names compare case-insensitively for ASCII.
    These functions scan the directory and do not allocate a directory-name list
    in SRAM, which keeps them safe for Mega 2560 recording modes.
*/
bool sdcard_read_first_sorted_entry(const char *path,
                                     uint16_t max_entries,
                                     sdcard_entry_t *entry);
bool sdcard_read_last_sorted_entry(const char *path,
                                    uint16_t max_entries,
                                    sdcard_entry_t *entry);
bool sdcard_read_sorted_neighbor(const char *path,
                                 uint16_t max_entries,
                                 const sdcard_entry_t *reference,
                                 bool previous,
                                 sdcard_entry_t *entry);
bool sdcard_find_sorted_entry_by_identity(const char *path,
                                          uint16_t max_entries,
                                          const char *name,
                                          bool is_dir,
                                          uint16_t *sorted_index,
                                          sdcard_entry_t *entry);

uint16_t sdcard_count_root_entries(uint16_t max_entries);

bool sdcard_read_root_entry_by_index(uint16_t index, sdcard_entry_t *entry);

/*
    Sequential one-file stream for format readers and direct recording output.
    All functions are foreground only; never use them from a timer ISR.
*/
bool sdcard_file_open_read(const char *path);
bool sdcard_file_open_write(const char *path);
bool sdcard_file_exists(const char *path);

/* Creates directory_path when absent and verifies that it is a directory. */
bool sdcard_ensure_directory(const char *directory_path);

/*
    Finds the next shared RECxxxx sequence number in directory_path. Files
    REC0001.WAV, REC0001.LEP, REC0001.L16 and REC0001.MZF share one namespace, so the
    result is one greater than the highest compatible existing number.
*/
bool sdcard_next_record_sequence(const char *directory_path,
                                 uint16_t *next_sequence);

int16_t sdcard_file_read(void *buffer, uint16_t size);
int16_t sdcard_file_write(const void *buffer, uint16_t size);

/*
    Reserve contiguous clusters before recording starts. The reservation is
    optional; callers may continue if it fails due to a nearly full card.
*/
bool sdcard_file_preallocate(uint32_t length);
bool sdcard_file_truncate(uint32_t length);

/* True while SD programming is busy; realtime RECORD must defer its next
   sector until this becomes false. */
bool sdcard_file_is_busy(void);

bool sdcard_file_sync(void);

bool sdcard_file_seek(uint32_t position);
uint32_t sdcard_file_size(void);
uint32_t sdcard_file_position(void);
bool sdcard_file_is_open(void);
void sdcard_file_close(void);

/* Remove a closed file. Used after cancelled or failed recording. */
bool sdcard_file_remove(const char *path);
/* Rename a closed file without replacing an existing destination. */
bool sdcard_file_rename(const char *old_path, const char *new_path);

const char *sdcard_last_error(void);
uint8_t sdcard_last_error_code(void);
uint8_t sdcard_last_error_data(void);

#endif
