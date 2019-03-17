#pragma once

#define HAS_LCD16X2_INPUT 1
#define HAS_IRREMOTE_INPUT 1

enum class InputCode : int8_t
{
	none,

	left,
	up,
	right,
	down,

	select,

	digit0,
	digit1,
	digit2,
	digit3,
	digit4,
	digit5,
	digit6,
	digit7,
	digit8,
	digit9,

	star,
	sharp
};

struct DummyInput
{
	static bool readCode(InputCode& code) { return true; }
	static void setup() {}
	static void configure() {}
};

#include "mz-sd2cmt.input.irremote.h"
#include "mz-sd2cmt.input.lcd16x2.h"

template<typename Head, typename... Rest> struct InputReaderSelector
{
	static bool readCode(InputCode &code)
	{
		return Head::readCode(code) or InputReaderSelector<Rest...>::readCode(code);
	}

	static void setup()
	{
		Head::setup();
		InputReaderSelector<Rest...>::setup();
	}
};

template<typename Tail> struct InputReaderSelector< Tail >
{
	static bool readCode(InputCode &code)
	{
		return Tail::readCode(code);
	}

	static void setup()
	{
		Tail::setup();
	}
};

using InputReader = InputReaderSelector< LCD16x2Input, IRRemoteInput >;
