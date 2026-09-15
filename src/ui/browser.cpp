#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "browser.h"
#include "../drivers/lcd.h"
#include "../drivers/flash_text.h"
#include "../drivers/sdcard.h"
#include "../formats/file_format.h"
#include "../formats/mzi_sidecar.h"
#include "../record/record_path_buffer.h"

#define MAX_DIR_ENTRIES 999
#define BROWSER_PATH_MAX 96
#define BROWSER_NAME_MAX SDCARD_ENTRY_NAME_MAX
#define MESSAGE_HOLD_MS 1200
#define LCD_COLUMNS 16U
#define NAME_SCROLL_START_MS 900U
#define NAME_SCROLL_STEP_MS 260U
#define NAME_SCROLL_END_PAUSE_MS 900U
#define BROWSER_WRAP_REPEAT_PAUSE_MS 650U
#define BROWSER_ROOT_RETRY_DELAY_MS 80U
#define BROWSER_ROOT_RETRY_COUNT 3U

static bool sd_ok = false;
static uint16_t dir_count = 0;
/* Index in alphabetical browser order, not physical FAT directory order. */
static uint16_t selected_index = 0;
static char current_path[BROWSER_PATH_MAX] = { '/', '\0' };
static sdcard_entry_t current_entry;
static bool saved_position_valid = false;
static char saved_name[BROWSER_NAME_MAX];
static bool saved_is_dir = false;
static char status_message[17];
static uint16_t status_message_until_ms = 0U;
static bool right_locked_until_release = false;
static bool current_info_sidecar_checked = false;
static bool current_info_sidecar = false;

/* Root bootstrap retries are service-driven so keypad, card detection and
   UI processing remain responsive while retry timing is active. */
static bool root_retry_pending = false;
static uint8_t root_retry_attempt = 0U;
static uint16_t root_retry_at_ms = 0U;

/* Horizontal positions on the 16-character LCD. */
static uint8_t name_scroll_offset = 0U;
static uint16_t name_scroll_next_ms = 0U;
/* The active directory gets its own scroll state; it must not jump when the
   selected file on the second line changes. */
static uint8_t path_scroll_offset = 0U;
static uint16_t path_scroll_next_ms = 0U;
static int8_t wrap_pause_direction = 0;
static uint16_t wrap_pause_until_ms = 0U;

static void browser_reset_name_scroll(void);
static void browser_reset_path_scroll(void);
static void browser_reset_wrap_pause(void);

static void lcd_print_line(uint8_t row, const char *text)
{
    char line[17];
    uint8_t length = 0U;
    if (text != NULL)
    {
        while ((length < 16U) && (text[length] != '\0'))
        {
            line[length] = text[length];
            ++length;
        }
    }
    while (length < 16U) line[length++] = ' ';
    line[16] = '\0';
    lcd_set_cursor(0U, row);
    lcd_print(line);
}

static void set_status_message_P(PGM_P message)
{
    flash_text_copy(status_message, sizeof(status_message), message);
    for (uint8_t i = 0U; i < 16U; ++i)
    {
        if (status_message[i] == '\0')
        {
            for (; i < 16U; ++i) status_message[i] = ' ';
            break;
        }
    }
    status_message[16] = '\0';
    status_message_until_ms = (uint16_t)((uint16_t)millis() + MESSAGE_HOLD_MS);
}

static void browser_set_current_entry_message_P(PGM_P message)
{
    memset(&current_entry, 0, sizeof(current_entry));
    flash_text_copy(current_entry.name, sizeof(current_entry.name), message);
    current_entry.is_dir = false;
    current_entry.size = 0U;
    browser_reset_name_scroll();
}

static bool browser_is_root(void)
{
    return (current_path[0] == '/') && (current_path[1] == '\0');
}

static void browser_reset_name_scroll(void)
{
    name_scroll_offset = 0U;
    name_scroll_next_ms = (uint16_t)((uint16_t)millis() + NAME_SCROLL_START_MS);
    current_info_sidecar_checked = false;
    current_info_sidecar = false;
}

static void browser_reset_path_scroll(void)
{
    path_scroll_offset = 0U;
    path_scroll_next_ms = (uint16_t)((uint16_t)millis() + NAME_SCROLL_START_MS);
}

static void browser_reset_wrap_pause(void)
{
    wrap_pause_direction = 0;
    wrap_pause_until_ms = 0U;
}

static bool browser_wrap_pause_elapsed(int8_t direction)
{
    uint16_t now = (uint16_t)millis();

    if (wrap_pause_direction != direction)
    {
        wrap_pause_direction = direction;
        wrap_pause_until_ms = (uint16_t)(now + BROWSER_WRAP_REPEAT_PAUSE_MS);
        return false;
    }

    if ((int16_t)(now - wrap_pause_until_ms) < 0)
    {
        return false;
    }

    browser_reset_wrap_pause();
    return true;
}

static void browser_set_current_entry_message(const char *message)
{
    memset(&current_entry, 0, sizeof(current_entry));
    strncpy(current_entry.name, message, sizeof(current_entry.name) - 1U);
    current_entry.name[sizeof(current_entry.name) - 1U] = '\0';
    current_entry.is_dir = false;
    current_entry.size = 0;
    browser_reset_name_scroll();
}

typedef enum
{
    BROWSER_DIRECTORY_LOADED = 0,
    BROWSER_DIRECTORY_EMPTY,
    BROWSER_DIRECTORY_READ_FAILED
} browser_directory_load_result_t;

static void browser_set_entry_read_error(void)
{
    const char *error_message;

    sd_ok = sdcard_is_mounted();
    if (!sd_ok)
    {
        dir_count = 0U;
        browser_set_current_entry_message_P(sdcard_detect_removed_edge() ?
                                            PSTR("INSERT CARD") : PSTR("SD CARD ERROR"));
        return;
    }

    error_message = sdcard_last_error();
    if ((error_message == NULL) || (error_message[0] == '\0') ||
        (strcmp_P(error_message, PSTR("OK")) == 0) || (strcmp_P(error_message, PSTR("NO ENTRY")) == 0))
    {
        browser_set_current_entry_message_P(PSTR("DIR FAIL"));
        return;
    }

    browser_set_current_entry_message(error_message);
}

/*
    Root and normal directory refreshes use one physical scan that returns both
    the count and the first sorted item.  Do not count and immediately reopen
    the same directory after a card initialization: some cards intermittently
    return an empty second scan even though the first scan found entries.
*/
static browser_directory_load_result_t browser_scan_current_directory(void)
{
    sdcard_entry_t first_entry;
    uint16_t scanned_count = 0U;

    if (!sd_ok)
    {
        return BROWSER_DIRECTORY_READ_FAILED;
    }

    memset(&first_entry, 0, sizeof(first_entry));
    if (!sdcard_scan_directory_first_sorted(current_path, MAX_DIR_ENTRIES,
                                            &scanned_count, &first_entry))
    {
        sd_ok = sdcard_is_mounted();
        if (!sd_ok)
        {
            dir_count = 0U;
        }
        return BROWSER_DIRECTORY_READ_FAILED;
    }

    dir_count = scanned_count;
    selected_index = 0U;

    if (dir_count == 0U)
    {
        browser_set_current_entry_message_P(PSTR("EMPTY"));
        return BROWSER_DIRECTORY_EMPTY;
    }

    current_entry = first_entry;
    browser_reset_name_scroll();
    browser_reset_wrap_pause();
    return BROWSER_DIRECTORY_LOADED;
}

static bool browser_load_first_entry(void)
{
    browser_directory_load_result_t result = browser_scan_current_directory();

    if (result == BROWSER_DIRECTORY_READ_FAILED)
    {
        browser_set_entry_read_error();
        return false;
    }

    return result == BROWSER_DIRECTORY_LOADED;
}

static bool browser_load_last_entry(void)
{
    if (!sd_ok || (dir_count == 0U))
    {
        return browser_load_first_entry();
    }
    if (!sdcard_read_last_sorted_entry(current_path, dir_count, &current_entry))
    {
        browser_set_entry_read_error();
        return false;
    }
    selected_index = (uint16_t)(dir_count - 1U);
    browser_reset_name_scroll();
    browser_reset_wrap_pause();
    return true;
}

static void browser_build_dir_index(void)
{
    (void)browser_load_first_entry();
}

static void browser_reset_root_state(void)
{
    current_path[0] = '/';
    current_path[1] = '\0';
    dir_count = 0U;
    selected_index = 0U;
    memset(&current_entry, 0, sizeof(current_entry));
    saved_position_valid = false;
    saved_name[0] = '\0';
    saved_is_dir = false;
    browser_reset_name_scroll();
    browser_reset_path_scroll();
    browser_reset_wrap_pause();
}

/*
    A successful card initialization always starts a new browser session at
    root. The function confirms both an empty and a failed first root scan with
    a complete SPI/SdFat retry; it never keeps a stale subdirectory or exposes
    a transient empty scan as a browser item.
*/
static void browser_try_root_after_sd_init(void)
{
    browser_directory_load_result_t result;

    if (!sd_ok)
    {
        root_retry_pending = false;
        browser_set_entry_read_error();
        return;
    }

    result = browser_scan_current_directory();
    root_retry_attempt++;

    if (result == BROWSER_DIRECTORY_LOADED)
    {
        root_retry_pending = false;
        return;
    }

    /* Accept a genuinely empty root only after all bootstrap attempts. */
    if ((result == BROWSER_DIRECTORY_EMPTY) &&
        (root_retry_attempt >= BROWSER_ROOT_RETRY_COUNT))
    {
        root_retry_pending = false;
        return;
    }

    if ((result == BROWSER_DIRECTORY_READ_FAILED) &&
        (root_retry_attempt >= BROWSER_ROOT_RETRY_COUNT))
    {
        root_retry_pending = false;
        browser_set_entry_read_error();
        return;
    }

    root_retry_pending = true;
    root_retry_at_ms =
        (uint16_t)((uint16_t)millis() + BROWSER_ROOT_RETRY_DELAY_MS);
}

static void browser_open_root_after_sd_init(void)
{
    root_retry_pending = false;
    root_retry_attempt = 0U;
    browser_reset_root_state();
    browser_try_root_after_sd_init();
}

static void browser_service_root_retry(void)
{
    if (!root_retry_pending)
    {
        return;
    }

    if ((int16_t)((uint16_t)millis() - root_retry_at_ms) < 0)
    {
        return;
    }

    root_retry_pending = false;
    sd_ok = sdcard_reinitialize();
    if (!sd_ok)
    {
        browser_set_entry_read_error();
        return;
    }

    browser_reset_root_state();
    browser_try_root_after_sd_init();
}

static bool browser_restore_entry_by_identity(const char *name, bool is_dir)
{
    sdcard_entry_t entry;
    uint16_t resolved_index = 0U;

    if ((name == NULL) || !sd_ok || (dir_count == 0U))
    {
        return false;
    }
    memset(&entry, 0, sizeof(entry));
    if (!sdcard_find_sorted_entry_by_identity(current_path, dir_count, name, is_dir,
                                              &resolved_index, &entry))
    {
        browser_set_entry_read_error();
        return false;
    }

    selected_index = resolved_index;
    current_entry = entry;
    browser_reset_name_scroll();
    return true;
}

static bool browser_find_saved_position(void)
{
    if (!saved_position_valid)
    {
        return false;
    }
    return browser_restore_entry_by_identity(saved_name, saved_is_dir);
}

static void browser_refresh_sd(void)
{
    set_status_message_P(PSTR("SD INIT"));

    /* A card retry deliberately discards the former path and selection.  The
       media may have been replaced, so root and its first alphabetical item
       are the only deterministic restart point. */
    sd_ok = sdcard_reinitialize();
    browser_open_root_after_sd_init();
}

static void browser_go_root(void)
{
    browser_reset_root_state();
    browser_build_dir_index();
}

static void browser_go_parent(void)
{
    char leaving_name[BROWSER_NAME_MAX];
    if (browser_is_root())
    {
        return;
    }
    char *last_slash = strrchr(current_path, '/');
    if (last_slash == NULL)
    {
        browser_go_root();
        return;
    }

    strncpy(leaving_name, last_slash + 1, sizeof(leaving_name) - 1U);
    leaving_name[sizeof(leaving_name) - 1U] = '\0';

    if (last_slash == current_path)
    {
        current_path[1] = '\0';
    }
    else
    {
        *last_slash = '\0';
    }
    selected_index = 0U;
    saved_position_valid = false;
    browser_reset_path_scroll();
    browser_build_dir_index();
    if (leaving_name[0] != '\0')
    {
        (void)browser_restore_entry_by_identity(leaving_name, true);
    }
}

static bool browser_enter_directory(const char *name)
{
    if ((name == NULL) || (strlen(name) == 0U))
    {
        return false;
    }

    char new_path[BROWSER_PATH_MAX];
    size_t name_length = strlen(name);
    size_t path_length = browser_is_root() ? 1U : strlen(current_path);
    size_t required_length = path_length + name_length;

    if (!browser_is_root())
    {
        required_length++;
    }
    if (required_length >= sizeof(new_path))
    {
        set_status_message_P(PSTR("PATH TOO LONG"));
        return false;
    }

    if (browser_is_root())
    {
        new_path[0] = '/';
        memcpy(&new_path[1], name, name_length + 1U);
    }
    else
    {
        memcpy(new_path, current_path, path_length);
        new_path[path_length] = '/';
        memcpy(&new_path[path_length + 1U], name, name_length + 1U);
    }

    strncpy(current_path, new_path, sizeof(current_path) - 1U);
    current_path[sizeof(current_path) - 1U] = '\0';
    selected_index = 0U;
    saved_position_valid = false;
    browser_reset_path_scroll();
    browser_build_dir_index();
    return true;
}

static bool browser_load_sorted_neighbor(bool previous)
{
    sdcard_entry_t neighbor;

    if (!sdcard_read_sorted_neighbor(current_path, dir_count, &current_entry, previous, &neighbor))
    {
        return false;
    }

    current_entry = neighbor;
    if (previous)
    {
        selected_index--;
    }
    else
    {
        selected_index++;
    }
    browser_reset_name_scroll();
    browser_reset_wrap_pause();
    return true;
}

static void browser_move_up(bool repeated)
{
    if (!sd_ok || (dir_count == 0U))
    {
        browser_reset_wrap_pause();
        return;
    }
    if (selected_index == 0U)
    {
        if (repeated && (dir_count > 1U) && !browser_wrap_pause_elapsed(-1))
        {
            return;
        }
        browser_load_last_entry();
        return;
    }
    browser_reset_wrap_pause();
    if (!browser_load_sorted_neighbor(true))
    {
        /* A no-neighbour result is a normal end-of-sort condition. Wrap
           without exposing the internal "NO ENTRY" status. */
        if (sdcard_is_mounted() && (strcmp_P(sdcard_last_error(), PSTR("NO ENTRY")) == 0))
        {
            browser_load_last_entry();
        }
        else
        {
            browser_set_entry_read_error();
        }
    }
}

static void browser_move_down(bool repeated)
{
    if (!sd_ok || (dir_count == 0U))
    {
        browser_reset_wrap_pause();
        return;
    }
    if ((uint16_t)(selected_index + 1U) >= dir_count)
    {
        if (repeated && (dir_count > 1U) && !browser_wrap_pause_elapsed(1))
        {
            return;
        }
        browser_load_first_entry();
        return;
    }
    browser_reset_wrap_pause();
    if (!browser_load_sorted_neighbor(false))
    {
        /* See browser_move_up(): keep a stale count or a tie at the end of
           the sort from producing a visible "NO ENTRY" line. */
        if (sdcard_is_mounted() && (strcmp_P(sdcard_last_error(), PSTR("NO ENTRY")) == 0))
        {
            browser_load_first_entry();
        }
        else
        {
            browser_set_entry_read_error();
        }
    }
}

void browser_clear_status(void)
{
    status_message[0] = '\0';
    status_message_until_ms = 0U;
}

static browser_action_t browser_select_current(void)
{
    if (!sd_ok)
    {
        browser_refresh_sd();
        return BROWSER_ACTION_NONE;
    }
    if (dir_count == 0U)
    {
        set_status_message_P(PSTR("EMPTY DIR"));
        return BROWSER_ACTION_NONE;
    }
    if (current_entry.name_too_long)
    {
        set_status_message_P(PSTR("NAME TOO LONG"));
        return BROWSER_ACTION_NONE;
    }
    if (current_entry.is_dir)
    {
        browser_enter_directory(current_entry.name);
        return BROWSER_ACTION_NONE;
    }
    set_status_message_P(PSTR("FILE SELECTED"));
    return BROWSER_ACTION_FILE_SELECTED;
}

static browser_action_t browser_request_record(void)
{
    /*
        RIGHT has exactly two states:
        - SD CARD ERROR: retry/reinitialize only. A successful retry never
          starts recording on the same press.
        - SD OK: verify the mounted card once and start RECORD.
    */
    if (!sd_ok)
    {
        right_locked_until_release = true;
        browser_refresh_sd();
        return BROWSER_ACTION_NONE;
    }

    /* A card may have been removed after the browser was built. */
    if (!sdcard_init())
    {
        sd_ok = false;
        browser_build_dir_index();
        return BROWSER_ACTION_NONE;
    }

    sd_ok = true;
    return BROWSER_ACTION_RECORD_REQUESTED;
}

void browser_init(bool initial_sd_ok)
{
    sd_ok = initial_sd_ok;
    status_message[0] = '\0';
    status_message_until_ms = 0U;
    right_locked_until_release = false;
    browser_reset_root_state();

    /* setup() already completed the initial SD initialization.  Treat it like
       every later retry: always start at root and select its first item. */
    browser_open_root_after_sd_init();
}

browser_action_t browser_handle_event(button_event_t event)
{
    switch (event)
    {
        case BUTTON_EVENT_UP_PRESS:
            browser_move_up(false);
            return BROWSER_ACTION_NONE;
        case BUTTON_EVENT_UP_REPEAT:
            browser_move_up(true);
            return BROWSER_ACTION_NONE;
        case BUTTON_EVENT_DOWN_PRESS:
            browser_move_down(false);
            return BROWSER_ACTION_NONE;
        case BUTTON_EVENT_DOWN_REPEAT:
            browser_move_down(true);
            return BROWSER_ACTION_NONE;
        case BUTTON_EVENT_LEFT_SHORT:
            browser_go_parent();
            return BROWSER_ACTION_NONE;
        case BUTTON_EVENT_LEFT_LONG:
            if (browser_is_root()) return BROWSER_ACTION_SETTINGS_REQUESTED;
            browser_go_root();
            return BROWSER_ACTION_NONE;
        case BUTTON_EVENT_RIGHT_SHORT:
            if (right_locked_until_release) return BROWSER_ACTION_NONE;
            return browser_request_record();
        case BUTTON_EVENT_RIGHT_LONG:
            if (right_locked_until_release) return BROWSER_ACTION_NONE;
            if (!sd_ok)
            {
                right_locked_until_release = true;
                browser_refresh_sd();
                return BROWSER_ACTION_NONE;
            }
            if (!sdcard_init())
            {
                sd_ok = false;
                browser_build_dir_index();
                return BROWSER_ACTION_NONE;
            }
            return BROWSER_ACTION_RECORD_MENU_REQUESTED;
        case BUTTON_EVENT_SELECT_SHORT:
            return browser_select_current();
        case BUTTON_EVENT_SELECT_LONG:
            return BROWSER_ACTION_PLAY_MENU_REQUESTED;
        default:
            break;
    }
    return BROWSER_ACTION_NONE;
}

static uint8_t browser_unsigned_length(uint16_t value)
{
    uint8_t length = 1U;

    while (value >= 10U)
    {
        value = (uint16_t)(value / 10U);
        length++;
    }
    return length;
}

static uint8_t browser_position_counter_length(void)
{
    uint16_t display_index = (dir_count == 0U) ? 0U :
        (uint16_t)(selected_index + 1U);
    uint16_t display_count = dir_count;

    if (display_index > MAX_DIR_ENTRIES) display_index = MAX_DIR_ENTRIES;
    if (display_count > MAX_DIR_ENTRIES) display_count = MAX_DIR_ENTRIES;

    return (uint8_t)(browser_unsigned_length(display_index) + 1U +
                     browser_unsigned_length(display_count));
}

static const char *browser_current_directory_name(void)
{
    const char *last_slash;

    if (browser_is_root())
    {
        return NULL;
    }

    last_slash = strrchr(current_path, '/');
    if ((last_slash == NULL) || (*(last_slash + 1U) == '\0'))
    {
        return NULL;
    }
    return last_slash + 1U;
}

static uint8_t browser_path_name_columns(void)
{
    uint8_t counter_length = browser_position_counter_length();
    uint8_t counter_column = (uint8_t)(LCD_COLUMNS - counter_length);

    /* Non-root line 0 uses "../" plus one dedicated blank before N/N. */
    return (counter_column > 4U) ? (uint8_t)(counter_column - 4U) : 0U;
}

static bool browser_current_has_info_sidecar(void)
{
    file_format_t format;
    size_t path_length;
    size_t name_length;
    size_t output_offset;
    bool add_separator;

    if (current_info_sidecar_checked) return current_info_sidecar;
    current_info_sidecar_checked = true;
    current_info_sidecar = false;

    if (!sd_ok || (dir_count == 0U) || current_entry.is_dir ||
        current_entry.name_too_long || (current_entry.name[0] == '\0'))
    {
        return false;
    }

    format = file_format_detect_from_name(current_entry.name);
    if ((format != FILE_FORMAT_MZF) && (format != FILE_FORMAT_MZT))
    {
        return false;
    }

    path_length = strlen(current_path);
    name_length = strlen(current_entry.name);
    if (path_length == 0U) return false;
    add_separator = current_path[path_length - 1U] != '/';
    if ((path_length + (add_separator ? 1U : 0U) + name_length) >=
        CMT_SESSION_PATH_BUFFER_MAX)
    {
        return false;
    }

    memcpy(cmt_session_path_buffer, current_path, path_length);
    output_offset = path_length;
    if (add_separator) cmt_session_path_buffer[output_offset++] = '/';
    memcpy(cmt_session_path_buffer + output_offset, current_entry.name,
           name_length + 1U);

    current_info_sidecar = (format == FILE_FORMAT_MZF) ?
        mzi_sidecar_exists_for_mzf(cmt_session_path_buffer) :
        mzi_sidecar_exists_for_mzt(cmt_session_path_buffer);
    return current_info_sidecar;
}

static void browser_advance_scroll(const char *text, uint8_t visible_columns,
                                   uint8_t *offset, uint16_t *next_ms)
{
    uint16_t now;
    size_t length;
    uint8_t maximum_offset;

    if ((text == NULL) || (visible_columns == 0U))
    {
        *offset = 0U;
        return;
    }

    length = strlen(text);
    if (length <= visible_columns)
    {
        *offset = 0U;
        return;
    }

    now = (uint16_t)millis();
    if ((int16_t)(now - *next_ms) < 0)
    {
        return;
    }

    maximum_offset = (uint8_t)(length - visible_columns);
    if (*offset < maximum_offset)
    {
        (*offset)++;
        if (*offset >= maximum_offset)
        {
            *next_ms = (uint16_t)(now + NAME_SCROLL_END_PAUSE_MS);
        }
        else
        {
            *next_ms = (uint16_t)(now + NAME_SCROLL_STEP_MS);
        }
    }
    else
    {
        *offset = 0U;
        *next_ms = (uint16_t)(now + NAME_SCROLL_START_MS);
    }
}

void browser_service(void)
{
    const char *directory_name;

    if (right_locked_until_release && (keypad_get_button() != BUTTON_RIGHT))
    {
        right_locked_until_release = false;
    }

    (void)sdcard_detect_poll();
    if (sdcard_detect_removed_edge())
    {
        sd_ok = false;
        root_retry_pending = false;
        dir_count = 0U;
        selected_index = 0U;
        status_message[0] = '\0';
        status_message_until_ms = 0U;
        if (strcmp_P(current_entry.name, PSTR("INSERT CARD")) != 0)
        {
            browser_set_current_entry_message_P(PSTR("INSERT CARD"));
        }
    }
    else if (sdcard_detect_consume_inserted_edge())
    {
        browser_refresh_sd();
    }

    browser_service_root_retry();

    browser_advance_scroll(current_entry.name, LCD_COLUMNS, &name_scroll_offset,
                           &name_scroll_next_ms);

    directory_name = browser_current_directory_name();
    browser_advance_scroll(directory_name, browser_path_name_columns(),
                           &path_scroll_offset, &path_scroll_next_ms);
}

static void browser_format_entry_line(char *line, const sdcard_entry_t *entry)
{
    size_t name_length;
    uint8_t offset;
    uint8_t column;

    memset(line, ' ', LCD_COLUMNS);
    line[LCD_COLUMNS] = '\0';

    if ((entry == NULL) || (entry->name[0] == '\0'))
    {
        return;
    }

    name_length = strlen(entry->name);
    offset = name_scroll_offset;
    if (name_length <= LCD_COLUMNS)
    {
        offset = 0U;
    }
    else if (offset > (uint8_t)(name_length - LCD_COLUMNS))
    {
        offset = 0U;
    }

    for (column = 0U; column < LCD_COLUMNS; column++)
    {
        size_t source_index = (size_t)offset + (size_t)column;
        if (source_index >= name_length)
        {
            break;
        }
        line[column] = entry->name[source_index];
    }
}

static uint8_t browser_write_unsigned(char *line, uint8_t column, uint16_t value)
{
    char digits[5];
    uint8_t digit_count = 0U;

    do
    {
        digits[digit_count++] = (char)('0' + (value % 10U));
        value = (uint16_t)(value / 10U);
    }
    while ((value != 0U) && (digit_count < sizeof(digits)));

    while (digit_count > 0U)
    {
        line[column++] = digits[--digit_count];
    }
    return column;
}

static void browser_format_position_line(char *line)
{
    char counter[8];
    uint8_t counter_length;
    uint8_t counter_column;
    uint8_t path_column = 0U;
    uint16_t display_index = (dir_count == 0U) ? 0U : (uint16_t)(selected_index + 1U);
    uint16_t display_count = dir_count;

    if (display_index > MAX_DIR_ENTRIES)
    {
        display_index = MAX_DIR_ENTRIES;
    }
    if (display_count > MAX_DIR_ENTRIES)
    {
        display_count = MAX_DIR_ENTRIES;
    }

    memset(line, ' ', LCD_COLUMNS);
    line[LCD_COLUMNS] = '\0';

    counter_length = browser_write_unsigned(counter, 0U, display_index);
    counter[counter_length++] = browser_current_has_info_sidecar() ? 'I' : '/';
    counter_length = browser_write_unsigned(counter, counter_length, display_count);
    counter[counter_length] = '\0';
    counter_column = (uint8_t)(LCD_COLUMNS - counter_length);

    if (browser_is_root())
    {
        line[0] = '/';
    }
    else
    {
        const char *directory_name = browser_current_directory_name();
        size_t name_length = (directory_name == NULL) ? 0U : strlen(directory_name);
        uint8_t visible_columns = browser_path_name_columns();
        uint8_t offset = path_scroll_offset;

        /* "../" says that LEFT returns to a parent directory.  Keep the
           column immediately before N/N blank so the directory name never
           visually joins the position counter. */
        if (counter_column >= 4U)
        {
            memcpy_P(line, PSTR("../"), 3U);
            path_column = 3U;
        }

        if ((name_length <= visible_columns) ||
            (offset > (uint8_t)(name_length - visible_columns)))
        {
            offset = 0U;
        }

        while ((directory_name != NULL) &&
               (path_column < (uint8_t)(counter_column - 1U)) &&
               ((size_t)offset < name_length))
        {
            line[path_column++] = directory_name[offset++];
        }
    }

    memcpy(&line[counter_column], counter, counter_length);
}

void browser_render(void)
{
    char line0[17];
    char line1[17];
    uint16_t now = (uint16_t)millis();

    if ((status_message[0] != '\0') &&
        ((int16_t)(now - status_message_until_ms) < 0))
    {
        lcd_set_cursor(0, 0);
        lcd_print(status_message);
        if (sd_ok)
        {
            lcd_set_cursor(0, 1);
            lcd_print_P(PSTR("                "));
        }
        else
        {
            lcd_set_cursor(0, 1);
            lcd_print_P(PSTR("RECORD=RETRY    "));
        }
        return;
    }

    status_message[0] = '\0';

    if (!sd_ok)
    {
        if (sdcard_detect_removed_edge())
        {
            flash_text_copy(line0, sizeof(line0), PSTR("INSERT CARD"));
            memset(line1, ' ', LCD_COLUMNS);
            line1[LCD_COLUMNS] = '\0';
        }
        else
        {
            flash_text_copy(line0, sizeof(line0), PSTR("SD CARD ERROR"));
            flash_text_copy(line1, sizeof(line1), PSTR("RECORD=RETRY"));
        }
    }
    else if (dir_count == 0U)
    {
        browser_format_position_line(line0);
        memset(line1, ' ', LCD_COLUMNS);
        memcpy_P(line1, PSTR("EMPTY"), 5U);
        line1[LCD_COLUMNS] = '\0';
    }
    else
    {
        browser_format_position_line(line0);
        browser_format_entry_line(line1, &current_entry);
    }

    /* Always overwrite all 16 cells. Short error/status strings must clear
       any path and N/N counter left in the row. */
    lcd_print_line(0U, line0);
    lcd_print_line(1U, line1);
}

const char* browser_get_selected_name(void)
{
    return current_entry.name;
}

bool browser_selected_is_directory(void)
{
    return current_entry.is_dir;
}

void browser_save_position(void)
{
    strncpy(saved_name, current_entry.name, sizeof(saved_name) - 1U);
    saved_name[sizeof(saved_name) - 1U] = '\0';
    saved_is_dir = current_entry.is_dir;
    saved_position_valid = true;
}

void browser_restore_saved_position(void)
{
    if (!saved_position_valid)
    {
        return;
    }
    if (!browser_find_saved_position())
    {
        browser_load_first_entry();
    }
}

const char* browser_get_current_path(void)
{
    return current_path;
}

void browser_refresh(void)
{
    /* The file was just saved/deleted on an already mounted card.
       Re-scan silently: do not show the SD INIT splash on return. */
    browser_build_dir_index();
    browser_find_saved_position();
    lcd_clear();
}
