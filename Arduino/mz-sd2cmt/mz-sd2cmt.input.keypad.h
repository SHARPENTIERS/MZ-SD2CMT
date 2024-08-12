#pragma once

#define HAS_INPUT_LCD16X2_KEYPAD_V1 1
#define HAS_INPUT_TTP223_KEYPAD 1

extern bool serial_debug;

//-----------------------------------------------------------------------------

struct LCD16x2KeyPadV1Input : DummyInput
{
	struct Config
	{
		bool enabled = !!HAS_INPUT_LCD16X2_KEYPAD_V1;
		// BVx sorted by value!
		int BV0 = 0x0000;
		int BV1 = 0x0063;
		int BV2 = 0x0101;
		int BV3 = 0x019A;
		int BV4 = 0x0280;
	} cfg;

	static constexpr int delta = 5;
	static constexpr unsigned long rate_time = 10 * 1000; /* 10 ms */

	static constexpr InputCode IC0 = InputCode::right;
	static constexpr InputCode IC1 = InputCode::up;
	static constexpr InputCode IC2 = InputCode::down;
	static constexpr InputCode IC3 = InputCode::left;
	static constexpr InputCode IC4 = InputCode::select;

	unsigned long                  last_time = 0;
	InputCode                      previous_code = InputCode::none;
	unsigned char                  repeated_code = 0;
	unsigned int                   repeat_count = 0;

	constexpr inline int lower(int value) const
	{
		return value - delta;
	}
	constexpr inline int upper(int value) const
	{
		return value + delta;
	}

	bool readCode(InputCode &code)
	{
		bool result = false;

		if (cfg.enabled)
		{
			auto current_time = micros();
			if (current_time > last_time + rate_time)
			{
				last_time = current_time;

				int value = analogRead(0);

				// Binary search of:
				//   if k exists when the given value is inside ]lower(BVk), upper(BVk)[ then get ICk else get none. 
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
					(value < upper(cfg.BV2))
					/**/ ? (value < lower(cfg.BV2))
					/*******/ ? (value < upper(cfg.BV1))
					/************/ ? (value < lower(cfg.BV1))
					/*****************/ ? (value < upper(cfg.BV0))
					/**********************/ ?					IC0
					/**********************/ :					InputCode::none
					/*****************/ :						IC1
					/************/ :							InputCode::none
					/*******/ :									IC2
					/**/ : (value < upper(cfg.BV3))
					/*******/ ? (value < lower(cfg.BV3))
					/************/ ?							InputCode::none
					/************/ :							IC3
					/*******/ : (value < upper(cfg.BV4))
					/************/ ? (value < lower(cfg.BV4))
					/*****************/ ?						InputCode::none
					/*****************/ :						IC4
					/************/ :							InputCode::none
					;

				result = received_code != InputCode::none;
				if (result)
				{
					if (received_code != previous_code)
					{
						previous_code = received_code;
						repeat_count = 0;
						code = received_code;
					}
					else
					{
						result = (received_code == InputCode::up) or (received_code == InputCode::down);
						if (result)
						{
							unsigned char mask =
								(repeat_count < (16 * 0  + 32 * 5)) ? 32 :
								(repeat_count < (16 * 15 + 32 * 5)) ? 16 :
								/* else                            */  8 ;
							if (mask > 8)
								++repeat_count;
							result = 0 != (++repeated_code & mask);
							if (result) // Slow down the repeat rate
							{
								repeated_code = 0;
								code = received_code;
							}
						}
					}
					if (serial_debug and code != InputCode::none) Serial.println(value, HEX);
				}
				else
				{
					repeat_count = 0;
					previous_code = InputCode::none;
				}
				if (!result)
				{
					code = InputCode::none;
					repeat_count = 0;
				}
			}
		}

		return result;
	}

	inline void setup()
	{
		if (Storage.configure(cfg, "/.config/LCD_KEYPAD_SHIELD.input"))
		{
			Serial.println(F("Input device: LCD16X2 KeyPad."));
			Serial.print(F("  +00 Enabled: ")); Serial.println(cfg.enabled);
			Serial.print(F("  +01 Button Voltage [RIGHT ]: ")); Serial.println(cfg.BV0, HEX);
			Serial.print(F("  +04 Button Voltage [UP    ]: ")); Serial.println(cfg.BV1, HEX);
			Serial.print(F("  +07 Button Voltage [DOWN  ]: ")); Serial.println(cfg.BV2, HEX);
			Serial.print(F("  +0A Button Voltage [LEFT  ]: ")); Serial.println(cfg.BV3, HEX);
			Serial.print(F("  +0D Button Voltage [SELECT]: ")); Serial.println(cfg.BV4, HEX);
		}
	}
};

//-----------------------------------------------------------------------------

struct TTP223KeyPadInput : DummyInput
{
	struct Config
	{
		bool enabled = !!HAS_INPUT_TTP223_KEYPAD;
		InputCode IC0 = InputCode::select;
		InputCode IC1 = InputCode::left;
		InputCode IC2 = InputCode::up;
		InputCode IC3 = InputCode::down;
		InputCode IC4 = InputCode::right;
		InputCode IC5 = InputCode::none;
		InputCode IC6 = InputCode::none;
		InputCode IC7 = InputCode::none;
		byte active = 0b00000000; // active signal on 0/1 (by default on 0)
	} cfg;

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
		if (cfg.enabled)
		{
			byte new_state = PINK & ((P0|P1|P2|P3|P4|P5|P6|P7) ^ ~cfg.active);
			if (new_state != old_state)
			{
				if (serial_debug)
				{
					Serial.print(F("TTP223: "));
					Serial.print((new_state & P0) ? "|" : "-");
					Serial.print((new_state & P1) ? "|" : "-");
					Serial.print((new_state & P2) ? "|" : "-");
					Serial.print((new_state & P3) ? "|" : "-");
					Serial.print((new_state & P4) ? "|" : "-");
					Serial.print((new_state & P5) ? "|" : "-");
					Serial.print((new_state & P6) ? "|" : "-");
					Serial.print((new_state & P7) ? "|" : "-");
				}
				/**/ if (new_state & P0) code = cfg.IC0;
				else if (new_state & P1) code = cfg.IC1;
				else if (new_state & P2) code = cfg.IC2;
				else if (new_state & P3) code = cfg.IC3;
				else if (new_state & P4) code = cfg.IC4;
				else if (new_state & P5) code = cfg.IC5;
				else if (new_state & P6) code = cfg.IC6;
				else if (new_state & P7) code = cfg.IC7;
				old_state = new_state;
			}
		}
	}

	inline void setup()
	{
		if (Storage.configure(cfg, "/.config/TTP223_KEYPAD.input"))
		{
			DDRK &= ~(P0 | P1 | P2 | P3 | P4 | P5);
			PORTK &= ~(P0 | P1 | P2 | P3 | P4 | P5);

			Serial.println(F("Input device: TTP223 KeyPad."));
			Serial.print(F("  +00 Enabled: ")); Serial.println(cfg.enabled);
			Serial.print(F("  +01 Input Code #0: ")); Serial.println(static_cast<int>(cfg.IC0));
			Serial.print(F("  +02 Input Code #1: ")); Serial.println(static_cast<int>(cfg.IC1));
			Serial.print(F("  +03 Input Code #2: ")); Serial.println(static_cast<int>(cfg.IC2));
			Serial.print(F("  +04 Input Code #3: ")); Serial.println(static_cast<int>(cfg.IC3));
			Serial.print(F("  +05 Input Code #4: ")); Serial.println(static_cast<int>(cfg.IC4));
			Serial.print(F("  +06 Input Code #5: ")); Serial.println(static_cast<int>(cfg.IC5));
			Serial.print(F("  +07 Input Code #6: ")); Serial.println(static_cast<int>(cfg.IC6));
			Serial.print(F("  +08 Input Code #7: ")); Serial.println(static_cast<int>(cfg.IC7));
			Serial.print(F("  +09 Active: ")); Serial.println(cfg.active, BIN);
		}
	}
};

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
