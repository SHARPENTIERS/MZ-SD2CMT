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
	pause_playing,
	resume_playing,
	cancel_playing,

	update_progress_bar,

	calibrate_device
};

struct DummyDisplay
{
	static inline void setLed(bool on) {}
	static inline void displayCode(DisplayCode code) {}
	static inline void setup() {}
	static inline void configure() {}
	static inline void initSdErrorHalt() {}
};

#include "mz-sd2cmt.display.lcd16x2.h"
#include "mz-sd2cmt.display.led.h"
#include "mz-sd2cmt.display.oled128x32.h"
#include "mz-sd2cmt.display.serial.h"

template<typename Head, typename... Rest> struct DisplaySelector : Head, DisplaySelector<Rest...>
{
	static inline void setLed(bool on)
	{
		Head::setLed(on);
		DisplaySelector<Rest...>::setLed(on);
	}

	inline void displayCode(DisplayCode code, const char *device, const char *message)
	{
		Head::displayCode(code, device, message);
		DisplaySelector<Rest...>::displayCode(code, device, message);
	}

	inline void setup()
	{
		Head::setup();
		DisplaySelector<Rest...>::setup();
	}

	inline void initSdErrorHalt()
	{
		Head::initSdErrorHalt();
		DisplaySelector<Rest...>::initSdErrorHalt();
	}
};

template<typename Tail> struct DisplaySelector<Tail> : Tail
{
	static inline void setLed(bool on)
	{
		Tail::setLed(on);
	}

	inline void displayCode(DisplayCode code, const char *device, const char *message)
	{
		return Tail::displayCode(code, device, message);
	}

	inline void setup()
	{
		Tail::setup();
	}

	inline void initSdErrorHalt()
	{
		Tail::initSdErrorHalt();
	}
};

struct Display : DisplaySelector<OLED128x32Display, LedDisplay, LCD16x2Display, SerialDisplay>
{
	inline void setup()
	{
		DisplaySelector::setup();
	}

	inline void initSdErrorHalt()
	{
		DisplaySelector::initSdErrorHalt();
	}

	inline void displayCode(DisplayCode code, const char *device = nullptr, const char *message = nullptr)
	{
		DisplaySelector::displayCode(code, device, message);
	}
} Display;
