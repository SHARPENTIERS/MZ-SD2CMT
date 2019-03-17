#pragma once

#if HAS_IRREMOTE_INPUT

#include <IRremote.h>  // Sketch > Include library > Manage libraries > search: irremote

#define IR_RECV_PIN 55 // IR Pin

IRrecv			irRecv(IR_RECV_PIN);
decode_results	irResults;

struct IRRemoteInput
{
	static InputCode previous_code;

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
				break;
			case 0xFF10EF: // 7
				Serial.println(" 7");
				break;
			case 0xFF18E7: // 2
				Serial.println(" 5");
				break;
			case 0xFF22DD: // LEFT
				Serial.println(" LEFT");
				code = InputCode::left;
				break;
			case 0xFF30CF: // 4
				Serial.println(" 4");
				break;
			case 0xFF38C7: // 8
				Serial.println(" 8");
				break;
			case 0xFF42BD: // *
				Serial.println(" *");
				break;

				// 2			FF9867
				// DOWN			FFA857
				// 3			FFB04F
				// RIGHT		FFC23D

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
				break;
			case 0xFF6897: // 1
				Serial.println(" 1");
				break;
			case 0xFF7A85: // 6
				Serial.println(" 6");
				break;
			case 0xFF9867: // 2
				Serial.println(" 2");
				break;
			case 0xFFA857: // DOWN
				Serial.println(" DOWN");
				code = InputCode::down;
				break;
			case 0xFFB04F: // 3
				Serial.println(" 3");
				break;
			case 0xFFC23D: // RIGHT
				Serial.println(" RIGHT");
				code = InputCode::right;
				break;
			default:
				Serial.println(" Unknown");
				break;
			}

			bool result = code != previous_code;

			if (result) previous_code = code;

			return result;
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

#else
using IRRemoteInput = DummyInput;
#endif
