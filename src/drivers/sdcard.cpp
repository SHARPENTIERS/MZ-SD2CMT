#include "sdcard.h"

#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <string.h>
#include "flash_text.h"

/*
    SD2CMT2 Reborn - Arduino Mega 2560 SD SPI mapping

    SD MISO -> Arduino 50
    SD MOSI -> Arduino 51
    SD SCK  -> Arduino 52
    SD SS   -> Arduino 53
*/

#define SD_CHIP_SELECT_PIN 53
#define SD_CARD_DETECT_PIN 3
#define SD_CARD_DETECT_ACTIVE_LOW 1
#define SD_CARD_DETECT_DEBOUNCE_MS 50U
#define SD_CARD_DETECT_POLL_INTERVAL_MS 10U

/* Card-detect state is bit-packed into one byte to minimize persistent SRAM. */
#define SD_CARD_DETECT_FLAG_KNOWN (1U << 0)
#define SD_CARD_DETECT_FLAG_LAST_INSERTED (1U << 1)
#define SD_CARD_DETECT_FLAG_CANDIDATE_INSERTED (1U << 2)
#define SD_CARD_DETECT_FLAG_INSERTED_PENDING (1U << 3)
#define SD_CARD_DETECT_FLAG_REMOVED_PENDING (1U << 4)

/*
    CMT/LCD hardware in this project does not share the Mega SPI bus. Dedicated
    SPI lets SdFat observe SD busy state instead of blocking a realtime record
    write behind internal card programming. 8 MHz is conservative for common
    5 V SD modules and still has ample bandwidth for 44.1 kHz PCM.
*/
#ifndef SD2CMT2_SD_SPI_MHZ
#define SD2CMT2_SD_SPI_MHZ 8
#endif

#ifndef SD2CMT2_SD_SPI_MODE
#define SD2CMT2_SD_SPI_MODE DEDICATED_SPI
#endif

static SdFat sd;
static FsFile sdcard_stream_file;

static bool sdcard_mounted = false;
static char sdcard_error[17];
static uint8_t sdcard_detect_flags = 0U;
static uint16_t sdcard_detect_candidate_since_ms = 0U;
static uint16_t sdcard_detect_last_poll_ms = 0U;

static void sdcard_set_error_P(PGM_P text)
{
    flash_text_copy(sdcard_error, sizeof(sdcard_error), text);
}

static bool sdcard_read_detect_pin(void)
{
#if SD_CARD_DETECT_ACTIVE_LOW
    return digitalRead(SD_CARD_DETECT_PIN) == LOW;
#else
    return digitalRead(SD_CARD_DETECT_PIN) == HIGH;
#endif
}

static bool sdcard_detect_flag(uint8_t flag)
{
    return (sdcard_detect_flags & flag) != 0U;
}

static void sdcard_detect_set_flag(uint8_t flag, bool enabled)
{
    if (enabled)
    {
        sdcard_detect_flags |= flag;
    }
    else
    {
        sdcard_detect_flags &= (uint8_t)~flag;
    }
}

static void sdcard_detect_clear_pending(void)
{
    sdcard_detect_flags &= (uint8_t)~(SD_CARD_DETECT_FLAG_INSERTED_PENDING |
                                     SD_CARD_DETECT_FLAG_REMOVED_PENDING);
}

static void sdcard_detect_reset_to_current(void)
{
    uint16_t now = (uint16_t)millis();
    bool inserted = sdcard_read_detect_pin();

    sdcard_detect_flags = SD_CARD_DETECT_FLAG_KNOWN;
    if (inserted)
    {
        sdcard_detect_flags |= (SD_CARD_DETECT_FLAG_LAST_INSERTED |
                                SD_CARD_DETECT_FLAG_CANDIDATE_INSERTED);
    }
    sdcard_detect_candidate_since_ms = now;
    sdcard_detect_last_poll_ms = now;
    sdcard_detect_clear_pending();
}

static uint8_t sdcard_error_code = 0;
static uint8_t sdcard_error_data = 0;

static void sdcard_close_all_files(void)
{
    if (sdcard_stream_file.isOpen())
    {
        sdcard_stream_file.close();
    }
}

static void sdcard_set_card_error(void)
{
    sdcard_close_all_files();
    sdcard_mounted = false;
    (void)sdcard_detect_poll();
    sdcard_set_error_P(sdcard_detect_flag(SD_CARD_DETECT_FLAG_REMOVED_PENDING) ?
                       PSTR("INSERT CARD") : PSTR("SD CARD ERROR"));

    if (sd.card() != NULL)
    {
        sdcard_error_code = sd.card()->errorCode();
        sdcard_error_data = (uint8_t)sd.card()->errorData();
    }
    else
    {
        sdcard_error_code = 0;
        sdcard_error_data = 0;
    }
}

/*
    Browser operations probe with CMD10. Stream and conversion reads avoid a
    probe per block because that would add latency; an actual I/O error drops
    the mounted state through sdcard_set_card_error().
*/
static bool sdcard_probe_present(void)
{
    cid_t cid;

    if (!sdcard_mounted || (sd.card() == NULL))
    {
        return false;
    }

    if (!sd.card()->readCID(&cid))
    {
        sdcard_set_card_error();
        return false;
    }

    return true;
}

void sdcard_early_prepare_pins(void)
{
    if (sdcard_error[0] == '\0') sdcard_set_error_P(PSTR("NOT INIT"));
    pinMode(SD_CHIP_SELECT_PIN, OUTPUT);
    digitalWrite(SD_CHIP_SELECT_PIN, HIGH);

    pinMode(SD_CARD_DETECT_PIN, INPUT_PULLUP);
    if (!sdcard_detect_flag(SD_CARD_DETECT_FLAG_KNOWN))
    {
        sdcard_detect_reset_to_current();
    }
    else
    {
        (void)sdcard_detect_poll();
    }
}

static void sdcard_send_idle_clocks(void)
{
    digitalWrite(SD_CHIP_SELECT_PIN, HIGH);

    SPI.begin();
    SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));

    for (uint8_t i = 0; i < 10; i++)
    {
        SPI.transfer(0xFF);
    }

    SPI.endTransaction();
}

static bool sdcard_initialize(bool force_reinitialize)
{
    sdcard_early_prepare_pins();

    if (!force_reinitialize && sdcard_mounted && sdcard_probe_present())
    {
        sdcard_set_error_P(PSTR("OK"));
        sdcard_error_code = 0;
        sdcard_error_data = 0;
        sdcard_detect_reset_to_current();
        return true;
    }

    sdcard_close_all_files();
    sdcard_mounted = false;
    sdcard_set_error_P(PSTR("SD CARD ERROR"));
    sdcard_error_code = 0;
    sdcard_error_data = 0;

    /* Card insertion is debounced by the foreground detector. Initialization
       starts immediately after the required SPI idle clocks. */
    sdcard_send_idle_clocks();

    if (!sd.begin(SdSpiConfig(
            SD_CHIP_SELECT_PIN,
            SD2CMT2_SD_SPI_MODE,
            SD_SCK_MHZ(SD2CMT2_SD_SPI_MHZ))))
    {
        sdcard_set_card_error();
        return false;
    }

    sdcard_mounted = true;

    if (!sdcard_probe_present())
    {
        return false;
    }

    sdcard_set_error_P(PSTR("OK"));
    sdcard_error_code = 0;
    sdcard_error_data = 0;
    sdcard_detect_reset_to_current();
    return true;
}

bool sdcard_is_inserted(void)
{
    return sdcard_read_detect_pin();
}

bool sdcard_detect_poll(void)
{
    uint16_t now = (uint16_t)millis();
    bool inserted;

    if (!sdcard_detect_flag(SD_CARD_DETECT_FLAG_KNOWN))
    {
        sdcard_detect_reset_to_current();
        return false;
    }

    /* PLAY services the transport several times per main-loop pass.  The
       mechanical card-detect switch does not need audio-rate sampling: limit
       the physical pin read here so every caller gets the same cheap 100 Hz
       poll, while the existing 50 ms debounce still has five samples. */
    if ((uint16_t)(now - sdcard_detect_last_poll_ms) <
        SD_CARD_DETECT_POLL_INTERVAL_MS)
    {
        return false;
    }
    sdcard_detect_last_poll_ms = now;
    inserted = sdcard_read_detect_pin();

    if (inserted == sdcard_detect_flag(SD_CARD_DETECT_FLAG_LAST_INSERTED))
    {
        sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_CANDIDATE_INSERTED, inserted);
        sdcard_detect_candidate_since_ms = now;
        return false;
    }

    if (inserted != sdcard_detect_flag(SD_CARD_DETECT_FLAG_CANDIDATE_INSERTED))
    {
        sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_CANDIDATE_INSERTED, inserted);
        sdcard_detect_candidate_since_ms = now;
        return false;
    }

    if ((uint16_t)(now - sdcard_detect_candidate_since_ms) <
        SD_CARD_DETECT_DEBOUNCE_MS)
    {
        return false;
    }

    if (inserted)
    {
        sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_INSERTED_PENDING, true);
        sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_REMOVED_PENDING, false);
    }
    else
    {
        sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_REMOVED_PENDING, true);
        sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_INSERTED_PENDING, false);
    }

    sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_LAST_INSERTED, inserted);
    return true;
}

bool sdcard_detect_consume_inserted_edge(void)
{
    (void)sdcard_detect_poll();
    if (!sdcard_detect_flag(SD_CARD_DETECT_FLAG_INSERTED_PENDING))
    {
        return false;
    }

    sdcard_detect_set_flag(SD_CARD_DETECT_FLAG_INSERTED_PENDING, false);
    return true;
}

bool sdcard_detect_removed_edge(void)
{
    (void)sdcard_detect_poll();
    return sdcard_detect_flag(SD_CARD_DETECT_FLAG_REMOVED_PENDING);
}
bool sdcard_init(void)
{
    return sdcard_initialize(false);
}

bool sdcard_reinitialize(void)
{
    return sdcard_initialize(true);
}

bool sdcard_is_mounted(void)
{
    return sdcard_mounted;
}

uint16_t sdcard_count_entries(const char *path, uint16_t max_entries)
{
    FsFile dir;
    FsFile file;
    uint16_t count = 0;

    if (path == NULL)
    {
        sdcard_set_error_P(PSTR("BAD PATH"));
        return 0;
    }

    if (!sdcard_probe_present())
    {
        return 0;
    }

    if (!dir.open(path))
    {
        if (!sdcard_probe_present())
        {
            return 0;
        }
        sdcard_set_error_P(PSTR("DIR FAIL"));
        return 0;
    }

    if (!dir.isDir())
    {
        dir.close();
        sdcard_set_error_P(PSTR("NOT DIR"));
        return 0;
    }

    while (file.openNext(&dir, O_RDONLY))
    {
        count++;
        file.close();

        if (count >= max_entries)
        {
            break;
        }
    }

    dir.close();

    if (!sdcard_probe_present())
    {
        return 0;
    }

    sdcard_set_error_P(PSTR("OK"));
    return count;
}

bool sdcard_read_entry_by_index(const char *path, uint16_t index, sdcard_entry_t *entry)
{
    FsFile dir;
    FsFile file;
    uint16_t current_index = 0;

    if (path == NULL)
    {
        sdcard_set_error_P(PSTR("BAD PATH"));
        return false;
    }

    if (entry == NULL)
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return false;
    }

    memset(entry, 0, sizeof(sdcard_entry_t));

    if (!sdcard_probe_present())
    {
        return false;
    }

    if (!dir.open(path))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("DIR FAIL"));
        return false;
    }

    if (!dir.isDir())
    {
        dir.close();
        sdcard_set_error_P(PSTR("NOT DIR"));
        return false;
    }

    while (file.openNext(&dir, O_RDONLY))
    {
        if (current_index == index)
        {
            entry->name_too_long = (file.getName(entry->name, sizeof(entry->name)) == 0U);
            entry->name[sizeof(entry->name) - 1] = '\0';
            if (entry->name_too_long)
            {
                flash_text_copy(entry->name, sizeof(entry->name), PSTR("NAME TOO LONG"));
            }
            entry->is_dir = file.isDir();
            entry->size = file.fileSize();
            entry->source_index = current_index;
            file.close();
            dir.close();

            if (!sdcard_probe_present())
            {
                memset(entry, 0, sizeof(sdcard_entry_t));
                return false;
            }

            sdcard_set_error_P(PSTR("OK"));
            return true;
        }

        current_index++;
        file.close();
    }

    dir.close();

    if (!sdcard_probe_present())
    {
        return false;
    }

    flash_text_copy(entry->name, sizeof(entry->name), PSTR("NO ENTRY"));
    entry->is_dir = false;
    entry->size = 0;
    sdcard_set_error_P(PSTR("NO ENTRY"));
    return false;
}

/*
    Browser sorting deliberately uses repeated directory scans instead of a RAM
    table. A 999-entry table plus names would exceed the practical SRAM budget
    of Mega 2560 while recording can also borrow browser scratch memory.
*/
static void sdcard_copy_entry_from_file(FsFile *file,
                                        uint16_t source_index,
                                        sdcard_entry_t *entry)
{
    memset(entry, 0, sizeof(sdcard_entry_t));
    entry->name_too_long = (file->getName(entry->name, sizeof(entry->name)) == 0U);
    entry->name[sizeof(entry->name) - 1U] = '\0';
    if (entry->name_too_long)
    {
        flash_text_copy(entry->name, sizeof(entry->name), PSTR("NAME TOO LONG"));
    }
    entry->is_dir = file->isDir();
    entry->size = file->fileSize();
    entry->source_index = source_index;
}

static char sdcard_ascii_upper(char value)
{
    if ((value >= 'a') && (value <= 'z'))
    {
        return (char)(value - ('a' - 'A'));
    }
    return value;
}

static bool sdcard_browser_entry_visible(const sdcard_entry_t *entry)
{
    size_t length;
    char middle;
    char last;

    if (entry == NULL) return false;
    if (entry->is_dir || entry->name_too_long) return true;

    length = strlen(entry->name);
    if ((length < 4U) || (entry->name[length - 4U] != '.')) return true;
    if (sdcard_ascii_upper(entry->name[length - 3U]) != 'M') return true;

    middle = sdcard_ascii_upper(entry->name[length - 2U]);
    last = sdcard_ascii_upper(entry->name[length - 1U]);

    /* MFI/MTI are metadata companions, not playable browser entries.
       MZI is also hidden for media compatibility. */
    return !((last == 'I') &&
             ((middle == 'F') || (middle == 'T') || (middle == 'Z')));
}

static int8_t sdcard_compare_entry_names(const char *left, const char *right)
{
    while ((*left != '\0') && (*right != '\0'))
    {
        uint8_t left_character = (uint8_t)sdcard_ascii_upper(*left);
        uint8_t right_character = (uint8_t)sdcard_ascii_upper(*right);

        if (left_character < right_character)
        {
            return -1;
        }
        if (left_character > right_character)
        {
            return 1;
        }

        left++;
        right++;
    }

    if (*left != '\0')
    {
        return 1;
    }
    if (*right != '\0')
    {
        return -1;
    }
    return 0;
}

static int8_t sdcard_compare_entries(const sdcard_entry_t *left,
                                     const sdcard_entry_t *right)
{
    int8_t name_relation;

    if (left->is_dir && !right->is_dir)
    {
        return -1;
    }
    if (!left->is_dir && right->is_dir)
    {
        return 1;
    }

    name_relation = sdcard_compare_entry_names(left->name, right->name);
    if (name_relation != 0)
    {
        return name_relation;
    }

    /* FAT permits combinations such as a long-file name and an 8.3 alias
       that compare equal here. Keep the browser order total and stable. */
    if (left->source_index < right->source_index)
    {
        return -1;
    }
    if (left->source_index > right->source_index)
    {
        return 1;
    }
    return 0;
}

static bool sdcard_open_directory_for_browser(const char *path, FsFile *directory)
{
    if (path == NULL)
    {
        sdcard_set_error_P(PSTR("BAD PATH"));
        return false;
    }
    if (directory == NULL)
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return false;
    }
    if (!sdcard_probe_present())
    {
        return false;
    }
    if (!directory->open(path))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("DIR FAIL"));
        return false;
    }
    if (!directory->isDir())
    {
        directory->close();
        sdcard_set_error_P(PSTR("NOT DIR"));
        return false;
    }
    return true;
}

static bool sdcard_finish_browser_directory_read(FsFile *directory)
{
    if ((directory != NULL) && directory->isOpen())
    {
        directory->close();
    }
    return sdcard_probe_present();
}

bool sdcard_scan_directory_first_sorted(const char *path,
                                        uint16_t max_entries,
                                        uint16_t *entry_count,
                                        sdcard_entry_t *first_entry)
{
    FsFile directory;
    FsFile file;
    sdcard_entry_t candidate;
    bool have_candidate = false;
    uint16_t count = 0U;

    if ((entry_count == NULL) || (first_entry == NULL))
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return false;
    }

    *entry_count = 0U;
    memset(first_entry, 0, sizeof(sdcard_entry_t));

    if (!sdcard_open_directory_for_browser(path, &directory))
    {
        return false;
    }

    while (file.openNext(&directory, O_RDONLY))
    {
        sdcard_entry_t current;

        if ((max_entries != 0U) && (count >= max_entries))
        {
            file.close();
            break;
        }

        sdcard_copy_entry_from_file(&file, count, &current);
        if (!sdcard_browser_entry_visible(&current))
        {
            file.close();
            continue;
        }
        count++;

        if (!have_candidate || (sdcard_compare_entries(&current, &candidate) < 0))
        {
            candidate = current;
            have_candidate = true;
        }

        file.close();
    }

    if (!sdcard_finish_browser_directory_read(&directory))
    {
        return false;
    }

    *entry_count = count;
    if (have_candidate)
    {
        *first_entry = candidate;
    }

    sdcard_set_error_P(PSTR("OK"));
    return true;
}

static bool sdcard_read_sorted_extreme(const char *path,
                                       uint16_t max_entries,
                                       bool want_last,
                                       sdcard_entry_t *entry)
{
    uint8_t attempt;

    if (entry == NULL)
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return false;
    }
    memset(entry, 0, sizeof(sdcard_entry_t));

    /* The initial directory access after SD power-up can occasionally finish
       before the first openNext() produces an item on some cards. Retry one
       completely closed scan. This does not use extra SRAM and only runs when
       the first scan was empty, not during normal successful navigation. */
    for (attempt = 0U; attempt < 2U; ++attempt)
    {
        FsFile directory;
        FsFile file;
        sdcard_entry_t candidate;
        bool have_candidate = false;
        uint16_t visited = 0U;

        if (!sdcard_open_directory_for_browser(path, &directory))
        {
            return false;
        }

        while (file.openNext(&directory, O_RDONLY))
        {
            sdcard_entry_t current;

            if ((max_entries != 0U) && (visited >= max_entries))
            {
                file.close();
                break;
            }
            sdcard_copy_entry_from_file(&file, visited, &current);
            if (!sdcard_browser_entry_visible(&current))
            {
                file.close();
                continue;
            }
            visited++;
            if (!have_candidate ||
                ((want_last && (sdcard_compare_entries(&current, &candidate) > 0)) ||
                 (!want_last && (sdcard_compare_entries(&current, &candidate) < 0))))
            {
                candidate = current;
                have_candidate = true;
            }
            file.close();
        }

        if (!sdcard_finish_browser_directory_read(&directory))
        {
            return false;
        }
        if (have_candidate)
        {
            *entry = candidate;
            sdcard_set_error_P(PSTR("OK"));
            return true;
        }

        /* Retry the closed directory scan immediately; no extra wait is
           required between two independent FAT scans. */
    }
    sdcard_set_error_P(PSTR("NO ENTRY"));
    return false;
}

bool sdcard_read_first_sorted_entry(const char *path,
                                     uint16_t max_entries,
                                     sdcard_entry_t *entry)
{
    uint16_t count = 0U;

    if (!sdcard_scan_directory_first_sorted(path, max_entries, &count, entry))
    {
        return false;
    }

    if (count == 0U)
    {
        sdcard_set_error_P(PSTR("NO ENTRY"));
        return false;
    }

    return true;
}

bool sdcard_read_last_sorted_entry(const char *path,
                                    uint16_t max_entries,
                                    sdcard_entry_t *entry)
{
    return sdcard_read_sorted_extreme(path, max_entries, true, entry);
}

bool sdcard_read_sorted_neighbor(const char *path,
                                 uint16_t max_entries,
                                 const sdcard_entry_t *reference,
                                 bool previous,
                                 sdcard_entry_t *entry)
{
    FsFile directory;
    FsFile file;
    sdcard_entry_t reference_copy;
    sdcard_entry_t candidate;
    bool have_candidate = false;
    uint16_t visited = 0U;

    if ((reference == NULL) || (entry == NULL))
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return false;
    }
    reference_copy = *reference;
    memset(entry, 0, sizeof(sdcard_entry_t));

    if (!sdcard_open_directory_for_browser(path, &directory))
    {
        return false;
    }

    while (file.openNext(&directory, O_RDONLY))
    {
        sdcard_entry_t current;
        int8_t relation;

        if ((max_entries != 0U) && (visited >= max_entries))
        {
            file.close();
            break;
        }
        sdcard_copy_entry_from_file(&file, visited, &current);
        if (!sdcard_browser_entry_visible(&current))
        {
            file.close();
            continue;
        }
        visited++;
        relation = sdcard_compare_entries(&current, &reference_copy);

        if (previous)
        {
            if ((relation < 0) &&
                (!have_candidate || (sdcard_compare_entries(&current, &candidate) > 0)))
            {
                candidate = current;
                have_candidate = true;
            }
        }
        else
        {
            if ((relation > 0) &&
                (!have_candidate || (sdcard_compare_entries(&current, &candidate) < 0)))
            {
                candidate = current;
                have_candidate = true;
            }
        }

        file.close();
    }

    if (!sdcard_finish_browser_directory_read(&directory))
    {
        return false;
    }
    if (!have_candidate)
    {
        sdcard_set_error_P(PSTR("NO ENTRY"));
        return false;
    }

    *entry = candidate;
    sdcard_set_error_P(PSTR("OK"));
    return true;
}

bool sdcard_find_sorted_entry_by_identity(const char *path,
                                          uint16_t max_entries,
                                          const char *name,
                                          bool is_dir,
                                          uint16_t *sorted_index,
                                          sdcard_entry_t *entry)
{
    FsFile directory;
    FsFile file;
    sdcard_entry_t matching_entry;
    uint16_t visited = 0U;
    uint16_t rank = 0U;
    bool found = false;

    if ((name == NULL) || (sorted_index == NULL) || (entry == NULL))
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return false;
    }

    if (!sdcard_open_directory_for_browser(path, &directory))
    {
        return false;
    }

    /* First matching physical entry is intentionally selected if identical
       names exist. Its source_index then makes the following sort rank unique. */
    while (file.openNext(&directory, O_RDONLY))
    {
        sdcard_entry_t current;

        if ((max_entries != 0U) && (visited >= max_entries))
        {
            file.close();
            break;
        }
        sdcard_copy_entry_from_file(&file, visited, &current);
        if (!sdcard_browser_entry_visible(&current))
        {
            file.close();
            continue;
        }
        visited++;

        if (!found && (current.is_dir == is_dir) && (strcmp(current.name, name) == 0))
        {
            matching_entry = current;
            found = true;
        }
        file.close();
    }

    if (!sdcard_finish_browser_directory_read(&directory))
    {
        return false;
    }
    if (!found)
    {
        sdcard_set_error_P(PSTR("NO ENTRY"));
        return false;
    }

    if (!sdcard_open_directory_for_browser(path, &directory))
    {
        return false;
    }

    visited = 0U;
    while (file.openNext(&directory, O_RDONLY))
    {
        sdcard_entry_t current;
        if ((max_entries != 0U) && (visited >= max_entries))
        {
            file.close();
            break;
        }
        sdcard_copy_entry_from_file(&file, visited, &current);
        if (!sdcard_browser_entry_visible(&current))
        {
            file.close();
            continue;
        }
        visited++;

        if (sdcard_compare_entries(&current, &matching_entry) < 0)
        {
            rank++;
        }
        file.close();
    }

    if (!sdcard_finish_browser_directory_read(&directory))
    {
        return false;
    }

    *sorted_index = rank;
    *entry = matching_entry;
    sdcard_set_error_P(PSTR("OK"));
    return true;
}

bool sdcard_file_open_read(const char *path)
{
    if (path == NULL)
    {
        sdcard_set_error_P(PSTR("BAD PATH"));
        return false;
    }

    if (!sdcard_probe_present())
    {
        return false;
    }

    if (sdcard_stream_file.isOpen())
    {
        sdcard_stream_file.close();
    }

    if (!sdcard_stream_file.open(path, O_RDONLY))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("FILE OPEN FAIL"));
        return false;
    }

    sdcard_set_error_P(PSTR("OK"));
    return true;
}

bool sdcard_file_open_write(const char *path)
{
    if (path == NULL)
    {
        sdcard_set_error_P(PSTR("BAD PATH"));
        return false;
    }

    if (!sdcard_probe_present())
    {
        return false;
    }

    if (sdcard_stream_file.isOpen())
    {
        sdcard_stream_file.close();
    }

    if (!sdcard_stream_file.open(path, O_RDWR | O_CREAT | O_TRUNC))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("FILE CREATE FAIL"));
        return false;
    }

    sdcard_set_error_P(PSTR("OK"));
    return true;
}

bool sdcard_file_exists(const char *path)
{
    if ((path == NULL) || !sdcard_mounted)
    {
        return false;
    }

    return sd.exists(path);
}

static bool sdcard_parse_record_sequence(const char *name, uint16_t *sequence)
{
    uint16_t value = 0U;
    bool valid_extension;

    if ((name == NULL) || (sequence == NULL) || (strlen(name) != 11U))
    {
        return false;
    }

    if ((sdcard_ascii_upper(name[0]) != 'R') ||
        (sdcard_ascii_upper(name[1]) != 'E') ||
        (sdcard_ascii_upper(name[2]) != 'C') ||
        (name[7] != '.'))
    {
        return false;
    }

    for (uint8_t index = 3U; index <= 6U; ++index)
    {
        if ((name[index] < '0') || (name[index] > '9'))
        {
            return false;
        }
        value = (uint16_t)(value * 10U + (uint16_t)(name[index] - '0'));
    }

    valid_extension =
        ((sdcard_ascii_upper(name[8]) == 'W') &&
         (sdcard_ascii_upper(name[9]) == 'A') &&
         (sdcard_ascii_upper(name[10]) == 'V')) ||
        ((sdcard_ascii_upper(name[8]) == 'L') &&
         (sdcard_ascii_upper(name[9]) == 'E') &&
         (sdcard_ascii_upper(name[10]) == 'P')) ||
        ((sdcard_ascii_upper(name[8]) == 'L') &&
         (name[9] == '1') && (name[10] == '6'));
    if (!valid_extension)
    {
        valid_extension =
            (sdcard_ascii_upper(name[8]) == 'M') &&
            (sdcard_ascii_upper(name[9]) == 'Z') &&
            (sdcard_ascii_upper(name[10]) == 'F');
    }

    if (!valid_extension || (value == 0U) || (value > 9999U))
    {
        return false;
    }

    *sequence = value;
    return true;
}

bool sdcard_ensure_directory(const char *directory_path)
{
    FsFile directory;

    if ((directory_path == NULL) || (directory_path[0] != '/') ||
        (directory_path[1] == '\0'))
    {
        sdcard_set_error_P(PSTR("BAD DIRECTORY"));
        return false;
    }

    if (!sdcard_probe_present())
    {
        return false;
    }

    if (!sd.exists(directory_path) && !sd.mkdir(directory_path, true))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("MKDIR FAIL"));
        return false;
    }

    if (!directory.open(directory_path, O_RDONLY))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("DIR FAIL"));
        return false;
    }

    if (!directory.isDir())
    {
        directory.close();
        sdcard_set_error_P(PSTR("NOT DIR"));
        return false;
    }

    directory.close();
    sdcard_set_error_P(PSTR("OK"));
    return true;
}

bool sdcard_next_record_sequence(const char *directory_path,
                                 uint16_t *next_sequence)
{
    FsFile directory;
    FsFile entry;
    uint16_t highest = 0U;

    if ((next_sequence == NULL) || !sdcard_ensure_directory(directory_path))
    {
        return false;
    }

    if (!directory.open(directory_path, O_RDONLY) || !directory.isDir())
    {
        if (directory.isOpen())
        {
            directory.close();
        }
        sdcard_set_error_P(PSTR("DIR FAIL"));
        return false;
    }

    while (entry.openNext(&directory, O_RDONLY))
    {
        char name[sizeof(((sdcard_entry_t *)0)->name)];
        uint16_t sequence;

        memset(name, 0, sizeof(name));
        entry.getName(name, sizeof(name));
        name[sizeof(name) - 1U] = '\0';

        if (!entry.isDir() && sdcard_parse_record_sequence(name, &sequence) &&
            (sequence > highest))
        {
            highest = sequence;
        }

        entry.close();
    }

    directory.close();

    if (!sdcard_probe_present())
    {
        return false;
    }

    if (highest >= 9999U)
    {
        sdcard_set_error_P(PSTR("REC NAMES FULL"));
        return false;
    }

    *next_sequence = (uint16_t)(highest + 1U);
    sdcard_set_error_P(PSTR("OK"));
    return true;
}

int16_t sdcard_file_read(void *buffer, uint16_t size)
{
    int result;

    if ((buffer == NULL) || (size == 0U))
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return -1;
    }

    if (!sdcard_mounted || !sdcard_stream_file.isOpen())
    {
        sdcard_set_card_error();
        return -1;
    }

    result = sdcard_stream_file.read(buffer, size);

    if (result < 0)
    {
        sdcard_set_card_error();
        return -1;
    }

    if ((result == 0) &&
        (sdcard_stream_file.curPosition() < sdcard_stream_file.fileSize()))
    {
        if (!sdcard_probe_present())
        {
            return -1;
        }
        sdcard_set_error_P(PSTR("FILE READ FAIL"));
        return -1;
    }

    return (int16_t)result;
}

int16_t sdcard_file_write(const void *buffer, uint16_t size)
{
    int result;

    if ((buffer == NULL) || (size == 0U))
    {
        sdcard_set_error_P(PSTR("BAD ARG"));
        return -1;
    }

    if (!sdcard_mounted || !sdcard_stream_file.isOpen())
    {
        sdcard_set_card_error();
        return -1;
    }

    result = (int)sdcard_stream_file.write(buffer, size);

    if (result != (int)size)
    {
        if (!sdcard_probe_present())
        {
            return -1;
        }
        sdcard_set_error_P(PSTR("FILE WRITE FAIL"));
        return -1;
    }

    return (int16_t)result;
}

bool sdcard_file_preallocate(uint32_t length)
{
    if ((length == 0UL) || !sdcard_mounted || !sdcard_stream_file.isOpen())
    {
        sdcard_set_card_error();
        return false;
    }

    if (!sdcard_stream_file.preAllocate((uint64_t)length))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("PREALLOC FAIL"));
        return false;
    }

    sdcard_set_error_P(PSTR("OK"));
    return true;
}

bool sdcard_file_truncate(uint32_t length)
{
    if (!sdcard_mounted || !sdcard_stream_file.isOpen())
    {
        sdcard_set_card_error();
        return false;
    }

    if (!sdcard_stream_file.truncate((uint64_t)length))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("FILE TRUNC FAIL"));
        return false;
    }

    sdcard_set_error_P(PSTR("OK"));
    return true;
}

/*
    With DEDICATED_SPI, SdFat can test the card busy signal without forcing a
    blocking write wait. A caller may transmit one 512-byte sector after this
    returns false, then must wait for a later false before sending another.
*/
bool sdcard_file_is_busy(void)
{
    if (!sdcard_mounted || !sdcard_stream_file.isOpen())
    {
        return true;
    }

    return sdcard_stream_file.isBusy();
}

bool sdcard_file_sync(void)
{
    if (!sdcard_mounted || !sdcard_stream_file.isOpen())
    {
        sdcard_set_card_error();
        return false;
    }

    if (!sdcard_stream_file.sync())
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("FILE SYNC FAIL"));
        return false;
    }

    return true;
}

bool sdcard_file_seek(uint32_t position)
{
    if (!sdcard_mounted || !sdcard_stream_file.isOpen())
    {
        sdcard_set_card_error();
        return false;
    }

    if (!sdcard_stream_file.seekSet(position))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("FILE SEEK FAIL"));
        return false;
    }

    return true;
}

uint32_t sdcard_file_size(void)
{
    return sdcard_stream_file.isOpen() ?
        (uint32_t)sdcard_stream_file.fileSize() : 0UL;
}

uint32_t sdcard_file_position(void)
{
    return sdcard_stream_file.isOpen() ?
        (uint32_t)sdcard_stream_file.curPosition() : 0UL;
}

bool sdcard_file_is_open(void)
{
    return sdcard_stream_file.isOpen();
}

void sdcard_file_close(void)
{
    if (sdcard_stream_file.isOpen())
    {
        sdcard_stream_file.close();
    }
}

bool sdcard_file_remove(const char *path)
{
    if ((path == NULL) || !sdcard_mounted)
    {
        sdcard_set_error_P(PSTR("BAD PATH"));
        return false;
    }

    if (!sd.remove(path))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("FILE REMOVE ERR"));
        return false;
    }

    sdcard_set_error_P(PSTR("OK"));
    return true;
}

bool sdcard_file_rename(const char *old_path, const char *new_path)
{
    if ((old_path == NULL) || (new_path == NULL) || !sdcard_mounted ||
        sdcard_stream_file.isOpen())
    {
        sdcard_set_error_P(PSTR("BAD RENAME"));
        return false;
    }
    if (sd.exists(new_path))
    {
        sdcard_set_error_P(PSTR("FILE EXISTS"));
        return false;
    }
    if (!sd.rename(old_path, new_path))
    {
        if (!sdcard_probe_present())
        {
            return false;
        }
        sdcard_set_error_P(PSTR("RENAME FAIL"));
        return false;
    }

    sdcard_set_error_P(PSTR("OK"));
    return true;
}

uint16_t sdcard_count_root_entries(uint16_t max_entries)
{
    const char root_path[2] = { '/', '\0' };
    return sdcard_count_entries(root_path, max_entries);
}

bool sdcard_read_root_entry_by_index(uint16_t index, sdcard_entry_t *entry)
{
    const char root_path[2] = { '/', '\0' };
    return sdcard_read_entry_by_index(root_path, index, entry);
}

const char *sdcard_last_error(void)
{
    return sdcard_error;
}

uint8_t sdcard_last_error_code(void)
{
    return sdcard_error_code;
}

uint8_t sdcard_last_error_data(void)
{
    return sdcard_error_data;
}
