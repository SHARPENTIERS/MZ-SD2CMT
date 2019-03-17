#pragma once

#include <SdFat.h> // Sketch > Include library > Manage libraries > search: SdFat, by Bill Greiman

constexpr auto SD0_MO = 50; // SD MOSI
constexpr auto SD0_MI = 51; // SD MISO
constexpr auto SD0_CK = 52; // SD SCK
constexpr auto SD0_SS = 53; // SS SS

constexpr auto DIR_DEPTH = 8;
constexpr auto SFN_DEPTH = 13;
constexpr auto LFN_DEPTH = 100;

#define ENTRY_UNK       0
#define ENTRY_DIR       1
#define ENTRY_LEP       2 // WAV-like file with 50µs/16us resolution where a byte encodes an edge change after a period
#define ENTRY_WAV       3 // WAV file
#define ENTRY_MZF       4 // well-known binary format which includes a 128-byte header block and a data block (MZF/M12).
#define ENTRY_MZT       5 // may contain more than one 128-byte header and one data block.

#define SPEED_NORMAL    0 // SHARP PWM system (MZ-700/800) 
#define SPEED_ULTRAFAST 1

bool					sd_ready = false;
SdFat					sd;
SdFile					entry;
int16_t					entry_index = 0;
bool					entry_exists = false;
char					entry_type = ENTRY_UNK;
int8_t					dir_depth = -1;
int16_t					dir_index[DIR_DEPTH] = {};
SdFile					dir[DIR_DEPTH];
char					sfn[SFN_DEPTH];
char					lfn[LFN_DEPTH + 1];

struct Storage
{
	static void setup()
	{
		pinMode(SD0_SS, OUTPUT);

		sd_ready = sd.begin(SD0_SS, SPI_FULL_SPEED);

		if (!sd_ready) // accès au SD en full-speed
		{
			sd.initErrorHalt();
		}
	}
};