#pragma once

#include <LiquidCrystal.h>   // Built-in by Arduino

byte progress_icon[][8] =
{
	{
		B10000,
		B10000,
		B10000,
		B10000,
		B10000,
		B10000,
		B10000,
		B10000
	},
	{
		B11000,
		B11000,
		B11000,
		B11000,
		B11000,
		B11000,
		B11000,
		B11000
	},
	{
		B11100,
		B11100,
		B11100,
		B11100,
		B11100,
		B11100,
		B11100,
		B11100
	},
	{
		B11110,
		B11110,
		B11110,
		B11110,
		B11110,
		B11110,
		B11110,
		B11110
	},
	{
		B11111,
		B11111,
		B11111,
		B11111,
		B11111,
		B11111,
		B11111,
		B11111
	}
};

byte entry_icon[][8] =
{
	{
		B01110,
		B10001,
		B10001,
		B00010,
		B00100,
		B00000,
		B00100,
		B00100
	},
	{
		B11100,
		B10011,
		B11101,
		B10001,
		B10001,
		B10001,
		B10001,
		B11111
	},
	{
		B00111,
		B00100,
		B00100,
		B01110,
		B10101,
		B00100,
		B00100,
		B11100
	},
	{
		B01000,
		B10100,
		B10100,
		B10100,
		B00101,
		B00101,
		B00101,
		B00010
	},
	{
		B10001,
		B11011,
		B10101,
		B10001,
		B11111,
		B00100,
		B01000,
		B11111
	},
	{
		B10001,
		B11011,
		B10101,
		B10001,
		B11111,
		B00100,
		B01000,
		B11111
	}
};

struct LCD16x2Display : DummyDisplay
{
	struct Config
	{
		bool enabled = !!HAS_LCD16X2_DISPLAY;
		int scroll_speed = 250; // Text scroll delay
		int scroll_wait = 3000; // Delay before scrolling starts
	} cfg;

	static constexpr auto LCD_RS = 8; // LCD RESET;
	static constexpr auto LCD_EN = 9; // LCD ENABLE;
	static constexpr auto LCD_D4 = 4; // LCD D4;
	static constexpr auto LCD_D5 = 5; // LCD D5;
	static constexpr auto LCD_D6 = 6; // LCD D6;
	static constexpr auto LCD_D7 = 7; // LCD D7;

	LiquidCrystal lcd{LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7};
	byte          scroll_pos = 0;
	char          scroll_dir = 0;
	unsigned long scroll_time;

	/// This function displays a text on the first line with a horizontal scrolling if necessary.
	void scrollText(char* text, bool refresh_all = true)
	{
		const auto n = strlen(text);
		int i = min(scroll_pos, n);
		lcd.createChar(5, entry_icon[Storage.entry_type]);
		if (refresh_all)
		{
			lcd.clear();
		}
		else
		{
			lcd.setCursor(0, 0);
		}
		lcd.write('\x05');
		lcd.print(text + i);
		if (refresh_all)
		{
			lcd.setCursor(0, 1);
			if (Storage.entry_type > ENTRY_DIR)
				lcd.print(ultrafast_enabled ? F("speed: ultra-fast") : F("speed: normal"));
		}
		else
		{
			lcd.write(' ');
		}
	}

	void setup()
	{
		if (Storage.configure(cfg, "/.config/LCD_KEYPAD_SHIELD.display"))
		{
			lcd.begin(16, 2);

			lcd.createChar(0, progress_icon[0]);
			lcd.createChar(1, progress_icon[1]);
			lcd.createChar(2, progress_icon[2]);
			lcd.createChar(3, progress_icon[3]);
			lcd.createChar(4, progress_icon[4]);

			lcd.clear();

			lcd.print(F("SD2MZCMT"));

			Serial.println(F("Output device: LCD16X2 screen."));

			scroll_time = millis() + cfg.scroll_wait;
		}
	}

	void displayCode(DisplayCode code, const char *device, const char *message)
	{
		if (cfg.enabled)
		{
			static unsigned long start_time = millis();
			static unsigned long old_progress = 0;
			switch (code)
			{
			case DisplayCode::no_sdcard:
				lcd.clear();
				lcd.print(F("MZ-SD2CMT: No SD card"));
				break;

			case DisplayCode::set_entry_name:
				scroll_time = millis() + cfg.scroll_wait;
				scroll_pos = 0;
				scrollText(Storage.lfn);
				break;

			case DisplayCode::scroll_entry_name:
				if (millis() >= scroll_time)
				{
					auto n = strlen(Storage.lfn);
					switch (scroll_dir)
					{
					case +1:
						if (n - scroll_pos > 15)
						{
							scroll_time = millis() + cfg.scroll_speed;
							scrollText(Storage.lfn, false);
							++scroll_pos;
						}
						else
						{
							scroll_dir = -1;
							scroll_time = millis() + cfg.scroll_wait;
							scrollText(Storage.lfn, false);
						}
						break;
					case -1:
						if (scroll_pos > 0)
						{
							scroll_time = millis() + cfg.scroll_speed;
							--scroll_pos;
							scrollText(Storage.lfn, false);
						}
						else
						{
							scroll_dir = +1;
							scroll_time = millis() + cfg.scroll_wait;
						}
						break;
					default:
						if (n - scroll_pos > 15)
						{
							scroll_time = millis();
							scroll_dir = +1;
						}
						break;
					}
				}

				break;

			case DisplayCode::start_playing:
				scroll_pos = 0;
				scrollText(Storage.lfn);
				lcd.setCursor(0, 1);
				lcd.print(F("Playing...[    ]"));
				start_time = millis();
				old_progress = 0;
				break;

			case DisplayCode::stop_playing:
				if (true)
				{
					auto stop_time = millis();
					lcd.setCursor(0, 1);
					lcd.print(F("Done in "));
					lcd.print((stop_time - start_time + 999) / 1000);
					lcd.print(F("s.      "));
					old_progress = 0;
				}
				break;

			case DisplayCode::cancel_playing:
				if (true)
				{
					auto cancel_time = millis();
					lcd.setCursor(0, 1);
					lcd.print(F("Canceled in "));
					lcd.print((cancel_time - start_time + 999) / 1000);
					lcd.print(F("s.  "));
					old_progress = 0;
				}
				break;

			case DisplayCode::pause_playing:
				lcd.setCursor(0, 1);
				lcd.print(F("Paus"));
				break;

			case DisplayCode::resume_playing:
				lcd.setCursor(0, 1);
				lcd.print(F("Play"));
				break;

			case DisplayCode::update_progress_bar:
				if (progress_size < 5 * 16)
				{
					unsigned long new_progress = progress_size * 4 / 16;
					if (old_progress != new_progress)
					{
						lcd.setCursor(11 + (new_progress / 5), 1);
						lcd.write(new_progress % 5);
						old_progress = new_progress;
					}
				}
				break;

			case DisplayCode::calibrate_device:
				// lcd.clear();
				// lcd.print(device);
				// lcd.setCursor(0, 1);
				// lcd.print(message);
				break;

			default:
				break;
			}
		}
	}
};
