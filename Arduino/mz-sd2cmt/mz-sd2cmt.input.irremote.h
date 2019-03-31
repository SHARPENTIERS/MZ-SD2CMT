#pragma once

#if HAS_INPUT_IRREMOTE

#define HAS_INPUT_KEYES_IRREMOTE 1

#include <IRremote.h>  // Sketch > Include library > Manage libraries > search: irremote

extern bool serial_debug;

//-----------------------------------------------------------------------------

#define IR_RECV_PIN 55 // IR Pin

template< typename ...Inputs >
struct IRRemoteInputSelector;

template<>
struct IRRemoteInputSelector<>
{
	static IRrecv	      recv;
	static decode_results results;

	static InputCode      previous_code;
	static size_t         repeated_code;
	static bool           accepted_code;
	static bool           validate_code;
	static size_t         repeat_count;

	static inline void setup()
	{
		recv.enableIRIn();
	}
};

template< typename ...Inputs >
struct IRRemoteInputSelector : IRRemoteInputSelector<>, InputReaderSelector< Inputs... >
{
	using IRRemoteInputSelector<>::setup;

	static inline bool readCode(InputCode &code)
	{
		bool result = false;

		if (recv.decode(&results))
		{
			if (serial_debug)
			{
				uint32_t value = results.value;

				Serial.print(value, HEX);
			}

			validate_code = false;

			result = InputReaderSelector< Inputs... >::readCode(code);

			if (not validate_code)
			{
				repeat_count = 0;
				accepted_code = false;
			}

			recv.resume();
		}


		return result;
	}
};

//-----------------------------------------------------------------------------

#if HAS_INPUT_KEYES_IRREMOTE

// Keyes / Xinda IR Remote Control
//////////////////////////////////
// OK			FF02FD
// 7			FF10EF
// 5			FF18E7
// LEFT			FF22DD
// 4			FF30CF
// 8			FF38C7
// *			FF42BD
// 0			FF4AB5
// #			FF52AD
// 9			FF5AA5
// UP			FF629D
// 1			FF6897
// 6			FF7A85
// 2			FF9867
// DOWN			FFA857
// 3			FFB04F
// RIGHT		FFC23D
struct KeyesIRRemoteInput : IRRemoteInputSelector<>
{
	static bool readCode(InputCode& code)
	{
		uint32_t value = results.value;

		Serial.println(results.decode_type, HEX);

		switch (value)
		{
		case 0xFF02FD: // OK
			if (serial_debug) Serial.println(" OK");
			code = InputCode::select;
			goto input_code_accepted;
		case 0xFF10EF: // 7
			if (serial_debug) Serial.println(" 7");
			break;
		case 0xFF18E7: // 2
			if (serial_debug) Serial.println(" 5");
			break;
		case 0xFF22DD: // LEFT
			if (serial_debug) Serial.println(" LEFT");
			code = InputCode::left;
			goto input_code_accepted;
		case 0xFF30CF: // 4
			if (serial_debug) Serial.println(" 4");
			break;
		case 0xFF38C7: // 8
			if (serial_debug) Serial.println(" 8");
			break;
		case 0xFF42BD: // *
			if (serial_debug) Serial.println(" *");
			break;
		case 0xFF4AB5: // 0
			if (serial_debug) Serial.println(" 0");
			break;
		case 0xFF52AD: // #
			if (serial_debug) Serial.println(" #");
			break;
		case 0xFF5AA5: // 9
			if (serial_debug) Serial.println(" 9");
			break;
		case 0xFF629D: // UP
			if (serial_debug) Serial.println(" UP");
			code = InputCode::up;
			goto input_code_accepted;
		case 0xFF6897: // 1
			if (serial_debug) Serial.println(" 1");
			break;
		case 0xFF7A85: // 6
			if (serial_debug) Serial.println(" 6");
			break;
		case 0xFF9867: // 2
			if (serial_debug) Serial.println(" 2");
			break;
		case 0x00FFA857: // DOWN
			if (serial_debug) Serial.println(" DOWN");
			code = InputCode::down;
			goto input_code_accepted;
		case 0x00FFB04F: // 3
			if (serial_debug) Serial.println(" 3");
			break;
		case 0x00FFC23D: // RIGHT
			if (serial_debug) Serial.println(" RIGHT");
			code = InputCode::right;
			goto input_code_accepted;
		case 0xFFFFFFFF: // Repeat previous code
			if (serial_debug) Serial.println(" Repeated");
			if (accepted_code)
			{
				size_t mask =
					(repeat_count < (4* 0 + 8*5)) ?8:
					(repeat_count < (4*15 + 8*5)) ?4:
						                            2;
				if (mask > 2)
					++repeat_count;
				if (++repeated_code & mask) // Slow down the repeat rate
				{
					validate_code = true;
					repeated_code = 0;
					code = previous_code;
					return true;
				}
			}
			validate_code = true;
			return false;
		default:
			if (serial_debug) Serial.println(" Unknown");
			break;
		input_code_accepted:
			accepted_code = true;
			previous_code = code;
			validate_code = true;
			repeat_count = 0;
			return true;
		}

		return false;
	}
};

#else

using KeyesIRRemoteInput = DummyInput;

#endif // HAS_INPUT_KEYES_IRREMOTE

//-----------------------------------------------------------------------------


// Insert your other IR remote codes here in the same model as KeyesIRRemoteInput


//-----------------------------------------------------------------------------

IRrecv			IRRemoteInputSelector<>::recv(IR_RECV_PIN);
decode_results  IRRemoteInputSelector<>::results;
InputCode       IRRemoteInputSelector<>::previous_code = InputCode::none;
size_t          IRRemoteInputSelector<>::repeated_code = 0;
bool            IRRemoteInputSelector<>::accepted_code = false;
bool            IRRemoteInputSelector<>::validate_code = false;
size_t          IRRemoteInputSelector<>::repeat_count = 0;

//-----------------------------------------------------------------------------

// Insert your other IR remote models in the selector list
using IRRemoteInput = IRRemoteInputSelector< KeyesIRRemoteInput >;

#else

using IRRemoteInput = DummyInput;

#endif // HAS_INPUT_IRREMOTE