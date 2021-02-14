#pragma once

#if HAS_INPUT_KEYPAD

#define HAS_INPUT_LCD16X2_KEYPAD_V1 1
#define HAS_INPUT_TTP223_KEYPAD 1

extern bool serial_debug;

//-----------------------------------------------------------------------------

#if HAS_INPUT_LCD16X2_KEYPAD_V1

struct LCD16x2KeyPadV1Input : DummyInput
{
	static constexpr unsigned long rate_time = 10 * 1000; /* 10 ms */

	unsigned long                  last_time = 0;
	InputCode                      previous_code = InputCode::none;
	size_t                         repeated_code = 0;
	bool                           validate_code = false;
	size_t                         repeat_count = 0;

	static constexpr int           delta = 5;

	// sorted by value
	static constexpr int BV0 = 0x0000; // TODO: make it configurable at run-time?
	static constexpr int BV1 = 0x0063; // TODO: make it configurable at run-time?
	static constexpr int BV2 = 0x0101; // TODO: make it configurable at run-time?
	static constexpr int BV3 = 0x019A; // TODO: make it configurable at run-time?
	static constexpr int BV4 = 0x0280; // TODO: make it configurable at run-time?

	static constexpr InputCode IC0 = InputCode::right;  // TODO: make it configurable at run-time?
	static constexpr InputCode IC1 = InputCode::up;     // TODO: make it configurable at run-time?
	static constexpr InputCode IC2 = InputCode::down;   // TODO: make it configurable at run-time?
	static constexpr InputCode IC3 = InputCode::left;   // TODO: make it configurable at run-time?
	static constexpr InputCode IC4 = InputCode::select; // TODO: make it configurable at run-time?
	
	static constexpr int lower(int value)
	{
		return value - delta;
	}
	static constexpr int upper(int value)
	{
		return value + delta;
	}

	bool readCode(InputCode &code)
	{
		bool result = false;

		auto current_time = micros();
		if (current_time > last_time + rate_time)
		{
			last_time = current_time;

			validate_code = false;

			int value = analogRead(0);

			// Binary search of:
			//   if k exists when the given value is inside ]lower(BVk), upper(BVk)[ then get BCk else get none. 
			//
			//               BV2
			//              /   \
			//           BV1     BV3
			//          /           \
			//       BV0             BV4
			//       --- --- --- --- ---
			//       IC0 IC1 IC2 IC3 IC4
			auto received_code =
				// Condition								Result
				// ---------------------------------------  ---------------
				(value < upper(BV2))
				/**/ ? (value < lower(BV2))
				/*******/ ? (value < upper(BV1))
				/************/ ? (value < lower(BV1))
				/*****************/ ? (value < upper(BV0))
				/**********************/ ?					IC0
				/**********************/ :					InputCode::none
				/*****************/ :						IC1
				/************/ :							InputCode::none
				/*******/ :									IC2
				/**/ : (value < upper(BV3))
				/*******/ ? (value < lower(BV3))
				/************/ ?							InputCode::none
				/************/ :							IC3
				/*******/ : (value < upper(BV4))
				/************/ ? (value < lower(BV4))
				/*****************/ ?						InputCode::none
				/*****************/ :						IC4
				/************/ :							InputCode::none
				;

			bool result = received_code != InputCode::none;
			if (result)
			{
				if (received_code != previous_code)
				{
					validate_code = true;
					previous_code = received_code;
					repeat_count = 0;
					code = received_code;
				}
				else
				{
					size_t mask =
						(repeat_count < (16 * 0  + 32 * 5)) ? 32 :
						(repeat_count < (16 * 15 + 32 * 5)) ? 16 :
															   8 ;
					if (mask > 8)
						++repeat_count;
					result = 0 != (++repeated_code & mask);
					if (result) // Slow down the repeat rate
					{
						validate_code = true;
						repeated_code = 0;
						code = previous_code;
					}
					else
					{
						code = InputCode::none;
						repeat_count = 0;
					}
				}
			}
			else
			{
				repeat_count = 0;
				code = received_code;
			}

			if (result)
			{
				if (serial_debug) Serial.println(value, HEX);
			}
		}

		return result;
	}

	inline void setup()
	{
	}
};

#else

using LCD16x2KeyPadV1Input = DummyInput;

#endif

//-----------------------------------------------------------------------------

#if HAS_INPUT_TTP223_KEYPAD

struct TTP223KeyPadInput : DummyInput
{
	static constexpr auto *PORTn = reinterpret_cast<volatile uint8_t *>(&PORTK);
	static constexpr auto *PINn = reinterpret_cast<volatile uint8_t *>(&PINK);
	static constexpr auto *DDRn = reinterpret_cast<volatile uint8_t *>(&DDRK);

	static constexpr auto P0 = 1 << PK0;
	static constexpr auto P1 = 1 << PK1;
	static constexpr auto P2 = 1 << PK2;
	static constexpr auto P3 = 1 << PK3;
	static constexpr auto P4 = 1 << PK4;
	static constexpr auto P5 = 1 << PK5;
	static constexpr auto P6 = 1 << PK6;
	static constexpr auto P7 = 1 << PK7;

	byte old_state = 0;

	bool readCode(InputCode &code)
	{
		byte new_state = *PINn & (P0|P1|P2|P3|P4|P5);
		if (new_state != old_state)
		{
			Serial.print(F("TTP223: "));
			Serial.print((new_state & P0) ? "|" : "-");
			Serial.print((new_state & P1) ? "|" : "-");
			Serial.print((new_state & P2) ? "|" : "-");
			Serial.print((new_state & P3) ? "|" : "-");
			Serial.print((new_state & P4) ? "|" : "-");
			Serial.print((new_state & P5) ? "|" : "-");
			old_state = new_state;
		}
	}

	inline void setup()
	{
		*DDRn &= ~(P0 | P1 | P2 | P3 | P4 | P5);
		*PORTn &= ~(P0 | P1 | P2 | P3 | P4 | P5);

		Serial.println(F("Input device: TTP223 KeyPad."));
	}
};

#else

using TTP223KeyPadInput = DummyInput;

#endif

//-----------------------------------------------------------------------------


// Insert your other keypad models


//-----------------------------------------------------------------------------

// Insert your other keypad models in the selector list
struct KeyPadInput : InputReaderSelector<LCD16x2KeyPadV1Input, TTP223KeyPadInput>
{
	inline void setup()
	{
		InputReaderSelector::setup();
	}

	inline bool readCode(InputCode &code)
	{
		return InputReaderSelector::readCode(code);
	}
};

#else

using KeyPadInput = DummyInput;

#endif
