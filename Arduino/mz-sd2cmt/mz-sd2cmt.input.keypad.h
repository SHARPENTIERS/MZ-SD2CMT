#pragma once

#if HAS_INPUT_KEYPAD

#define HAS_INPUT_LCD16X2_KEYPAD_V1 1

extern bool serial_debug;

//-----------------------------------------------------------------------------

template< typename ...Inputs >
struct KeyPadInputSelector;

template<>
struct KeyPadInputSelector<>
{
	static constexpr unsigned long rate_time = 10*1000; /* 10 ms */
	static unsigned long           last_time;
	static InputCode               previous_code;
	static size_t                  repeated_code;
	static bool                    validate_code;
	static size_t                  repeat_count;

	static inline void setup()
	{
	}
};

template< typename ...Inputs >
struct KeyPadInputSelector : KeyPadInputSelector<>, InputReaderSelector< Inputs... >
{
	using KeyPadInputSelector<>::setup;

	static inline bool readCode(InputCode &code)
	{
		bool result = false;

		auto current_time = micros();
		if (current_time > last_time + rate_time)
		{
			last_time = current_time;

			validate_code = false;

			result = InputReaderSelector< Inputs... >::readCode(code);

			if (not validate_code)
			{
				repeat_count = 0;
			}
		}

		return result;
	}
};

//-----------------------------------------------------------------------------

#if HAS_INPUT_LCD16X2_KEYPAD_V1

struct LCD16x2KeyPadV1Input : KeyPadInputSelector<>
{
	static constexpr int delta = 5;

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

	static bool readCode(InputCode& code)
	{
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

		return result;
	}
};

#else

using LCD16x2KeyPadV1Input = DummyInput;

#endif

//-----------------------------------------------------------------------------


// Insert your other keypad models


//-----------------------------------------------------------------------------

unsigned long   KeyPadInputSelector<>::last_time = 0;
InputCode       KeyPadInputSelector<>::previous_code = InputCode::none;
size_t          KeyPadInputSelector<>::repeated_code = 0;
bool            KeyPadInputSelector<>::validate_code = false;
size_t          KeyPadInputSelector<>::repeat_count = 0;

//-----------------------------------------------------------------------------

// Insert your other keypad models in the selector list
using KeyPadInput = KeyPadInputSelector< LCD16x2KeyPadV1Input >;

#else

using KeyPadInput = DummyInput;

#endif
