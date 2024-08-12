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
	inline bool readCode(InputCode &code) { return false; }
	inline void setup() {}
};

template<typename Head, typename... Rest> struct InputReaderSelector : Head, InputReaderSelector<Rest...>
{
	inline bool readCode(InputCode &code)
	{
		return Head::readCode(code) or InputReaderSelector<Rest...>::readCode(code);
	}

	inline void setup()
	{
		Head::setup();
		InputReaderSelector<Rest...>::setup();
	}
};

template<typename Tail> struct InputReaderSelector<Tail> : Tail
{
	inline bool readCode(InputCode &code)
	{
		return Tail::readCode(code);
	}

	inline void setup()
	{
		Tail::setup();
	}
};

#include "mz-sd2cmt.input.keypad.h"
#include "mz-sd2cmt.input.irremote.h"
#include "mz-sd2cmt.input.rotary_encoder.h"

struct InputReader : InputReaderSelector<RotaryEncoderInput, KeyPadInput, IRRemoteInput>
{
	inline void setup()
	{
		InputReaderSelector::setup();
	}

	inline bool readCode(InputCode &code)
	{
		return InputReaderSelector::readCode(code);
	}
} InputReader;
