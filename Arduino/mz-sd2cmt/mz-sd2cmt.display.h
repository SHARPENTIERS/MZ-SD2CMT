#pragma once

#define HAS_LCD16X2_DISPLAY 1
#define HAS_OLED128X32_DISPLAY 1
#define HAS_LED_DISPLAY 1

extern unsigned long progress_size;
extern bool ultrafast_enabled;

enum class DisplayCode : int8_t
{
	none,

	no_sdcard,

	set_entry_name,
	scroll_entry_name,

	start_playing,
	stop_playing,
	pausePlaying,
	resumePlaying,
	cancel_playing,

	updateProgressBar
};

struct DummyDisplay
{
	static inline void setLed(bool on) {}
	static inline void displayCode(DisplayCode code) {}
	static inline void setup() {}
	static inline void configure() {}
};

#include "mz-sd2cmt.display.lcd16x2.h"
#include "mz-sd2cmt.display.led.h"
#include "mz-sd2cmt.display.oled128x32.h"
#include "mz-sd2cmt.display.serial.h"

template<typename Head, typename... Rest> struct DisplaySelector
{
	static inline void setLed(bool on)
	{
		Head::setLed(on);
		DisplaySelector<Rest...>::setLed(on);
	}

	static inline void displayCode(DisplayCode code)
	{
		Head::displayCode(code);
		DisplaySelector<Rest...>::displayCode(code);
	}

	static inline void setup()
	{
		Head::setup();
		InputReaderSelector<Rest...>::setup();
	}

	static inline void configure()
	{
		Head::configure();
		InputReaderSelector<Rest...>::configure();
	}
};

template<typename Tail> struct DisplaySelector< Tail >
{
	static inline void setLed(bool on)
	{
		Tail::setLed(on);
	}

	static inline void displayCode(DisplayCode code)
	{
		return Tail::displayCode(code);
	}

	static inline void setup()
	{
		Tail::setup();
	}

	static inline void configure()
	{
		Tail::configure();
	}
};

using Display = DisplaySelector< OLED128x32Display, LedDisplay, LCD16x2Display, SerialDisplay >;
