#pragma once

#if HAS_IRREMOTE_INPUT

#include <IRremote.h>  // Sketch > Include library > Manage libraries > search: irremote

#define IR_RECV_PIN 55 // IR Pin

IRrecv			irRecv(IR_RECV_PIN);
decode_results	irResults;

struct IRRemoteInput
{
	static InputCode previous_code;
	static size_t    repeated_code;
	static bool      accepted_code;
	static size_t    repeat_count;

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

	static bool readCode(InputCode& code)
	{
		if (irRecv.decode(&irResults))
		{
			uint32_t value = irResults.value;

			Serial.print(value, HEX);

			irRecv.resume();

			switch (value)
			{
			case 0xFF02FD: // OK
				Serial.println(" OK");
				code = InputCode::select;
				goto input_code_accepted;
			case 0xFF10EF: // 7
				Serial.println(" 7");
				break;
			case 0xFF18E7: // 2
				Serial.println(" 5");
				break;
			case 0xFF22DD: // LEFT
				Serial.println(" LEFT");
				code = InputCode::left;
				goto input_code_accepted;
			case 0xFF30CF: // 4
				Serial.println(" 4");
				break;
			case 0xFF38C7: // 8
				Serial.println(" 8");
				break;
			case 0xFF42BD: // *
				Serial.println(" *");
				break;
			case 0xFF4AB5: // 0
				Serial.println(" 0");
				break;
			case 0xFF52AD: // #
				Serial.println(" #");
				break;
			case 0xFF5AA5: // 9
				Serial.println(" 9");
				break;
			case 0xFF629D: // UP
				Serial.println(" UP");
				code = InputCode::up;
				goto input_code_accepted;
			case 0xFF6897: // 1
				Serial.println(" 1");
				break;
			case 0xFF7A85: // 6
				Serial.println(" 6");
				break;
			case 0xFF9867: // 2
				Serial.println(" 2");
				break;
			case 0x00FFA857: // DOWN
				Serial.println(" DOWN");
				code = InputCode::down;
				goto input_code_accepted;
			case 0x00FFB04F: // 3
				Serial.println(" 3");
				break;
			case 0x00FFC23D: // RIGHT
				Serial.println(" RIGHT");
				code = InputCode::right;
				goto input_code_accepted;
			case 0xFFFFFFFF: // Repeat previous code
				Serial.println(" Repeated");
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
						repeated_code = 0;
						code = previous_code;
						return true;
					}
				}
				return false;
			default:
				Serial.println(" Unknown");
				break;
			input_code_accepted:
				accepted_code = true;
				previous_code = code;
				repeat_count = 0;
				return true;
			}

			repeat_count = 0;
			accepted_code = false;
		}

		return false;
	}

	static void setup()
	{
		// TODO:
		// possibly reading a specific file from the SD or reading EEPROM
		// to get the right values and their associated codes.
		irRecv.enableIRIn();
	}

	static void configure()
	{
		// TODO:
		// ask via Serial to press a specific button to determine its analogical value. 
	}
};

InputCode IRRemoteInput::previous_code = InputCode::none;
size_t    IRRemoteInput::repeated_code = 0;
bool      IRRemoteInput::accepted_code = false;
size_t    IRRemoteInput::repeat_count = 0;

#else
using IRRemoteInput = DummyInput;
#endif
