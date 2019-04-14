#pragma once

#define HAS_INPUT_KEYPAD 1
#define HAS_INPUT_IRREMOTE 1
#define HAS_INPUT_ROTARY_ENCODER 1

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
	static inline bool readCode(InputCode& code) { return false; }
	static inline void setup() {}
	static inline void configure() {}
};

template<typename Head, typename... Rest> struct InputReaderSelector
{
	static inline bool readCode(InputCode &code)
	{
		return Head::readCode(code) or InputReaderSelector<Rest...>::readCode(code);
	}

	static inline void setup()
	{
		Head::setup();
		InputReaderSelector<Rest...>::setup();
	}
};

template<typename Tail> struct InputReaderSelector< Tail >
{
	static inline bool readCode(InputCode &code)
	{
		return Tail::readCode(code);
	}

	static inline void setup()
	{
		Tail::setup();
	}
};

#include "mz-sd2cmt.input.keypad.h"
#include "mz-sd2cmt.input.irremote.h"
#include "mz-sd2cmt.input.rotary_encoder.h"

using InputReader = InputReaderSelector< RotaryEncoderInput, KeyPadInput, IRRemoteInput >;
