#pragma once

#if HAS_INPUT_ROTARY_ENCODER

#include <avr/pgmspace.h>

extern bool serial_debug;

//-----------------------------------------------------------------------------

struct RotaryEncoderInput : DummyInput
{
	static constexpr bool USE_DEBOUNCER = true;
	static constexpr byte DEBOUNCER_LOOP = 8;
	static constexpr byte BUFFERED_COUNT = 4;

	static constexpr InputCode SWITCH_CODE = InputCode::select;
	static constexpr InputCode CW_CODE     = InputCode::up;
	static constexpr InputCode CCW_CODE    = InputCode::down;

	static constexpr auto *PORTn = reinterpret_cast<volatile uint8_t *>(&PORTF);
	static constexpr auto *PINn = reinterpret_cast<volatile uint8_t *>(&PINF);
	static constexpr auto *DDRn = reinterpret_cast<volatile uint8_t *>(&DDRF);
	static constexpr byte BASEPIN = PF2;

	static constexpr byte SWITCH = 4;
	static constexpr byte CHANNEL_A = 2;
	static constexpr byte CHANNEL_B = 1;

	static const byte cw_rotor_state[4];
	static const byte ccw_rotor_state[4];

	static byte       previous_state;
	static int8_t     buffered_count;

	static bool readCode(InputCode& code)
	{
		bool result = false;
		byte rotor_state = debouncedState();
		if (previous_state != rotor_state)
		{
			if (!(rotor_state & SWITCH))
			{
				code = SWITCH_CODE;
				result = true;
			}
			else
			{
				byte new_mask = rotor_state & (CHANNEL_A | CHANNEL_B);
				byte old_mask = previous_state & (CHANNEL_A | CHANNEL_B);
				if (new_mask == cw_rotor_state[old_mask])
				{
					++buffered_count;
					if (buffered_count == BUFFERED_COUNT)
					{
						buffered_count = 0;
						code = CW_CODE;
						result = true;
					}
				}
				else if (new_mask == ccw_rotor_state[old_mask])
				{
					--buffered_count;
					if (buffered_count == -BUFFERED_COUNT)
					{
						buffered_count = 0;
						code = CCW_CODE;
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
		*DDRn &= ~((CHANNEL_A | CHANNEL_B | SWITCH) << BASEPIN);
		// Set CHANNEL A and CHANNEL B as pull down input.
		*PORTn &= ~((CHANNEL_A | CHANNEL_B) << BASEPIN);
		// Set SWITCH as pull up input.
		*PORTn |=  ((SWITCH) << BASEPIN);

		if (serial_debug)
			Serial.println(F("Input device: rotary encoder."));
	}

	static inline byte state()
	{
		return (*PINn >> BASEPIN) & (CHANNEL_A | CHANNEL_B | SWITCH);
	}

	static inline byte debouncedState()
	{
		byte result_state = state();
		if (USE_DEBOUNCER)
		{
			for (byte count = 0; count < DEBOUNCER_LOOP; ++count)
			{
				byte debounced_state = state();
				if (debounced_state != result_state)
				{
					result_state = debounced_state;
					count = 0;
				}
			}
		}
		return result_state;
	}
};

const byte RotaryEncoderInput::cw_rotor_state[4] = { B10, B00, B11, B01 };
const byte RotaryEncoderInput::ccw_rotor_state[4] = { B01, B11, B00, B10 };
byte       RotaryEncoderInput::previous_state = 0;
int8_t     RotaryEncoderInput::buffered_count = 0;

#else

using RotaryEncoderInput = DummyInput;

#endif
