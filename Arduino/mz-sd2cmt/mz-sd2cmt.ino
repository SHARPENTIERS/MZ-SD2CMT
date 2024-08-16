#include <Wire.h>
#include <Arduino.h>

// Sketch > Include library > Manage libraries > search: SSD1306Ascii 1.3.5, by Bill Greiman
// Sketch > Include library > Manage libraries > search: SdFat 2.2.3, by Bill Greiman
// Sketch > Include library > Manage libraries > search: irremote 4.4.0

#include "mz-sd2cmt.gpio.h"
#include "mz-sd2cmt.osp.h"
#include "mz-sd2cmt.serial.h"
#include "mz-sd2cmt.storage.h"
#include "mz-sd2cmt.display.h"
#include "mz-sd2cmt.input.h"

#define LO				0
#define HI				1

#define MZT_DI          15     // MZTape WRITE (WRITE -> DI)
#define MZT_MI          16     // MZTape MOTOR ON (MOTOR ON -> MI) 
#define MZT_DO           2     // MZTape READ (DO -> READ) - UNUSED because using OSP instead! 
#define MZT_CS          18     // MZTape /SENSE (CS -> /SENSE) 

#define port_MZT_DI		(&PINJ)
#define port_MZT_MI		(&PINH)
#define port_MZT_DO		(&PORTE)
#define port_MZT_CS		(&PORTD)

#define mask_MZT_DI		(1 << PJ0)
#define mask_MZT_MI		(1 << PH1)
#define mask_MZT_DO		(1 << PE5) // OC3C
#define mask_MZT_CS		(1 << PD3)

InputCode readInput()
{
	auto result = InputCode::none;

	return InputReader.readCode(result) ? result : InputCode::none;
}

unsigned long		progress_size;
bool				canceled = false;
bool				ultrafast = false;
bool				ultrafast_enabled = false;
bool				level = LOW;
char				header_buffer[128];
char const			loader_buffer[] =
{
    //           _________     
    // CS  _____/         \____ : Arduino changes SENSE value so MZ can retrieve the current bit 
    //     _____           ____
    // PC4      \_________/     : MZ watches PC4 edge changes set by Arduino
    //     _  __ _____  __ ____
    // PC5 _><__*_____><__*____ : MZ retrieves PC5 (*) at each PC4 edge change set by Arduino
    //              _________
    // PC1 ________/         \_ : MZ acknowledges Arduino so the latter can send the next bit 
    //     ________           _
    // DI          \_________/  : Arduino watches SI edge changes so it can send the next bit 

	// NOTE: values in microsecond unit are computed for the MZ-700.

    "\xED\x43\x02\x11"    //_main:ld      (0x1102),bc
    "\x22\x04\x11"        //      ld      (0x1104),hl
    "\xED\x53\x06\x11"    //      ld      (0x1106),de
    "\xD5"                //      push    de
    "\x11\x02\xE0"        //      ld      de,0xE002
    "\xDD\x21\x03\xE0"    //      ld      ix,0xE003
    "\xAF"                //      xor     a               ; let Arduino to set some pins...
    "\x3D"                //delay:dec     a
    "\x20\xFD"            //      jr      nz,delay
    "\xF3"                //      di
    "\x1A"                //l0:   ld      a,(de)
    "\xE6\x20"            //      and     0x20
    "\x28\xFB"            //      jr      z,l0
    "\xDD\x36\x00\x03"    //      ld      (ix+0),3
    "\xC5"                //l1:   push    bc              ; +11
    "\x01\x00\x04"        //      ld      bc,0x0400       ; +10
    "\x1A"                //l2:   ld      a,(de)          ; +7
    "\xCB\x67"            //      bit     4,a             ; +8    ; is MOTOR high / SENSE low?
    "\x28\xFB"            //      jr      z,l2            ; +12/7 --> n * 27 + 22 --> n * 7.7us + 6.3us
    "\xE6\x20"            //      and     0x20            ; +7    ; retrieve READ bit
    "\xB1"                //      or      c               ; +4
    "\x07"                //      rlca                    ; +4
    "\x4F"                //      ld      c,a             ; +4
    "\xDD\x36\x00\x02"    //      ld      (ix+0),2        ; +19   --> n * 27 + 60 -->  17.1us at best case
    "\x1A"                //l3:   ld      a,(de)          ; +7
    "\xCB\x67"            //      bit     4,a             ; +8    ; is MOTOR low / SENSE high?
    "\x20\xFB"            //      jr      nz,l3           ; +12/7 --> n' * 27 + 22 --> n * 7.7us + 6.3us
    "\xE6\x20"            //      and     0x20            ; +7    ; retrieve READ bit
    "\xB1"                //      or      c               ; +4
    "\x07"                //      rlca                    ; +4
    "\x4F"                //      ld      c,a             ; +4
    "\xDD\x36\x00\x03"    //      ld      (ix+0),3        ; +19   --> n' * 27 + 60 -->  17.1us at best case
    "\x10\xE2"            //      djnz    l2              ; +13/8 --> (n + n') * 27 + 133 --> 38us for two bits at best case
    "\x71"                //      ld      (hl),c          ; +7
    "\xC1"                //      pop     bc              ; +11
    "\x0B"                //      dec     bc              ; +6
    "\x23"                //      inc     hl              ; +6
    "\x79"                //      ld      a,c             ; +4
    "\xB0"                //      or      b               ; +4
    "\x20\xD6"            //      jr      nz,l1           ; +12/7 --> 577 cycles per byte at best case --> 164,9us per byte at best case
    "\xFB"                //      ei
    "\xE1"                //      pop     hl
//	"\xC3\xAD\x00"
    "\xE9"                //      jp      (hl)            ; at this point, 44544 bytes should be loaded in 7.4s in theory but Arduino needs to read bytes 
};

OneShortPulse Osp3;

/// This function handles the case where LEFT button is pressed.
void leftPressed()
{
	if (Storage.leaveDir())
		Display.displayCode(DisplayCode::set_entry_name);
}

/// This function handles the case where RIGHT button is pressed.
void rightPressed()
{
    ultrafast_enabled = !ultrafast_enabled;

	Display.displayCode(DisplayCode::set_entry_name);
}

/// This function handles the case where UP button is pressed.
void upPressed()
{
    Storage.fetchEntry(Storage.entry_index - 1);

	Display.displayCode(DisplayCode::set_entry_name);
}

/// This function handles the case where DOWN button is pressed.
void downPressed()
{
	Storage.fetchEntry(Storage.entry_index + 1);

	Display.displayCode(DisplayCode::set_entry_name);
}

/// This function handles the case where SELECT button is pressed.
void selectPressed()
{
    switch (Storage.entry_type)
    {
    case ENTRY_DIR:
		if (Storage.enterDir())
			Display.displayCode(DisplayCode::set_entry_name);
		break;

    case ENTRY_LEP:
	case ENTRY_WAV:
	case ENTRY_MZF:

		Storage.entry.rewind();

		Display.displayCode(DisplayCode::start_playing);

        /**/ if (Storage.entry_type == ENTRY_LEP)
            playLEP();
		else if (Storage.entry_type == ENTRY_WAV)
			playWAV();
		else
            playMZF();

		Display.displayCode((canceled) ? DisplayCode::cancel_playing : DisplayCode::stop_playing);

		break;

	default:
		break;
	}
}

/// This function handles the case where a button is released.
void nonePressed()
{
}

/// This function plays a LEP file.
void playLEP()
{
	unsigned long period, period1, period0; // half-pulse period, mark pulse period, space pulse period 
	bool          led = false;              // led indicating the frequency of reading data
	char          prev = 0, data, next = 0; // LEP bytes read from the SD
	unsigned long count = 0;                // number of LEP bytes read progressively
	unsigned long led_period = 0;           // blinking period for 512 bytes LEP read
	unsigned long total = Storage.entry.fileSize(); // total number of LEP bytes to read
	unsigned long old_progress = -1;
	unsigned long new_progress = 0;

	canceled = false;

	Display::setLed(led); // led light off initially

	set_port_bit(MZT_CS, 0); // signal /SENSE at 0 to acknowledge the MZ that data is ready

	Osp3.start();
	Osp3.adjustWait(2000000); // foolish MONITOR MOTOR call which waits for 2s

	while (Storage.entry.available() || next) // read all the LEP bytes from the file
	{
		if (get_port_bit(MZT_MI) == 0) // MOTOR at 0, pause
		{
			Display::setLed(false); // LED off

			Osp3.stop();

			Display.displayCode(DisplayCode::pause_playing);

			while (get_port_bit(MZT_MI) == 0) // as long as MOTOR does not resume
			{
				if (Serial.available() || readInput() == InputCode::select) // but if you ask to cancel
				{
					canceled = true;
					set_port_bit(MZT_CS, 1);  // the signal /SENSE reset to 1
					return; // leave totally
				}
			}

			Display.displayCode(DisplayCode::resume_playing);

			Osp3.start();
			Osp3.adjustWait(2000000); // foolish MONITOR MOTOR call which waits for 2s
		}

		if (next) // the following LEP byte is immediately available
		{
			data = next; // take it
			next = 0;
			++count;
		}
		else
		{
			data = Storage.entry.read(); // otherwise we read it from the SD
			++count;

			if (data < 0) // if the new LEP byte is a mark period
			{
				if (count < total)
				{
					next = Storage.entry.read(); // read in advance the following LEP byte

					new_progress = (5 * 16 * count) / total;
					if (old_progress != new_progress)
					{
						progress_size = new_progress;
						Display.displayCode(DisplayCode::update_progress_bar);
						old_progress = new_progress;
					}
				}
			}
		}

		/**/ if (data == 0)
		{
			/**/ if (prev < 0) period1 += 127 * Storage.lep_unit; // very long period ...
			else if (prev > 0) period0 += 127 * Storage.lep_unit; // very long period ...
		}
		else if (data < 0)
		{
			period1 = -data * Storage.lep_unit; // in absolute value
			prev = data;
		}
		else
		{
			period0 = +data * Storage.lep_unit; // in absolute value
			prev = data;

			Osp3.wait();
			Osp3.fire(period1, period1 + period0); // and we update the level output DATA IN

			if (Storage.entry.available())
			{
				period1 = 0;
				period0 = 0;
			}
		}

        ++led_period;

        if (led_period & 512) // the control led is alternated every 512 LEP bytes processed
        {
            led = !led; // toggle the signal level of the LED indicator
			Display::setLed(led);
        }
    }

	Osp3.wait();
	Osp3.fire(period1, period1 + period0); // and we update the level output DATA IN
	Osp3.wait();

	Display::setLed(false); // it's over, no more led.

	set_port_bit(MZT_CS, 1); // reset the /SENSE signal to 1

	Osp3.stop();
}

/// This function plays a WAV file.
void playWAV()
{
	unsigned long period, period1, period0; // half-pulse period, mark pulse period, space pulse period 
	bool          led = false;              // led indicating the frequency of reading data
	char          prev = 0, data, next = 0; // WAV bytes read from the SD
	unsigned long count = 0;                // number of WAV bytes read progressively
	unsigned long led_period = 0;           // blinking period for 512 pulses read
	unsigned long total = Storage.entry.fileSize(); // total number of LEP bytes to read
	unsigned long old_progress = -1;
	unsigned long new_progress = 0;

	canceled = true;

	union
	{
		struct
		{
			char     id[4];
			uint32_t size;
			char     data[4];
		} riff; // riff chunk
		struct
		{
			uint16_t compress;
			uint16_t channels;
			uint32_t sampleRate;
			uint32_t bytesPerSecond;
			uint16_t blockAlign;
			uint16_t bitsPerSample;
			uint16_t extraBytes;
		} fmt; // fmt data
	} buffer;

	// must start with WAVE header
	if (Storage.entry.read(&buffer, 12) != 12	||
		strncmp(buffer.riff.id,   "RIFF", 4)	||
		strncmp(buffer.riff.data, "WAVE", 4))
	{
		Serial.println(F("WAV: No header!"));
		return;
	}

	// next chunk must be fmt
	if (Storage.entry.read(&buffer, 8) != 8	||
		strncmp(buffer.riff.id, "fmt ", 4))
	{
		Serial.println(F("WAV: No 'fmt'!"));
		return;
	}

	// fmt chunk size must be 16 or 18
	auto size = buffer.riff.size;
	if (size == 16 || size == 18)
	{
		if (Storage.entry.read(&buffer, size) != (int16_t)size)
		{
			Serial.println(F("WAV: Invalid 'fmt'!"));
			return;
		}
	}
	else
	{
		buffer.fmt.compress = 0;
	}

	if (buffer.fmt.compress != 1 || (size == 18 && buffer.fmt.extraBytes != 0))
	{
		Serial.println(F("WAV: Compression not supported!"));
		return;
	}

	if (buffer.fmt.channels > 1)
	{
		Serial.println(F("WAV: Not mono!"));
		return;
	}

	if (buffer.fmt.bitsPerSample != 8)
	{
		Serial.println(F("WAV: Not 8 bits per sample!"));
		return;
	}

	unsigned long wav_rate = buffer.fmt.sampleRate;

	canceled = false;

	Display::setLed(led); // led light off initially

	set_port_bit(MZT_CS, 0); // signal /SENSE at 0 to acknowledge the MZ that data is ready

	Osp3.start();
	Osp3.adjustWait(2000000); // foolish MONITOR MOTOR call which waits for 2s

	auto readBytes = [&]()
	{
		char accumulator = 0;
		do
		{
			char sample = char(Storage.entry.read());
			++count;
			/**/ if (sample < 0) --accumulator;
			else if (sample > 0) ++accumulator;
			else                   accumulator = 0;
		}
		while (Storage.entry.available() && (0x80 & (char(-Storage.entry.peek()) ^ accumulator)));

		return accumulator;
	};

	while (Storage.entry.available() || next)
	{
		if (get_port_bit(MZT_MI) == 0) // MOTOR at 0, pause
		{
			Display::setLed(false); // LED off

			Osp3.stop();

			Display.displayCode(DisplayCode::pause_playing);

			while (get_port_bit(MZT_MI) == 0) // as long as MOTOR does not resume
			{
				if (Serial.available() || readInput() == InputCode::select) // but if you ask to cancel
				{
					canceled = true;
					set_port_bit(MZT_CS, 1);  // the signal /SENSE reset to 1
					return; // leave totally
				}
			}

			Display.displayCode(DisplayCode::resume_playing);

			Osp3.start();
			Osp3.adjustWait(2000000); // foolish MONITOR MOTOR call which waits for 2s
		}

		if (next) // the following samples are immediately available
		{
			data = next; // take it
			next = 0;
		}
		else
		{
			data = readBytes(); // read the samples of the same sign

			if (data < 0) // if it is a mark period
			{
				if (Storage.entry.available())
				{
					next = readBytes(); // read in advance the following samples

					new_progress = (5 * 16 * count) / total;
					if (old_progress != new_progress)
					{
						progress_size = new_progress;
						Display.displayCode(DisplayCode::update_progress_bar);
						old_progress = new_progress;
					}
				}
			}
		}

		/**/ if (data == 0)
		{
			/**/ if (prev < 0) period1 += 127; // very long period ...
			else if (prev > 0) period0 += 127; // very long period ...
		}
		else if (data < 0)
		{
			period1 = -data; // in absolute value
			prev = data;
		}
		else
		{
			period0 = +data; // in absolute value
			prev = data;

			Osp3.wait();
			Osp3.fire(period1 * 1000000 / wav_rate, (period1 + period0) * 1000000 / wav_rate); // and we update the level output DATA IN

			if (Storage.entry.available())
			{
				period1 = 0;
				period0 = 0;
			}
		}

		++led_period;

		if (led_period & 512) // the control led is alternated every 512 LEP bytes processed
		{
			led = !led; // toggle the signal level of the LED indicator
			Display::setLed(led);
		}
	}

	Osp3.wait();
	Osp3.fire(period1 * 1000000 / wav_rate, (period1 + period0) * 1000000 / wav_rate); // and we update the level output DATA IN
	Osp3.wait();

	Display::setLed(false); // it's over, no more led.

	set_port_bit(MZT_CS, 1); // reset the /SENSE signal to 1

	Osp3.stop();
}

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

/// This function plays a MZF file in legacy mode.
void playMZF()
{
	unsigned long checksum;
	unsigned long period1, period0;                  // half-pulse period (mark, space) 
	unsigned long last_edge;                         // elapsed time since the last rising / falling edge
	bool          led = false;                       // led indicating the frequency of reading data
	char          data;                              // LEP bytes read from the SD
	unsigned long count = 0;                         // number of bytes read progressively
	unsigned long led_period = 0;                    // blinking period for 512 bytes read
	unsigned long total = Storage.entry.fileSize();  // total number of bytes to read
	unsigned long fsize = total;
	unsigned long old_progress = -1;
	unsigned long new_progress = 0;
	unsigned long loop;
	LegacyStep    step = SHARP_PWM_BEGIN;
	bool          header = true;
	char          jumper_buffer[12];

	const unsigned long sp1 = 240;       // short period for signal 1
	const unsigned long lp1 = 440;       // long period for signal 1
	const unsigned long sp0 = 480 - sp1; // short period for signal 0
	const unsigned long lp0 = 680 - lp1; // long period for signal 0

	canceled = false;

	Osp3.start();
	Osp3.adjustWait(2000000); // 2s

	Display::setLed(led); // led light off initially

	set_port_bit(MZT_CS, 0); // signal /SENSE at 0 to acknowledge the MZ that data is ready

	if (total > 128)
	{
		if (Storage.entry.read(header_buffer, 128) != 128)
		{
			canceled = true;
			step = SHARP_PWM_END;
		}
		else
		{
			total =
				(((unsigned long)header_buffer[0x12]) & 255) +
				(((unsigned long)header_buffer[0x13]) & 255) * 256;

			if (header_buffer[0x00] == 1 && ultrafast_enabled)
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
		}
	}

	while (step != SHARP_PWM_END)
	{
		if (step != SHARP_PWM_ULTRAFAST && get_port_bit(MZT_MI) == 0) // MOTOR at 0, pause
		{
			bool multiple_data = Storage.entry.available() != 0;

			if (!header && ultrafast)
			{
				loop = total * 8;

				step = SHARP_PWM_ULTRAFAST;

				set_port_bit(MZT_CS, 1);

				Osp3.setLevel(1);

				delay(100);

				continue;
			}

			if (header || multiple_data)
			{
				Osp3.stop();

				Display::setLed(false); // LED off

				// TODO:
				Serial.print(F("Waiting for motor on... "));

				Display.displayCode(DisplayCode::pause_playing);

				while (get_port_bit(MZT_MI) == 0) // as long as MOTOR does not resume
				{
					if (Serial.available() || readInput() == InputCode::select) // but if you ask to cancel
					{
						canceled = true;
						set_port_bit(MZT_CS, 1);  // the signal /SENSE reset to 1

						return; // leave totally
					}
				}

				// TODO:
				Serial.println(F("Done!"));

				Display.displayCode(DisplayCode::resume_playing);

				if (!header)
				{
					total = Storage.entry.available();

					// TODO:
					Serial.print(F("Secondary data block size: "));
					Serial.println(total);
				}

				Osp3.start();
				Osp3.adjustWait(2000000); // 2s

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
				loop = 1;
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
			data = header ? header_buffer[count] : ultrafast ? jumper_buffer[count - 128] : Storage.entry.read();
			++count;
			period1 = lp1;
			period0 = lp0;
			step = SHARP_PWM_BB2;
			break;

		case SHARP_PWM_BB2:
			if (loop)
			{
				if ((loop & 7) == 0)
				{
					step = SHARP_PWM_BB1;
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
				//           _________     
				// CS  _____/         \____ : Arduino changes SENSE value so MZ can retrieve the current bit 
				//     _____           ____
				// PC4      \_________/     : MZ watches PC4 edge changes set by Arduino
				//     _  __ _____  __ ____
				// PC5 _><__*_____><__*____ : MZ retrieves PC5 (*) at each PC4 edge change set by Arduino
				//              _________
				// PC1 ________/         \_ : MZ acknowledges Arduino so the latter can send the next bit 
				//     ________           _
				// DI          \_________/  : Arduino watches SI edge changes so it can send the next bit 

				size_t n = loop / 8;
				if (n = Storage.entry.read(header_buffer, (n <= sizeof(header_buffer)) ? n : sizeof(header_buffer)))
				{
					for (size_t i = 0; i < n; ++i)
					{
						//char data = header_buffer[i];
						//data = (data << 2) | ((data >> 6) & 0x03);
						char data = __builtin_avr_insert_bits(0x54321076, header_buffer[i], 0);
						for (size_t j = 4; j; --j)
						{
							while (get_port_bit(MZT_DI) != 0);

							Osp3.setLevel(!!(data & 0x80));

							set_port_bit(MZT_CS, 0);

							while (get_port_bit(MZT_DI) == 0);

							Osp3.setLevel(!!(data & 0x40));

							set_port_bit(MZT_CS, 1);

							data <<= 2;
						}
					}
					count += n;
					loop -= n * 8;
				}
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
			new_progress = (5 * 16 * count) / fsize;
			if (old_progress != new_progress)
			{
				progress_size = new_progress;
				Display.displayCode(DisplayCode::update_progress_bar);
				old_progress = new_progress;
			}
		}

		if (step < SHARP_PWM_ULTRAFAST)
		{
			Osp3.wait();
			Osp3.fire(period1, period1 + period0);
		}

		++led_period;

		if (led_period & 128) // the control led is alternated every 128 bits processed
		{
			led = !led; // toggle the signal level of the LED indicator
			Display::setLed(led);
		}
	}

	Display::setLed(false); // it's over, no more led.

	set_port_bit(MZT_CS, 1); // reset the /SENSE signal to 1

	Osp3.stop();
	Osp3.setLevel(0);
}

void setup()
{
	Osp3.setup(1);

	SerialPrompt.setup();
	
	Storage.setup();
	
	if (Storage.configure(SerialPrompt.cfg, "/.config/SERIAL.debug")) serial_debug = SerialPrompt.cfg.enabled;

	InputReader.setup();

	Display.setup();

	pinMode(MZT_DI, INPUT_PULLUP);
	pinMode(MZT_CS, OUTPUT);
	pinMode(MZT_MI, INPUT_PULLUP);

	set_port_bit(MZT_CS, 1); // signal /SENSE à 1 (lecteur non disponible)

	if (!Storage.sd_ready)
	{
		Display.displayCode(DisplayCode::no_sdcard);
	}

	if (Storage.enterDir())
	{
		Display.displayCode(DisplayCode::set_entry_name);
	}
}

void loop()
{
	Osp3.setLevel(0);

	SerialCode	serialCode = SerialCode::none;
	InputCode	inputCode = InputCode::none;

	if (SerialPrompt.readCode(serialCode))
	{
		// Do something with Serial?
	}

	if (InputReader.readCode(inputCode))
	{
		switch (inputCode)
		{
		case InputCode::right:
			rightPressed();
			break;
		case InputCode::left:
			leftPressed();
			break;
		case InputCode::up:
			upPressed();
			break;
		case InputCode::down:
			downPressed();
			break;
		case InputCode::select:
			selectPressed();
			break;
		default:
			nonePressed();
			break;
		}
	}

	Display.displayCode(DisplayCode::scroll_entry_name);
}
