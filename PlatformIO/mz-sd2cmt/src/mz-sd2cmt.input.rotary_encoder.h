#pragma once

#include <avr/pgmspace.h>

extern bool serial_debug;

//-----------------------------------------------------------------------------

struct RotaryEncoderInput : DummyInput
{
	struct Config
	{
		bool enabled = true;
		bool use_debouncer = true;
		byte debouncer_loop = 16;
	} cfg;

	static constexpr byte BUFFERED_COUNT = 4;

	static constexpr InputCode SWITCH_CODE = InputCode::select;
	static constexpr InputCode CW_CODE     = InputCode::up;
	static constexpr InputCode CCW_CODE    = InputCode::down;

	static constexpr byte BASEPIN = PF2;

	static constexpr byte SWITCH = 4;
	static constexpr byte CHANNEL_A = 2;
	static constexpr byte CHANNEL_B = 1;

	static const byte cw_rotor_state[4];
	static const byte ccw_rotor_state[4];

	byte       previous_state = 0;
	int8_t     buffered_count = 0;

	bool readCode(InputCode& code)
	{
		bool result = false;
		if (cfg.enabled)
		{
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
		}
		return result;
	}

	inline void setup()
	{
		if (Storage.configure(cfg, "/.config/ROTARY_ENCODER.input"))
		{
			// Set CHANNEL A, CHANNEL B and SWITCH pins as input
			DDRF &= ~((CHANNEL_A | CHANNEL_B | SWITCH) << BASEPIN);
			// Set CHANNEL A and CHANNEL B as pull down input.
			PORTF &= ~((CHANNEL_A | CHANNEL_B) << BASEPIN);
			// Set SWITCH as pull up input.
			PORTF |=  ((SWITCH) << BASEPIN);

			Serial.println(F("Input device: rotary encoder."));
		}
	}

	static inline byte state()
	{
		return (PINF >> BASEPIN) & (CHANNEL_A | CHANNEL_B | SWITCH);
	}

	inline byte debouncedState()
	{
		byte result_state = state();
		if (cfg.use_debouncer)
		{
			for (byte count = 0; count < cfg.debouncer_loop; ++count)
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
