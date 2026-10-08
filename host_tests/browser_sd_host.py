"""Compile production browser/menu and record acknowledgement against SD mocks.
Run: python host_tests/browser_sd_host.py [path-to-g++]
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ARDUINO = '''#pragma once
#include <stdint.h>
#include <stddef.h>
inline uint32_t host_ms=0;
inline uint32_t millis() { return host_ms; }
'''
PGM = '''#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define PROGMEM
#define PSTR(x) (x)
typedef const char *PGM_P;
#define pgm_read_byte(x) (*(const uint8_t *)(x))
#define pgm_read_word(x) (*(const uintptr_t *)(x))
#define strncpy_P strncpy
#define strlen_P strlen
#define strcmp_P strcmp
#define memcpy_P memcpy
#define vsnprintf_P vsnprintf
'''

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

HARNESS = r'''
#include <cassert>
#include <string>
#include <iostream>
#include "src/ui/browser.cpp"
#include "src/ui/record_menu.cpp"
#include "src/formats/file_format.cpp"
#include "src/record/record_engine.h"
#include "src/ui/record_screen.h"
char cmt_session_path_buffer[CMT_SESSION_PATH_BUFFER_MAX];
static bool mounted=true, scan_fail=false, identity_fail=false;
static bool inserted=false, removed=false, init_success=true;
static unsigned scans=0, inits=0, reinits=0, identities=0, saves=0;
static uint16_t entries=3;
static const char *sd_error="OK";
static uint8_t row;
static std::string lines[2];
void lcd_set_cursor(uint8_t,uint8_t r) { row=r; }
void lcd_print(const char *s) { lines[row]=s; }
void lcd_print_P(PGM_P s) { lcd_print(s); }
void lcd_clear() {}
button_t keypad_get_button() { return BUTTON_NONE; }
bool sdcard_is_mounted() { return mounted; }
bool sdcard_detect_poll() { return false; }
bool sdcard_detect_removed_edge() { return removed; }
bool sdcard_detect_consume_inserted_edge() { bool r=inserted; inserted=false; return r; }
const char *sdcard_last_error() { return sd_error; }
bool sdcard_init() { ++inits; return mounted; }
bool sdcard_reinitialize() { ++reinits; mounted=init_success; scan_fail=false; removed=false; return mounted; }
static void entry(sdcard_entry_t *e, const char *s) { memset(e,0,sizeof(*e)); strcpy(e->name,s); }
bool sdcard_scan_directory_first_sorted(const char *,uint16_t,uint16_t *n,sdcard_entry_t *e) {
    ++scans; if(scan_fail) { sd_error="DIR FAIL"; return false; }
    *n=entries; entry(e,entries ? "FIRST.WAV" : ""); return true;
}
bool sdcard_find_sorted_entry_by_identity(const char *,uint16_t,const char *s,bool,uint16_t *i,sdcard_entry_t *e) {
    ++identities; if(identity_fail) return false;
    *i=1; entry(e,s); return true;
}
bool sdcard_read_last_sorted_entry(const char *,uint16_t,sdcard_entry_t *e) { entry(e,"LAST.WAV"); return true; }
bool sdcard_read_sorted_neighbor(const char *,uint16_t,const sdcard_entry_t *,bool,sdcard_entry_t *e) { entry(e,"SECOND.WAV"); return true; }
bool mzi_sidecar_exists_for_tape(const char *,file_format_t) { return false; }
bool calibration_store_load_record_settings(record_settings_data_t *) { return false; }
bool calibration_store_save_record_settings(const record_settings_data_t *) { ++saves; return true; }
static record_engine_state_t rec_state=RECORD_ENGINE_ERROR;
static const char *rec_error="MKDIR FAIL";
static record_screen_action_t rec_action=RECORD_SCREEN_ACTION_STOP_SAVE;
static unsigned returns=0,cancels=0;
record_screen_action_t record_screen_handle_event(button_event_t) { return rec_action; }
record_engine_state_t record_engine_get_state() { return rec_state; }
const char *record_engine_get_error_text() { return rec_error; }
void record_engine_toggle_pause() {}
void record_engine_request_stop() {}
void record_engine_cancel() { ++cancels; }
void app_enter_browser() { browser_resume(); ++returns; }
// Production app handler inserted below, without copying its implementation.
@HANDLER@
int main() {
    browser_init(true); record_menu_init();
    strcpy(current_path,"/GAMES"); browser_handle_event(BUTTON_EVENT_DOWN_PRESS);
    const auto count=dir_count, index=selected_index;
    const std::string name=current_entry.name;
    const auto before_scans=scans, before_inits=inits, before_reinits=reinits;
    for(unsigned i=0;i<50;++i) {
        assert(browser_handle_event(BUTTON_EVENT_RIGHT_LONG)==BROWSER_ACTION_RECORD_MENU_REQUESTED);
        browser_save_position();
        if(i%2) record_menu_handle_event(BUTTON_EVENT_SELECT_SHORT);
        assert(record_menu_handle_event(BUTTON_EVENT_LEFT_SHORT)==RECORD_MENU_ACTION_BACK);
        assert(record_menu_save_if_dirty()); browser_resume(); browser_render();
        assert(dir_count==count && selected_index==index && current_entry.name==name);
        assert(std::string(current_path)=="/GAMES");
    }
    assert(saves>0 && scans==before_scans && inits==before_inits && reinits==before_reinits && identities==0);
    assert(browser_handle_event(BUTTON_EVENT_RIGHT_SHORT)==BROWSER_ACTION_RECORD_REQUESTED);
    assert(inits==before_inits+1);
    // Genuine refresh restores identity; a deleted item retains first selection.
    browser_refresh(); assert(identities==1);
    identity_fail=true; sd_error="NO ENTRY"; browser_refresh();
    assert(!directory_error && std::string(current_entry.name)=="FIRST.WAV");
    // Failed refresh must not be overwritten by another identity/first scan.
    scan_fail=true; const auto id=identities; browser_refresh(); browser_render();
    assert(directory_error && identities==id && lines[0].find("DIR FAIL")==0);
    assert(lines[1].find("RECORD=RETRY")==0);
    browser_handle_event(BUTTON_EVENT_LEFT_SHORT); assert(directory_error);
    const auto retry=reinits, probe=inits;
    assert(browser_handle_event(BUTTON_EVENT_RIGHT_SHORT)==BROWSER_ACTION_NONE);
    assert(reinits==retry+1 && inits==probe && !directory_error && browser_is_root());
    browser_service();
    // Empty subdirectory is valid, with no retry or EMPTY label.
    entries=0; strcpy(current_path,"/EMPTYDIR"); browser_refresh(); browser_render();
    assert(lines[0].find("0/0")!=std::string::npos && lines[1]==std::string(16,' '));
    assert(!directory_error);
    entries=3; identity_fail=false;
    // Hardware media changes are serviced during menus and UI resume.
    removed=true; browser_resume(); browser_render(); assert(lines[0].find("INSERT CARD")==0);
    removed=false; inserted=true; browser_resume(); assert(browser_is_root() && dir_count==3);
    // Both acknowledgement actions recover directory errors, even mounted/CID OK.
    for(auto action : {RECORD_SCREEN_ACTION_STOP_SAVE,RECORD_SCREEN_ACTION_CANCEL_BACK}) {
        for(auto error : {"MKDIR FAIL","DIR FAIL"}) {
            rec_action=action; rec_error=error; const auto n=reinits;
            app_handle_record_event(BUTTON_EVENT_LEFT_SHORT);
            assert(reinits==n+1 && browser_is_root());
        }
    }
    init_success=false; app_handle_record_event(BUTTON_EVENT_LEFT_SHORT); browser_render();
    assert(lines[0].find("SD CARD ERROR")==0 && lines[1].find("RECORD=RETRY")==0);
    init_success=true; browser_service(); browser_handle_event(BUTTON_EVENT_RIGHT_SHORT);
    assert(sd_ok && dir_count==3);
    // Non-filesystem errors keep ordinary refresh/cancel behavior.
    rec_error="BAD REC RATE"; rec_action=RECORD_SCREEN_ACTION_STOP_SAVE;
    const auto n=reinits, s=scans; app_handle_record_event(BUTTON_EVENT_LEFT_SHORT);
    assert(reinits==n && scans==s+1);
    rec_action=RECORD_SCREEN_ACTION_CANCEL_BACK; app_handle_record_event(BUTTON_EVENT_LEFT_LONG);
    assert(cancels==1 && reinits==n);
    std::cout << "PASS: 50 menu cycles, EEPROM independence, record validation, refresh, empty/error rendering, media edges, forced recovery and non-FAT errors\n";
}
'''
handler = function((ROOT / "src/main.cpp").read_text(), "static void app_handle_record_event(")
SD_HARNESS = r'''
#include <cassert>
#include <string>
#include <iostream>
#include <Arduino.h>
#include <avr/pgmspace.h>
#define SD_CHIP_SELECT_PIN 53
#define SD2CMT2_SD_SPI_MODE 1
#define SD2CMT2_SD_SPI_MHZ 8
#define SD_SCK_MHZ(x) (x)
static bool sdcard_mounted=true, alive=true, begin_ok=true;
static unsigned closed=0, ended=0, begun=0, clocks=0, mkdirs=0, opens=0;
static uint8_t sdcard_error_code=0, sdcard_error_data=0;
static std::string error;
struct FsFile {
    bool opened=true;
    uint8_t read_error=0;
    bool isOpen() { return opened; }
    uint8_t getError() { return read_error; }
    void close() { opened=false; ++closed; }
    bool open(const char *,int) { ++opens; opened=true; return true; }
    bool isDir() { return true; }
};
struct SdSpiConfig { SdSpiConfig(int,int,int) {} };
struct SD {
    void end() { ++ended; }
    bool begin(SdSpiConfig) { ++begun; return begin_ok; }
    bool mkdir(const char *,bool) { ++mkdirs; return true; }
} sd;
#define O_RDONLY 0
void sdcard_early_prepare_pins() {}
bool sdcard_probe_present() { return alive; }
void sdcard_set_ok() { error="OK"; }
void sdcard_detect_reset_to_current() {}
void sdcard_close_all_files() { ++closed; }
void sdcard_detect_clear_pending() {}
void sdcard_clear_soft_probe_failures() {}
void sdcard_set_error_P(PGM_P s) { error=s; }
void sdcard_send_idle_clocks() { ++clocks; }
void sdcard_set_card_error() { sdcard_mounted=false; error="SD CARD ERROR"; }
@FUNCTIONS@
int main() {
    // A responsive CID allows normal init to return early; forced init must not.
    assert(sdcard_initialize(false) && begun==0 && ended==0);
    assert(sdcard_initialize(true) && begun==1 && ended==1 && clocks==1 && sdcard_mounted);
    begin_ok=false;
    assert(!sdcard_initialize(true) && ended==2 && !sdcard_mounted);
    // Existing RECORDINGS opens without mkdir, independently of browser tests.
    assert(sdcard_ensure_directory("/RECORDINGS") && opens==1 && mkdirs==0);
    // Directory read error is rejected even when the card-level probe succeeds.
    FsFile dir; dir.read_error=1;
    assert(!sdcard_finish_browser_directory_read(&dir) && error=="DIR FAIL" && !dir.opened);
    FsFile empty;
    assert(sdcard_finish_browser_directory_read(&empty));
    std::cout << "PASS: forced end/begin with responsive card, failed begin, existing directory fast path, FAT read error vs EOF\n";
}
'''
sd_source = (ROOT / "src/drivers/sdcard.cpp").read_text()
sd_functions = '\n'.join(function(sd_source, signature) for signature in (
    "static bool sdcard_initialize(",
    "static bool sdcard_finish_browser_directory_read(",
    "bool sdcard_ensure_directory("))
with tempfile.TemporaryDirectory(prefix="sd2cmt2-browser-") as scratch:
    tmp = Path(scratch)
    (tmp / "avr").mkdir()
    (tmp / "Arduino.h").write_text(ARDUINO)
    (tmp / "avr/pgmspace.h").write_text(PGM)
    source = tmp / "browser.cpp"
    source.write_text(HARNESS.replace("@HANDLER@", handler))
    exe = tmp / "browser.exe"
    subprocess.run([sys.argv[1] if len(sys.argv)>1 else "g++", "-std=c++17", "-O1",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-I"+str(tmp), "-I"+str(ROOT), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    source.write_text(SD_HARNESS.replace("@FUNCTIONS@", sd_functions))
    subprocess.run([sys.argv[1] if len(sys.argv)>1 else "g++", "-std=c++17", "-O1",
                    "-I"+str(tmp), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
