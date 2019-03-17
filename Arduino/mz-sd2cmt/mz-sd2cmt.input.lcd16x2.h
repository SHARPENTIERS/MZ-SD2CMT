#pragma once

#if HAS_LCD16X2_INPUT
struct LCD16x2Input
{
	static InputCode previous_code;

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
		//   if k exists when the given value is inside [under(BVk), upper(BVk)] then get BCk else get none. 
		//
		//               BV2
		//              /   \
		//           BV1     BV3
		//          /           \
		//       BV0             BV4
		//       --- --- --- --- ---
		//       IC0 IC1 IC2 IC3 IC4
		code =
			// Condition								Result
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

		bool result = code != previous_code;

		if (result)
		{
			Serial.println(value, HEX);
			previous_code = code;
		}

		return result;
	}

	static void setup()
	{
		// TODO:
		// possibly reading a specific file from the SD or reading EEPROM
		// to get the right values and their associated codes.
	}

	static void configure()
	{
		// TODO:
		// ask via Serial to press a specific button to determine its analogical value. 
	}
};

InputCode LCD16x2Input::previous_code = InputCode::none;

#else
using LCD16x2Input = DummyInput;
#endif
