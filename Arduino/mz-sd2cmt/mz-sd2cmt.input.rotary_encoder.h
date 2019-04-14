#pragma once

#if HAS_INPUT_ROTARY_ENCODER

#include <avr/pgmspace.h>

extern bool serial_debug;

//-----------------------------------------------------------------------------

struct RotaryEncoderInput : DummyInput
{
	static constexpr auto &OPORT = reinterpret_cast<volatile uint8_t &>(PORTF);
	static constexpr auto &IPORT = reinterpret_cast<volatile uint8_t &>(PINF);
	static constexpr auto &DPORT = reinterpret_cast<volatile uint8_t &>(DDRF);
	static constexpr byte BASEPIN = PF2;

	static constexpr byte SWITCH = 4;
	static constexpr byte CHANNEL_A = 2;
	static constexpr byte CHANNEL_B = 1;

	static const byte cw_rotor_state[4];
	static const byte ccw_rotor_state[4];

	static byte previous_state;
	static int  buffered_count;

	static bool readCode(InputCode& code)
	{
		bool result = false;
		byte rotor_state = state();
		if (previous_state != rotor_state)
		{
			if (!(rotor_state & SWITCH))
			{
				code = InputCode::select;
				result = true;
			}
			else
			{
				byte new_mask = rotor_state & (CHANNEL_A | CHANNEL_B);
				byte old_mask = previous_state & (CHANNEL_A | CHANNEL_B);
				if (new_mask == cw_rotor_state[old_mask])
				{
					++buffered_count;
					if (buffered_count == 4)
					{
						buffered_count = 0;
						code = InputCode::up;
						result = true;
					}
				}
				else if (new_mask == ccw_rotor_state[old_mask])
				{
					--buffered_count;
					if (buffered_count == -4)
					{
						buffered_count = 0;
						code = InputCode::down;
						result = true;
					}
				}
				else
				{
					buffered_count = 0;
				}
			}
			previous_state = rotor_state;
		}
		return result;
	}

	static inline void setup()
	{
		// Set CHANNEL A, CHANNEL B and SWITCH pins as input
		DPORT &= ~((CHANNEL_A | CHANNEL_B | SWITCH) << BASEPIN);
		// Set CHANNEL A and CHANNEL B as pull down input.
		OPORT &= ~((CHANNEL_A | CHANNEL_B) << BASEPIN);
		// Set SWITCH as pull up input.
		OPORT |=  ((SWITCH) << BASEPIN);

		if (serial_debug)
			Serial.println(F("Input device: rotary encoder."));
	}

	static inline byte state()
	{
		return (IPORT >> BASEPIN) & (CHANNEL_A | CHANNEL_B | SWITCH);
	}
};

const byte RotaryEncoderInput::cw_rotor_state[4] = { B10, B00, B11, B01 };
const byte RotaryEncoderInput::ccw_rotor_state[4] = { B01, B11, B00, B10 };
byte RotaryEncoderInput::previous_state = 0;
int  RotaryEncoderInput::buffered_count = 0;

#else

using RotaryEncoderInput = DummyInput;

#endif
