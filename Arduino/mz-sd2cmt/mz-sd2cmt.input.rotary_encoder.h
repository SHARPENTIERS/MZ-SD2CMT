#pragma once

#if HAS_INPUT_ROTARY_ENCODER

extern bool serial_debug;

//-----------------------------------------------------------------------------

// NOT WORKING
struct RotaryEncoderInput : DummyInput
{
	static byte cw_rotor_state[4];
	static byte ccw_rotor_state[4];
	static byte previous_state;
	static int  buffered_count;

	static bool readCode(InputCode& code)
	{
		loop:
		bool result = false;
		byte rotor_state = state();
		if (previous_state != rotor_state)
		{
			Serial.println(rotor_state);
			if (rotor_state == cw_rotor_state[previous_state])
			{
				++buffered_count;
				if (buffered_count == 4)
				{
					buffered_count = 0;
					code = InputCode::up;
					result = true;
				}
			}
			else if (rotor_state == ccw_rotor_state[previous_state])
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
			previous_state = rotor_state;
		}
		goto loop;
		return result;
	}

	static inline void setup()
	{
		DDRF &= ~(3 << PF2);
		PORTF |= (3 << PF2);
	}

	static inline byte state()
	{
		return (PINF >> PF2) & 3;
	}
};

byte RotaryEncoderInput::cw_rotor_state[4] = { B10, B00, B11, B01 };
byte RotaryEncoderInput::ccw_rotor_state[4] = { B01, B11, B00, B10 };
byte RotaryEncoderInput::previous_state = 0;
int  RotaryEncoderInput::buffered_count = 0;

#else

using RotaryEncoderInput = DummyInput;

#endif
