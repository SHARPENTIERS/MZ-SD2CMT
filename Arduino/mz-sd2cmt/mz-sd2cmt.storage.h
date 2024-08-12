#pragma once

#include <SdFat.h>	// Sketch > Include library > Manage libraries > search: SdFat, by Bill Greiman https://github.com/greiman/SdFat

#define ENTRY_UNK       0
#define ENTRY_DIR       1
#define ENTRY_LEP       2 // WAV-like file with 50µs/16µs resolution where a byte encodes an edge change after a period
#define ENTRY_WAV       3 // WAV file
#define ENTRY_MZF       4 // well-known binary format which includes a 128-byte header block and a data block (MZF/M12).
#define ENTRY_MZT       5 // may contain more than one 128-byte header and one data block.

#define SPEED_NORMAL    0 // SHARP PWM system (MZ-700/800) 
#define SPEED_ULTRAFAST 1

#define L16_UNIT        16     // LEP resolution in 16µs unit
#define L50_UNIT        50     // LEP resolution in 50µs unit

struct Storage
{
	static constexpr auto	SD0_MO = 50; // SD MOSI
	static constexpr auto	SD0_MI = 51; // SD MISO
	static constexpr auto	SD0_CK = 52; // SD SCK
	static constexpr auto	SD0_SS = 53; // SD SS

	static constexpr auto	DIR_DEPTH = 8;
	static constexpr auto	SFN_DEPTH = 13;
	static constexpr auto	LFN_DEPTH = 100;

	class SdFile : public ::SdFile
	{
	public:
		// This method is present in ExFatFile and FatFile but not in FsFile. Why?
		bool isSystem() { return attrib() & FS_ATTRIB_SYSTEM; }
	};

	bool					sd_ready = false;
	SdFat					sd;
	SdFile					entry;
	int16_t					entry_index = 0;
	bool					entry_exists = false;
	char					entry_type = ENTRY_UNK;
	int8_t					dir_depth = -1;
	int16_t					dir_index[DIR_DEPTH] = {};
	SdFile					dir[DIR_DEPTH];
	char					lfn[LFN_DEPTH + 1];
	unsigned long			lep_unit = L16_UNIT; // 16µs by default
	char					double_buffer[256];
	byte					double_buffer_wpos = 0;
	byte					double_buffer_rpos = 0;

	void setup()
	{
		pinMode(SD0_SS, OUTPUT);

		int retries = 0;

		while (retries < 5)
		{
			sd_ready = sd.begin(SD0_SS, SPI_FULL_SPEED);

			if (sd_ready) break;

			++retries;
		}

		if (not sd_ready)
		{
			sd.initErrorHalt();
		}
	}

	template<typename Config>
	bool configure(Config &cfg, char *filename)
	{
		File file = sd.open(filename, O_RDWR | O_CREAT);
		bool ko = !file;
		if (!ko)
		{
			ko = file.read(&cfg, sizeof(cfg)) != sizeof(cfg);
			Serial.print(ko ? F("Invalid or no configuration file: ") : F("Loaded configuration file: "));
			Serial.println(filename);
		}
		if (ko)
		{
			sd.mkdir(F("/.config"));
			File root = sd.open(F("/.config"));
			if (root) root.attrib(FS_ATTRIB_HIDDEN);
			root.close();
			file.seek(0);
			ko = file.write((const uint8_t*)&cfg, sizeof(cfg)) != sizeof(cfg);
			Serial.print(ko ? F("Failed configuration file: ") : F("Created configuration file: "));
			Serial.println(filename);
		}

		file.close();

		return !ko and cfg.enabled;
	}

	bool checkForLEP(char *filename)
	{
		auto ext = strlwr(filename + (strlen(filename) - 4));

		if (!!strstr(ext, ".l50"))
		{
			Storage::lep_unit = L50_UNIT;

			return true;
		}

		Storage::lep_unit = L16_UNIT;

		return
			!!strstr(ext, ".l16") ||
			!!strstr(ext, ".lep");
	}

	bool checkForWAV(char *filename)
	{
		auto ext = strlwr(filename + (strlen(filename) - 4));

		return
			!!strstr(ext, ".wav");
	}

	bool checkForMZF(char *filename)
	{
		auto ext = strlwr(filename + (strlen(filename) - 4));

		return
			!!strstr(ext, ".mzf") ||
			!!strstr(ext, ".m12") ||
			!!strstr(ext, ".mzt");
	}

	/// This function fetches an entry in the current directory.
	/// @arg int16_t new_index index of a valid entry in the directory.
	void fetchEntry(int16_t new_index)
	{
		bool    found = true;
		int16_t index;

		entry.close();

		if (new_index < 0)
		{
			new_index = 0;
		}

		do
		{
			dir[dir_depth].rewind();
			index = 0;

			while (index <= new_index)
			{
				found = entry.openNext(&dir[dir_depth], O_READ);
				if (found)
				{
					if (!entry.isHidden() && !entry.isSystem())
					{
						if (index == new_index)
						{
							break;
						}
						++index;
					}
					entry.close();
				}
				else
				{
					break;
				}
			}

			if (!found)
			{
				new_index = entry_index;
			}
		} while (!found && index > 0);

		if (found)
		{
			entry.getName(lfn, LFN_DEPTH);

			/**/ if (entry.isDir())
			{
				entry_type = ENTRY_DIR;
			}
			else if (checkForLEP(lfn))
			{
				entry_type = ENTRY_LEP;
			}
			else if (checkForWAV(lfn))
			{
				entry_type = ENTRY_WAV;
			}
			else if (checkForMZF(lfn))
			{
				entry_type = ENTRY_MZF;
			}
			else
			{
				entry_type = ENTRY_UNK;
			}

			entry_exists = true;

			entry_index = new_index;
		}
		else
		{
			memset(lfn, 0, LFN_DEPTH + 1);
			strcpy(lfn, "<---------no file--------->");

			entry_exists = false;
		}
	}

	/// This function enters a directory.
	bool enterDir()
	{
		if (dir_depth < DIR_DEPTH - 2)
		{
			if (dir_depth < 0)
			{
				if (dir[0].openRoot(&sd))
				{
					++dir_depth;

					fetchEntry(0);

					return true;
				}
				else
				{
					// TODO
					Serial.println(F("Error: enterDir - cannot open root directory!"));
				}
			}
			else if (entry.isOpen())
			{
				++dir_depth;

				dir[dir_depth].move(&entry);
				dir_index[dir_depth] = entry_index;

				fetchEntry(0);

				return true;
			}
			else
			{
				// TODO:
				Serial.println(F("Error: enterDir - no subdirectory!"));
			}
		}
		else
		{
			// TODO:
			Serial.println(F("Error: enterDir - directory depth exceeded!"));
		}

		return false;
	}

	/// This function leaves a subdirectory.
	bool leaveDir()
	{
		// leave only subdirectory
		if (dir_depth > 0)
		{
			dir[dir_depth].close();

			entry_index = dir_index[dir_depth];

			--dir_depth;

			fetchEntry(entry_index);

			return true;
		}

		return false;
	}
} Storage;
