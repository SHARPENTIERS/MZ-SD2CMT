#pragma once

#if HAS_LCD16X2_DISPLAY  

#include <LiquidCrystal.h>   // Built-in by Arduino

#define LCD_RS        8      // LCD RESET
#define LCD_EN        9      // LCD ENABLE
#define LCD_D4        4      // LCD D4
#define LCD_D5        5      // LCD D5
#define LCD_D6        6      // LCD D6
#define LCD_D7        7      // LCD D7

#define SCROLL_SPEED  250    // Text scroll delay
#define SCROLL_WAIT   3000   // Delay before scrolling starts

byte icon[8][8] =
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

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
byte          scroll_pos = 0;
unsigned long scroll_time = millis() + SCROLL_WAIT;
unsigned long progress_size;

struct LCD16x2Display
{
	/// This function displays a text on the first line with a horizontal scrolling if necessary.
	static void scrollText(char* text)
	{
		if (scroll_pos < 0)
		{
			scroll_pos = 0;
		}
		char outtext[17];
		outtext[0] = entry_type ? (entry_type + 4) : '?';
		for (int i = 1; i < 16; ++i)
		{
			int p = i + scroll_pos - 1;
			if (p < strlen(text))
			{
				outtext[i] = text[p];
			}
			else
			{
				outtext[i] = '\0';
			}
		}
		outtext[16] = '\0';

		lcd.setCursor(0, 0);
		lcd.print(F("                    "));
		lcd.setCursor(0, 0);
		lcd.print(outtext);
		lcd.setCursor(0, 1);
		lcd.print(F("                    "));
	}

	static void setup()
	{
		lcd.begin(16, 2);

		lcd.createChar(0, icon[0]);
		lcd.createChar(1, icon[1]);
		lcd.createChar(2, icon[2]);
		lcd.createChar(3, icon[3]);
		lcd.createChar(4, icon[4]);
		lcd.createChar(5, icon[5]);
		lcd.createChar(6, icon[6]);
		lcd.createChar(7, icon[7]);
		lcd.clear();

		lcd.print(F("SD2MZCMT"));
	}

	static void displayCode(DisplayCode code)
	{
		static unsigned long start_time = millis();
		switch (code)
		{
		case DisplayCode::no_sdcard:
			lcd.clear();
			lcd.print(F("MZ-SD2CMT: No SD card"));
			break;

		case DisplayCode::set_entry_name:
			scroll_time = millis() + SCROLL_WAIT;
			scroll_pos = 0;
			scrollText(lfn);
			break;

		case DisplayCode::scroll_entry_name:
			if ((millis() >= scroll_time) && (strlen(lfn) > 15))
			{
				scroll_time = millis() + SCROLL_SPEED;
				scrollText(lfn);
				++scroll_pos;
				if (scroll_pos > strlen(lfn))
				{
					scroll_pos = 0;
					scroll_time = millis() + SCROLL_WAIT;
					scrollText(lfn);
				}
			}
			break;

		case DisplayCode::startPlaying:
			lcd.clear();
			scroll_pos = 0;
			scrollText(lfn);
			lcd.setCursor(0, 1);
			lcd.print(F("Playing...[    ]"));
			start_time = millis();
			break;

		case DisplayCode::stopPlaying:
			if (true)
			{
				auto stop_time = millis();
				lcd.setCursor(0, 1);
				lcd.print(F("Done in "));
				lcd.print((stop_time - start_time + 999) / 1000);
				lcd.print(F("s.      "));
			}
			break;

		case DisplayCode::cancelPlaying:
			if (true)
			{
				auto cancel_time = millis();
				lcd.setCursor(0, 1);
				lcd.print(F("Canceled in "));
				lcd.print((cancel_time - start_time + 999) / 1000);
				lcd.print(F("s.  "));
			}
			break;

		case DisplayCode::pausePlaying:
			lcd.setCursor(0, 1);
			lcd.write(F("Paus"));
			break;

		case DisplayCode::resumePlaying:
			lcd.setCursor(0, 1);
			lcd.write(F("Play"));
			break;

		case DisplayCode::updateProgressBar:
			lcd.setCursor(11 + (progress_size / 5), 1);
			lcd.write(progress_size % 5);
			break;

		default:
			break;
		}
	}
};

#else

using LCD16x2Display = DummyDisplay;

#endif
