#include <Arduino.h>

//#include <DigitalIO.h>       // Sketch > Include library > Manage libraries > search: DigitalIO, by Bill Greiman
#include <SdFat.h>			// Sketch > Include library > Manage libraries > search: SdFat, by Bill Greiman
//#include <TimerThree.h>		// Sketch > Include library > Manage libraries > search: TimerThree, by Jesse Tane, Jérôme Despatis, Michael Polli, Dan Clemens, Paul Stoffregen

#define HAS_LCD16x2 1
#define HAS_ANALOG_BTNSET 1

#define LEP_UNIT        16     // LEP resolution in µs unit

#define SD0_MO          50     // SD MOSI
#define SD0_MI          51     // SD MISO
#define SD0_CK          52     // SD SCK
#define SD0_SS          53     // SS SS

#define MZT_DI          15     // MZTape WRITE (WRITE -> DI)
#define MZT_MI          16     // MZTape MOTOR ON (MOTOR ON -> MI) 
#define MZT_DO           3     // MZTape READ (DO -> READ)
#define MZT_CS          18     // MZTape /SENSE (CS -> /SENSE) 
#define MZT_LO          19     // MZTape LED OUTPUT

#define DIR_DEPTH       8
#define SFN_DEPTH       13
#define LFN_DEPTH       100

#define ENTRY_UNK       0
#define ENTRY_DIR       1
#define ENTRY_LEP       2 // WAV-like file with 16us resolution where a byte encodes an edge change after a period
#define ENTRY_MZF       3 // well-known binary format which includes a 128-byte header block and a data block (MZF/M12).
#define ENTRY_MZT       4 // may contain more than one 128-byte header and one data block.

#define SPEED_LEGACY    0 // SHARP PWM system (MZ-700/800) 
#define SPEED_ULTRAFAST 1

char				entry_type = ENTRY_UNK;

SdFat				sd;
SdFile				entry;
SdFile				dir[DIR_DEPTH];
char				sfn[SFN_DEPTH];
char				lfn[LFN_DEPTH + 1];
bool				sd_ready = false;
int16_t				entry_index = 0;
int8_t				dir_depth = -1;
int16_t				dir_index[DIR_DEPTH] = {};
bool				canceled = false;
bool				ultrafast = false;
char				header_buffer[128];
char const			loader_buffer[] =
{
	//    _          ____
	// DI  \________/
	//    ____  _________
	// DO ____><_________
	//    _______       _
	// CS        \_____/  

	//						//	1108                    _main:
	0xED, 0x43, 0x02, 0x11,	//	1108    ED 43 02 11     	ld		(0x1102),bc
	0x22, 0x04, 0x11,		//	110C    22 04 11        	ld		(0x1104),hl
	0xED, 0x53, 0x06, 0x11,	//	110F    ED 53 06 11     	ld		(0x1106),de
	0xE5,					//	1113    E5              	push	hl
	0x11, 0x02, 0xE0,		//	1114    11 02 E0        	ld		de,0xe002
	0x3E, 0x03,				//	1117    3E 03           	ld		a,0x03		; set WRITE to high
	0x12,					//	1119    12              	ld		(de),a
	//						//	111A                    l1 :
	0xC5,					//	111A    C5              	push	bc
	0x01, 0x00, 0x08,		//	111B    01 00 08        	ld		bc,0x0800
	//						//	111E                    l2 :
	0x1A,					//	111E    1A              	ld		a,(de)
	0xE6, 0x10,				//	111F    E6 10				and		0x10		; is MOTOR low / SENSE high ?
	0x20, 0xFB,				//	1121    28 FB           	jr		nz,l2
	0x3E, 0x02,				//	1123    3E 02           	ld		a,0x02		; set WRITE to low
	0x12,					//	1125    12              	ld		(de),a
	//						//	1126                    l3:
	0x1A,					//	1126    1A              	ld		a,(de)
	0xCB, 0x67,				//	1127    CB 67           	bit		4,a			; is MOTOR high / SENSE low ?
	0x28, 0xFB,				//	1129    28 FB           	jr		z, l3
	0xE6, 0x20,				//	112B    E6 20				and		0x20		; retrieve READ bit
	0xB1,					//	112D    B1					or		c
	0x07,					//	112E    07              	rlca
	0x4F,					//	112F    4F              	ld		c,a
	0x3E, 0x03,				//	1130    3E 03           	ld		a,0x03		; set WRITE to high
	0x12,					//	1132    12              	ld		(de),a
	0x10, 0xF1,				//	1133    10 F1           	djnz	l3
	0x71,					//	1135    71              	ld		(hl),c
	0xC1,					//	1136    C1              	pop		bc
	0x0B,					//	1137    0B              	dec		bc
	0x79,					//	1138    79              	ld		a,c
	0xB0,					//	1139    B0					or		b
	0x23,					//	113A    23              	inc		hl
	0x20, 0xDD,				//	113B    20 DD           	jr		nz,l1
	0xE1,					//	113D    E1              	pop		hl
	0xE9					//	113E    E9              	jp		(hl)
};

class OneShortPulse
{
	unsigned long time_point;

public:
	OneShortPulse() : time_point(micros())
	{}

	static inline void setup()
	{
		TCCR3B = 0;
		TCNT3 = 0x0000;
		ICR3 = 0;
		OCR3B = 0xffff;
		TCCR3A = (1 << COM3B0) | (1 << COM3B1) | (1 << WGM31);
		TCCR3B = (1 << WGM32 ) | (1 << WGM33 ) | (1 << CS30 );
		DDRE = (1 << 4);
	}

	static inline bool inProgress()
	{
		return TCNT3 > 0;
	}

	static inline void fire()
	{
		TCNT3 = OCR3B - 1;
	}

	static inline void fire(uint16_t cycles)
	{
		uint16_t m = 0xffff - (cycles - 1);
		OCR3B = m;
		TCNT3 = m - 1;
	}

	inline void fire(unsigned long mark, unsigned long total)
	{
		fire(uint16_t(mark * F_CPU / 1000000));
		time_point = micros() + total;
	}

	inline void adjustWait(unsigned long us)
	{
		time_point += us;
	}


	inline void wait()
	{
		while (micros() < time_point);
	}

	inline void start()
	{
		TCNT3 = 0x0000;
		OCR3B = 0xffff;
		TCCR3B = (1 << WGM32) | (1 << WGM33) | (1 << CS30);
		time_point = micros();
	}

	inline void stop()
	{
		TCCR3B = 0;
		PORTE |= (1 << PE4);
		time_point = micros();
	}
} osp;

#if HAS_LCD16x2  

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

/// This function displays a text on the first line with a horizontal scrolling if necessary.
void scrollText(char* text)
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
#endif

#define BTN_R         0      // Button RIGHT
#define BTN_U         1      // Button UP
#define BTN_D         2      // Button DOWN
#define BTN_L         3      // Button LEFT
#define BTN_S         4      // Button SELECT
#define BTN_N         5      // Button None

bool          btn_pressed = false;

/// This function reads an analogic pin to determine which button is pressed.
/// @return BTN_L, BTN_R, BTN_U, BTN_D or BTN_S if one pressed (left, right, up, down or select buttons), otherwise BTN_N (none)
int readButtons()
{
#if HAS_ANALOG_BTNSET
	int adc_key = analogRead(0);

	if (adc_key < 380)
	{
		if (adc_key < 50)
			return BTN_R;
		else
			return (adc_key < 195) ? BTN_U : BTN_D;
	}
	else
	{
		if (adc_key < 555)
			return BTN_L;
		else
			return (adc_key < 790) ? BTN_S : BTN_N;
	}
#else
	return BTN_N;
#endif
}

bool checkForLEP(char *filename)
{
	auto ext = strlwr(filename + (strlen(filename) - 4));

	return !!strstr(ext, ".lep");
}

bool checkForMZF(char *filename)
{
	auto ext = strlwr(filename + (strlen(filename) - 4));

	return !!strstr(ext, ".mzf") ||
		!!strstr(ext, ".m12");
}

bool checkForMZT(char *filename)
{
	auto ext = strlwr(filename + (strlen(filename) - 4));

	return !!strstr(ext, ".mzt");
}

void setupDisplay()
{
#if HAS_LCD16x2  
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
#endif
}

void displayNoSDCard()
{
#if HAS_LCD16x2  
	lcd.clear();
	lcd.print(F("MZ-SD2CMT: No SD card"));
#endif
}

inline void displayEntryNameMessage(bool exists)
{
#if HAS_LCD16x2  
	scroll_time = millis() + SCROLL_WAIT;
	scroll_pos = 0;

	scrollText(lfn);
#endif

	if (exists)
	{
		entry.printFileSize(&Serial);
		Serial.write(' ');
		entry.printModifyDateTime(&Serial);
		Serial.write(' ');
		entry.printName(&Serial);
		if (entry.isDir())
		{
			Serial.write('/');
		}
		Serial.println();
	}
	else
	{
		Serial.println(F("Error: fetchEntry - no file/directory!"));
	}
}

inline void displayScrollingMessage()
{
#if HAS_LCD16x2  
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
#endif
}

inline void displayStartPlayingMessage()
{
#if HAS_LCD16x2  
	lcd.clear();
	scroll_pos = 0;
	scrollText(lfn);
	lcd.setCursor(0, 1);
	lcd.print(F("Playing...[    ]"));
#endif
}

inline void displayResumePlayingMessage()
{
#if HAS_LCD16x2  
	lcd.setCursor(0, 1);
	lcd.write(F("Play"));
#endif
}

inline void displayStopPlayingMessage(unsigned long elapsed_time)
{
#if HAS_LCD16x2  
	auto duration = (elapsed_time + 999) / 1000;
	lcd.setCursor(0, 1);
	if (canceled)
	{
		lcd.print(F("Canceled in "));
		lcd.print(duration);
		lcd.print(F("s.  "));
	}
	else
	{
		lcd.print(F("Done in "));
		lcd.print(duration);
		lcd.print(F("s.      "));
	}
#endif
}

inline void displayPausingMessage()
{
#if HAS_LCD16x2  
	lcd.setCursor(0, 1);
	lcd.write(F("Paus"));
#endif
}

inline void displayProgressBar(unsigned long new_progress)
{
#if HAS_LCD16x2  
	lcd.setCursor(11 + (new_progress / 5), 1);
	lcd.write(new_progress % 5);
#endif
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
	}
	while (!found && index > 0);

	if (found)
	{
		entry.getSFN(sfn);
		entry.getName(lfn, LFN_DEPTH);

		/**/ if (entry.isDir())
		{
			entry_type = ENTRY_DIR;
		}
		else if (checkForLEP(lfn))
		{
			entry_type = ENTRY_LEP;
		}
		else if (checkForMZF(lfn))
		{
			entry_type = ENTRY_MZF;
		}
		else if (checkForMZT(lfn))
		{
			entry_type = ENTRY_MZT;
		}
		else
		{
			entry_type = ENTRY_UNK;
		}

		displayEntryNameMessage(true);

		entry_index = new_index;
	}
	else
	{
		memset(sfn, 0, SFN_DEPTH);
		memset(lfn, 0, LFN_DEPTH + 1);
		strcpy(lfn, "<no file>");

		displayEntryNameMessage(false);
	}
}

/// This function enters a directory.
void enterDir()
{
	if (dir_depth < DIR_DEPTH - 2)
	{
		if (dir_depth < 0)
		{
			if (dir[0].openRoot(&sd))
			{
				++dir_depth;

				fetchEntry(0);
			}
			else
			{
				Serial.println(F("Error: enterDir - cannot open root directory!"));
			}
		}
		else if (entry.isOpen())
		{
			++dir_depth;

			dir[dir_depth] = entry;
			dir_index[dir_depth] = entry_index;

			fetchEntry(0);
		}
		else
		{
			Serial.println(F("Error: enterDir - no subdirectory!"));
		}
	}
	else
	{
		Serial.println(F("Error: enterDir - directory depth exceeded!"));
	}
}

/// This function leaves a subdirectory.
void leaveDir()
{
	// leave only subdirectory
	if (dir_depth > 0)
	{
		dir[dir_depth].close();

		entry_index = dir_index[dir_depth];

		--dir_depth;

		fetchEntry(entry_index);
	}
}

/// This function handles the case where LEFT button is pressed.
void leftPressed()
{
	if (!btn_pressed)
	{
		leaveDir();

		btn_pressed = true;
	}
}

/// This function handles the case where RIGHT button is pressed.
void rightPressed()
{
	if (!btn_pressed)
	{
		btn_pressed = true;
	}
}

/// This function handles the case where UP button is pressed.
void upPressed()
{
	if (!btn_pressed)
	{
		fetchEntry(entry_index - 1);

		btn_pressed = true;
	}
}

/// This function handles the case where DOWN button is pressed.
void downPressed()
{
	if (!btn_pressed)
	{
		fetchEntry(entry_index + 1);

		btn_pressed = true;
	}
}

/// This function handles the case where SELECT button is pressed.
void selectPressed()
{
	if (!btn_pressed)
	{
		switch (entry_type)
		{
		case ENTRY_DIR:
		{
			enterDir();
		}
		break;

		case ENTRY_LEP:
		case ENTRY_MZF:
		{
			displayStartPlayingMessage();

			entry.rewind();

			unsigned long start_time = millis();

			if (entry_type == ENTRY_LEP)
				playLEP();
			else
				playMZF();

			unsigned long stop_time = millis();

			displayStopPlayingMessage(stop_time - start_time);
		}
		break;

		default:
			break;
		}

		btn_pressed = true;
	}
}

/// This function handles the case where a button is released.
void nonePressed()
{
	if (btn_pressed)
	{
		btn_pressed = false;
	}
}

/// This function plays a LEP file.
void playLEP()
{
	unsigned long period;                   // half-pulse period
	unsigned long delta;                    // maximum time to spend before moving to the next half-pulse
	unsigned long last_edge;                // elapsed time since the last rising / falling edge
	bool          level = HIGH;             // level of the DATA IN signal at the output of the CMT
	bool          led = LOW;                // led indicating the frequency of reading data
	char          data, next = 0;           // LEP bytes read from the SD
	unsigned long count = 0;                // number of LEP bytes read progressively
	unsigned long led_period = 0;           // blinking period for 512 bytes LEP read
	unsigned long total = entry.fileSize(); // total number of LEP bytes to read
	unsigned long old_progress = -1;
	unsigned long new_progress = 0;

	canceled = false;

	digitalWrite(MZT_DO, level); // DATA IN signal to 1 initially
	digitalWrite(MZT_LO, led); // led light off initially

	digitalWrite(MZT_CS, LOW); // signal /SENSE at 0 to acknowledge the MZ that data is ready

	last_edge = micros(); // initial elapsed time
	delta = 125000 * LEP_UNIT; // foolish MONITOR MOTOR call which waits for 2s

	while (count < total) // read all the LEP bytes from the file
	{
		if (digitalRead(MZT_MI) == LOW) // MOTOR at 0, pause
		{
			digitalWrite(MZT_DO, HIGH); // DAT IN signal at 1
			digitalWrite(MZT_LO, LOW); // LED off

			displayPausingMessage();

			while (digitalRead(MZT_MI) == LOW) // as long as MOTOR does not resume
			{
				if (Serial.available() || readButtons() == BTN_S) // but if you ask to cancel
				{
					canceled = true;
					digitalWrite(MZT_CS, HIGH);  // the signal /SENSE reset to 1
					return; // leave totally
				}
			}

			displayResumePlayingMessage();

			last_edge = micros(); // reset
			delta = 125000 * LEP_UNIT; // foolish MONITOR MOTOR call which waits for 2s
		}

		if (next) // the following LEP byte is immediately available
		{
			data = next; // take it
			next = 0;
			++count;
		}
		else
		{
			data = entry.read(); // otherwise we read it from the SD
			++count;

			if (data < 0) // if the new LEP byte is a falling edge
			{
				if (count < total)
				{
					next = entry.read(); // read in advance the following LEP byte

					new_progress = (5 * 4 * count) / total;
					if (old_progress != new_progress)
					{
						displayProgressBar(new_progress);

						old_progress = new_progress;
					}
				}
			}
		}

		/**/ if (data == 0) period = 127;   // very long period ...
		else if (data <  0) period = -data; // in absolute value
		else                period = +data; // in absolute value

		period *= LEP_UNIT; // converts to microseconds

		/**/ if (data > 0) level = HIGH; // if positive LEP byte, next half-pulse at 1
		else if (data < 0) level = LOW;  // if negative LEP byte, next half-pulse at 0

		while (micros() - last_edge < delta); // we pause in the desired period

		digitalWrite(MZT_DO, level); // and we update the level output DATA IN

		last_edge = micros(); // update the time reference for the period
		delta = period; // and the next period

		++led_period;

		if (led_period & 512) // the control led is alternated every 512 LEP bytes processed
		{
			led = !led; // toggle the signal level of the LED indicator
			digitalWrite(MZT_LO, led);
		}
	}
	digitalWrite(MZT_LO, LOW); // it's over, no more led.
	digitalWrite(MZT_CS, HIGH); // reset the /SENSE signal to 1
	digitalWrite(MZT_DO, HIGH);
}

void playMZF()
{
	//playMZF_Legacy_Procedural();
	playMZF_Legacy_MachineState();
}

//////////////////////////////////////////////////////////////////////

#if 0
static const unsigned long sp1 = 240; // short period for signal 1 
static const unsigned long lp1 = 464; // long period for signal 1
static const unsigned long sp0 = 504; // short period for signal 0
static const unsigned long lp0 = 958; // long period for signal 0

void sendBit(unsigned long p1, unsigned long p0)
{
	osp.wait();
	osp.fire(p1, p0);
}

void sendEdge()
{
	sendBit(lp1, lp0);
}

void sendGap(unsigned short loop)
{
	while (loop--)
	{
		sendBit(sp1, sp0);
	}
}

void sendTapeMark(unsigned char loop)
{
	for (auto i = loop; i; --i)
	{
		sendBit(lp1, lp0);
	}
	for (auto i = loop; i; --i)
	{
		sendBit(sp1, sp0);
	}
}

unsigned short sendByte(char value)
{
	unsigned short checksum = 0;
	for (char i = 8; i; --i)
	{
		if (value & 0x80)
		{
			sendBit(lp1, lp0);
			++checksum;
		}
		else
		{
			sendBit(sp1, sp0);
		}
		value <<= 1;
	}
	sendEdge();
	return checksum;
}

void sendChecksum(unsigned short checksum)
{
	for (char j = 2; j; --j)
	{
		for (char i = 8; i; --i)
		{
			if (checksum & 0x8000)
			{
				sendBit(lp1, lp0);
			}
			else
			{
				sendBit(sp1, sp0);
			}
			checksum <<= 1;
		}
		sendEdge();
	}
}

void playMZF_Legacy_Procedural()
{
	unsigned long  total = entry.fileSize(); // total number of LEP bytes to read
	unsigned short checksum;
	unsigned long  size;
	unsigned long  addr;
	unsigned long  exec;

	canceled = false;

	if (total > 128)
	{
		total -= 128;
		if (entry.read(header_buffer, 128) != 128)
		{
			canceled = true;
			return;
		}
		else
		{
			total =
				(((unsigned long)header_buffer[0x12]) & 255) +
				(((unsigned long)header_buffer[0x13]) & 255) * 256;

			Serial.print(F("Program size: "));
			Serial.println(total);
		}
		if (entry.available() < total)
		{
			canceled = true;
			return;
		}

		size =
			total;

		addr =
			(((unsigned long)header_buffer[0x14]) & 255) +
			(((unsigned long)header_buffer[0x15]) & 255) * 256;


		exec =
			(((unsigned long)header_buffer[0x16]) & 255) +
			(((unsigned long)header_buffer[0x17]) & 255) * 256;

		Serial.print(F("SIZE: "));
		Serial.println(size | 0xC0DE0000, HEX);

		Serial.print(F("ADDR: "));
		Serial.println(addr | 0xC0DE0000, HEX);

		Serial.print(F("EXEC: "));
		Serial.println(exec | 0xC0DE0000, HEX);
	}

	digitalWrite(MZT_LO, LOW); // led light off initially
    digitalWrite(MZT_CS, LOW); // signal /SENSE at 0 to acknowledge the MZ that data is ready
	osp.start();

	int trial = 2;

retry:

	// HEADER

	delay(2000);

	sendGap(100);

	sendTapeMark(40);

	sendEdge();
	sendEdge();

	checksum = 0;

	for (auto i = 0u; i < 128u; ++i)
	{
		checksum += sendByte(header_buffer[i]);
	}

	sendChecksum(checksum);

	sendEdge();

	// PROGRAM

	delay(2000);

	sendGap(100);

	sendTapeMark(20);

	sendEdge();
	sendEdge();

	checksum = 0;

	for (auto i = total; i; --i)
	{
		checksum += sendByte(entry.read());
	}

	sendChecksum(checksum);

	sendEdge();

	delay(2000);

	Serial.print(F("CHKS: "));
	Serial.println(checksum | 0xC0DE0000, HEX);

	if (digitalRead(MZT_MI) == HIGH)
	{
		Serial.println(F("CHECKSUM Error!"));

		if (--trial)
		{
			entry.seekSet(128);
			goto retry;
		}
	}

	osp.stop();
	digitalWrite(MZT_CS, HIGH); // reset the /SENSE signal to 1
}
#endif

//////////////////////////////////////////////////////////////////////


enum LegacyStep
{
	SHARP_PWM_BEGIN,

	// GAP
	SHARP_PWM_GAP, // 100 short pulses

	// TAPEMARK
	SHARP_PWM_TMl, // 40|20 long pulses
	SHARP_PWM_TM2, // 40|20 short pulses
	SHARP_PWM_TM3, // 1 long pulse

	// BYTE BLOCK
	SHARP_PWM_BB1, // (128|n) read byte
	SHARP_PWM_BB2, // (128|n) x 8 short or long pulses
	SHARP_PWM_BB3, // (128|n) x 2 long pulse

	// CHECKSUM
	SHARP_PWM_CS1, // 2 x 8 short or long pulses
	SHARP_PWM_CS2, // 2 x 1 long pulse
	SHARP_PWM_CS3, // 1 long pulse

	SHARP_PWM_WAIT,

	SHARP_PWM_ULTRAFAST,

	SHARP_PWM_END
};

#define SERIAL_PLAYING_MZF 0

/// This function plays a MZF file in legacy mode.
void playMZF_Legacy_MachineState()
{
	unsigned long checksum;
	unsigned long period1, period0;         // half-pulse period (mark, space) 
	unsigned long last_edge;                // elapsed time since the last rising / falling edge
	bool          led = LOW;                // led indicating the frequency of reading data
	char          data;                     // LEP bytes read from the SD
	unsigned long count = 0;                // number of bytes read progressively
	unsigned long led_period = 0;           // blinking period for 512 bytes read
	unsigned long total = entry.fileSize(); // total number of bytes to read
	unsigned long fsize = total;
	unsigned long old_progress = -1;
	unsigned long new_progress = 0;
	unsigned long loop;
	LegacyStep    step = SHARP_PWM_BEGIN;
	bool          header = true;
	char          jumper_buffer[0x0C];

	//const unsigned long sp1 = 240;       // short period for signal 1 
	//const unsigned long lp1 = 464;       // long period for signal 1
	//const unsigned long sp0 = 504 - sp1; // short period for signal 0
	//const unsigned long lp0 = 958 - lp1; // long period for signal 0

	const unsigned long sp1 = 240;       // short period for signal 1
	const unsigned long lp1 = 440;       // long period for signal 1
	const unsigned long sp0 = 480 - sp1; // short period for signal 0
	const unsigned long lp0 = 680 - lp1; // long period for signal 0

	canceled = false;

	osp.start();
	osp.adjustWait(125000 * LEP_UNIT); // 2s

	digitalWrite(MZT_LO, led); // led light off initially

	digitalWrite(MZT_CS, LOW); // signal /SENSE at 0 to acknowledge the MZ that data is ready

	if (total > 128)
	{
		if (entry.read(header_buffer, 128) != 128)
		{
			canceled = true;
			step = SHARP_PWM_END;
		}
		else
		{
			total =
				(((unsigned long)header_buffer[0x12]) & 255) +
				(((unsigned long)header_buffer[0x13]) & 255) * 256;

#if 0
			if (header_buffer[0x00] == 1)
			{
				Serial.println("Ultrafast mode!");

				ultrafast = true;

				jumper_buffer[0x00] = 0x01;					// 01 xx xx        	ld		bc,$xxxx
				jumper_buffer[0x01] = header_buffer[0x12];
				jumper_buffer[0x02] = header_buffer[0x13];
				jumper_buffer[0x03] = 0x21;					// 21 yy yy        	ld		hl,$yyyy
				jumper_buffer[0x04] = header_buffer[0x14];
				jumper_buffer[0x05] = header_buffer[0x15];
				jumper_buffer[0x06] = 0x11;					// 11 zz zz        	ld		de,$zzzz
				jumper_buffer[0x07] = header_buffer[0x16];
				jumper_buffer[0x08] = header_buffer[0x17];
				jumper_buffer[0x09] = 0xC3;					// C3 08 11        	jp		0x1108
				jumper_buffer[0x0A] = 0x08;
				jumper_buffer[0x0B] = 0x11;

				header_buffer[0x12] = sizeof(jumper_buffer) & 255;
				header_buffer[0x13] = 0x00;
				header_buffer[0x16] = header_buffer[0x14];
				header_buffer[0x17] = header_buffer[0x15];

				for (size_t i = 0; i < sizeof(loader_buffer); ++i)
				{
					header_buffer[0x18 + i] = loader_buffer[i];
				}
			}
#endif
		}
	}

	while (step != SHARP_PWM_END)
	{
		if (step != SHARP_PWM_ULTRAFAST && digitalRead(MZT_MI) == LOW) // MOTOR at 0, pause
		{
			bool multiple_data = entry.available() != 0;

			if (!header && ultrafast)
			{
				Serial.print(F("Starting Ultrafast transfer... "));

				osp.stop();

				loop = total * 8;

				step = SHARP_PWM_ULTRAFAST;

				digitalWrite(MZT_CS, HIGH);  // the signal /SENSE reset to 1

				continue;
			}

			if (header || multiple_data)
			{
				osp.stop();

				digitalWrite(MZT_LO, LOW); // LED off

				Serial.print(F("Waiting for motor on... "));

				displayPausingMessage();

				while (digitalRead(MZT_MI) == LOW) // as long as MOTOR does not resume
				{
					if (Serial.available() || readButtons() == BTN_S) // but if you ask to cancel
					{
						canceled = true;
						digitalWrite(MZT_CS, HIGH);  // the signal /SENSE reset to 1

						return; // leave totally
					}
				}

				Serial.println(F("Done!"));

				displayResumePlayingMessage();

				if (!header)
				{
					total = entry.available();

					Serial.print(F("Secondary data block size: "));
					Serial.println(total);
				}

				osp.start();
				osp.adjustWait(125000 * LEP_UNIT); // 2s

				header = false;
				step = SHARP_PWM_BEGIN;
			}
			else
			{
				step = SHARP_PWM_END;

				continue;
			}
		}

		switch (step)
		{
		case SHARP_PWM_BEGIN:
			loop = 100;
			step = SHARP_PWM_GAP;

		case SHARP_PWM_GAP:
			period1 = sp1;
			period0 = sp0;
			if (loop == 0)
			{
				step = SHARP_PWM_TMl;
				loop = header ? 40 : 20;
			}
			--loop;
			break;

		case SHARP_PWM_TMl:
			period1 = lp1;
			period0 = lp0;
			if (loop == 0)
			{
				step = SHARP_PWM_TM2;
				loop = header ? 40 : 20;
			}
			--loop;
			break;

		case SHARP_PWM_TM2:
			period1 = sp1;
			period0 = sp0;
			if (loop == 0)
			{
				step = SHARP_PWM_TM3;
				loop = 2;
			}
			--loop;
			break;

		case SHARP_PWM_TM3:
			period1 = lp1;
			period0 = lp0;
			if (loop == 0)
			{
				step = SHARP_PWM_BB1;
				loop = header ? (128 * 8) : ultrafast ? (sizeof(jumper_buffer) * 8) : (total * 8);
				checksum = 0;
			}
			--loop;
			break;

		case SHARP_PWM_BB1:
			data = header ? header_buffer[count] : ultrafast ? jumper_buffer[count - 128] : entry.read();
			++count;
			step = SHARP_PWM_BB2;

		case SHARP_PWM_BB2:
			if (loop)
			{
				if ((loop & 7) == 0)
				{
					step = SHARP_PWM_BB3;
				}
			}
			else
			{
				step = SHARP_PWM_CS1;
				loop = 2 * 8;
			}
			if (data & 0x80)
			{
				period1 = lp1;
				period0 = lp0;
				++checksum;
			}
			else
			{
				period1 = sp1;
				period0 = sp0;
			}
			data <<= 1;
			--loop;
			break;

		case SHARP_PWM_BB3:
			period1 = lp1;
			period0 = lp0;
			step = SHARP_PWM_BB1;
			break;

		case SHARP_PWM_CS1:
			period1 = lp1;
			period0 = lp0;
			step = SHARP_PWM_CS2;
			break;

		case SHARP_PWM_CS2:
			if (loop)
			{
				if ((loop & 7) == 0)
				{
					step = SHARP_PWM_CS1;
				}
			}
			else
			{
				step = SHARP_PWM_CS3;
				loop = 2;
			}
			if (checksum & 0x8000)
			{
				period1 = lp1;
				period0 = lp0;
			}
			else
			{
				period1 = sp1;
				period0 = sp0;
			}
			checksum <<= 1;
			--loop;
			break;

		case SHARP_PWM_CS3:
			period1 = lp1;
			period0 = lp0;
			if (loop == 0)
			{
				step = SHARP_PWM_WAIT;
			}
			--loop;
			break;

		case SHARP_PWM_WAIT:
			continue;

		case SHARP_PWM_ULTRAFAST:
			if (loop != 0)
			{
				if ((loop & 7) == 0)
				{
					data = entry.read();
					++count;
				}
				if (data & 0x80)
				{
					period1 = 1;
				}
				else
				{
					period1 = 0;
				}
				data <<= 1;
				--loop;
			}
			else
			{
				step = SHARP_PWM_END;
			}
			break;

		default:
			step = SHARP_PWM_END;
			canceled = true;

		case SHARP_PWM_END:
			continue;
		}

		if (count < fsize)
		{
			new_progress = (5 * 4 * count) / fsize;
			if (old_progress != new_progress)
			{
				displayProgressBar(new_progress);

				old_progress = new_progress;
			}
		}

		/**/ if (step < SHARP_PWM_ULTRAFAST)
		{
			osp.wait();
			osp.fire(period1, period1 + period0);
		}
		else if (step == SHARP_PWM_ULTRAFAST)
		{
			//    __
			// DI   \_
			//    ____
			// DO ____
			//    ____     
			// CS   
			Serial.println("R: DI -> 0");
			while (digitalRead(MZT_DI) == LOW);

			//    _
			// DI  \_____
			//    ____  _
			// DO ____><_
			//    _______     
			// CS   
			Serial.println("W: DO -> 0|1");
			digitalWrite(MZT_DO, period1 ? HIGH : LOW);

			//    _
			// DI  \_______
			//    ____  ___
			// DO ____><___
			//    _______
			// CS        \_  
			Serial.println("W: CS -> 1");
			digitalWrite(MZT_CS, LOW);

			//    _          _
			// DI  \________/
			//    ____  ______
			// DO ____><______
			//    _______
			// CS        \____  
			Serial.println("R: DI -> 1");
			while (digitalRead(MZT_DI) == HIGH);

			//    _          ____
			// DI  \________/
			//    ____  _________
			// DO ____><_________
			//    _______       _
			// CS        \_____/  
			Serial.println("W: CS -> 0");
			digitalWrite(MZT_CS, HIGH);
		}

		++led_period;

		if (led_period & 256) // the control led is alternated every 256 bits processed
		{
			led = !led; // toggle the signal level of the LED indicator
			digitalWrite(MZT_LO, led);
		}
	}

	digitalWrite(MZT_LO, LOW); // it's over, no more led.

	digitalWrite(MZT_CS, HIGH); // reset the /SENSE signal to 1

	osp.stop();
}

// SCK = /SENSE ---> MZ inputs  MOTOR (PC4), MOTOR = not /SENSE
// SDI = READ   ---> MZ inputs  READ  (PC5) 
// SDO = WRITE  ---> MZ outputs WRITE (PC1)

void sendMZFByte_UltraFast(unsigned char c)
{
	// TODO    
	for (int i = 0; i < 8; ++i)
	{
		unsigned long us = micros();

		bool level = (c & (1 << i)) ? HIGH : LOW;
		digitalWrite(MZT_DO, level);                // set SDI to level 
		digitalWrite(MZT_CS, (i & 1) ? HIGH : LOW); // toggle SCK to acknowledge MZ
		digitalWrite(MZT_LO, level);

		while (micros() < us + 16);
	}
}

/// This function plays a MZF file.
void playMZF_UltraFast()
{
	unsigned long   total = entry.fileSize();
	unsigned char   data;
	unsigned long   size = 0;
	unsigned short  load = 0;
	unsigned short  exec = 0;
	unsigned long   count;
	unsigned long   old_progress = -1;
	unsigned long   new_progress = 0;
	bool            led = LOW;

	canceled = false;

	data = entry.read(); /* 0x10F0: file attribute */

	if (data == 1 && total >= 128) // On ne lit que les fichiers de type EXECUTABLE BINAIRE
	{
		for (int i = 0x10F1; i < 0x1102; ++i) entry.read(); // skip file name

		size = (/* 0x1102 */entry.read() & 255) << 0;
		size += (/* 0x1103 */entry.read() & 255) << 8;

		load = (/* 0x1104 */entry.read() & 255) << 0;
		load += (/* 0x1105 */entry.read() & 255) << 8;

		exec = (/* 0x1106 */entry.read() & 255) << 0;
		exec += (/* 0x1107 */entry.read() & 255) << 8;

		for (int i = 0x1108; i < 0x1170; ++i) entry.read(); // skip the header

		sendMZFByte_UltraFast((size >> 0) & 255);
		sendMZFByte_UltraFast((size >> 8) & 255);
		sendMZFByte_UltraFast((exec >> 0) & 255);
		sendMZFByte_UltraFast((exec >> 8) & 255);
		sendMZFByte_UltraFast((load >> 0) & 255);
		sendMZFByte_UltraFast((load >> 8) & 255);

		count = 0;

		if (total - 128 == size)
		{
			while (count != size)
			{
				data = entry.read();
				++count;

				new_progress = (5 * 4 * count) / size;
				if (old_progress != new_progress)
				{
					displayProgressBar(new_progress);

					old_progress = new_progress;
				}

				sendMZFByte_UltraFast(data);
			}
		}
		else
		{
			canceled = true;
		}

		digitalWrite(MZT_LO, LOW);
		digitalWrite(MZT_CS, HIGH);
	}
	else
	{
		canceled = true;
	}
}

void setup()
{
	OneShortPulse::setup();

	osp.stop();

	Serial.begin(115200);

	setupDisplay();

	pinMode(SD0_SS, OUTPUT);

	pinMode(MZT_DI, INPUT_PULLUP);
	pinMode(MZT_CS, OUTPUT);
	pinMode(MZT_MI, INPUT_PULLUP);
	pinMode(MZT_LO, OUTPUT);

	digitalWrite(MZT_CS, HIGH); // signal /SENSE à 1 (lecteur non disponible)
	digitalWrite(MZT_LO, LOW);  // témoin led éteint

	sd_ready = sd.begin(SD0_SS, SPI_FULL_SPEED);

	if (!sd_ready) // accès au SD en full-speed
	{
		sd.initErrorHalt();

		displayNoSDCard();
	}
}

void loop()
{
	Serial.println(F("MZ-SD2CMT"));

	enterDir();

	while (!Serial.available())
	{
		switch (readButtons())
		{
		case BTN_R:
			rightPressed();
			break;
		case BTN_L:
			leftPressed();
			break;
		case BTN_U:
			upPressed();
			break;
		case BTN_D:
			downPressed();
			break;
		case BTN_S:
			selectPressed();
			break;
		default:
			nonePressed();
			break;
		}

		displayScrollingMessage();
	}

	// Do something with Serial?
}
