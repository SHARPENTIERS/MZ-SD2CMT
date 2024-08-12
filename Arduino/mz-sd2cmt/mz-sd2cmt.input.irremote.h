#pragma once

#define HAS_INPUT_KEYES_IRREMOTE 1

#include <IRremote.hpp> // Sketch > Include library > Manage libraries > search: irremote https://github.com/Arduino-IRremote/Arduino-IRremote

extern bool serial_debug;

//-----------------------------------------------------------------------------

enum IRRemoteCodeState
{
	IR_REMOTE_CODE_NONE,
	IR_REMOTE_CODE_ACCEPTED,
	IR_REMOTE_CODE_REPEATED
};

//-----------------------------------------------------------------------------

template<typename Head, typename... Rest> struct IRRemoteInputSelector : Head, IRRemoteInputSelector<Rest...>
{
	static constexpr bool enabled = Head::enabled or IRRemoteInputSelector<Rest...>::enabled;

	template<typename Derived>
	static inline IRRemoteCodeState readCode(Derived* that, InputCode &code)
	{
		auto state = Head::readCode(that, code);
		if (state == IR_REMOTE_CODE_NONE)
		{
			state = IRRemoteInputSelector<Rest...>::readCode(that, code);
		}
		return state;
	}
};

template<typename Tail> struct IRRemoteInputSelector<Tail> : Tail
{
	static constexpr bool enabled = Tail::enabled;

	template<typename Derived>
	static inline IRRemoteCodeState readCode(Derived* that, InputCode &code)
	{
		return Tail::readCode(that, code);
	}
};

//-----------------------------------------------------------------------------

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
struct KeyesIRRemoteInput
{
	static constexpr bool enabled = !!HAS_INPUT_KEYES_IRREMOTE;

	static IRRemoteCodeState readCode(uint32_t value, InputCode &code)
	{
		if (enabled)
		{
			switch (value)
			{
			case 0xFF02FD: // OK
				if (serial_debug) Serial.println(" OK");
				code = InputCode::select;
				return IR_REMOTE_CODE_ACCEPTED;
			case 0xFF10EF: // 7
				if (serial_debug) Serial.println(" 7");
				break;
			case 0xFF18E7: // 2
				if (serial_debug) Serial.println(" 5");
				break;
			case 0xFF22DD: // LEFT
				if (serial_debug) Serial.println(" LEFT");
				code = InputCode::left;
				return IR_REMOTE_CODE_ACCEPTED;
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
				return IR_REMOTE_CODE_ACCEPTED;
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
				return IR_REMOTE_CODE_ACCEPTED;
			case 0x00FFB04F: // 3
				if (serial_debug) Serial.println(" 3");
				break;
			case 0x00FFC23D: // RIGHT
				if (serial_debug) Serial.println(" RIGHT");
				code = InputCode::right;
				return IR_REMOTE_CODE_ACCEPTED;
			case 0xFFFFFFFF: // Repeat previous code
				if (serial_debug) Serial.println(" Repeated");
				return IR_REMOTE_CODE_REPEATED;
			default:
				if (serial_debug) Serial.println(" Unknown");
				break;
			}
		}

		return IR_REMOTE_CODE_NONE;
	}
};

//-----------------------------------------------------------------------------


// Insert your other IR remote codes here in the same model as KeyesIRRemoteInput


//-----------------------------------------------------------------------------

#define IR_RECV_PIN 55 // IR Pin

// Insert your other IR remote models in the selector list
struct IRRemoteInput : IRRemoteInputSelector<KeyesIRRemoteInput>
{
	struct Config
	{
		bool enabled = false;
	} cfg;

	IRrecv	       recv{ IR_RECV_PIN };
	decode_results results;

	InputCode      previous_code = InputCode::none;
	size_t         repeated_code = 0;
	bool           accepted_code = false;
	bool           validate_code = false;
	size_t         repeat_count = 0;

	inline void setup()
	{
		if (Storage.configure(cfg, "/.config/IR_REMOTE.input"))
		{
			recv.enableIRIn();

			Serial.println(F("Input device: IR remote."));
		}
	}

	inline bool readCode(InputCode &code)
	{
		bool result = false;

		if (cfg.enabled)
		{
			if (recv.decode_old(&results))
			{
				if (false and serial_debug)
				{
					uint32_t value = results.value;

					if (value) Serial.print(value, HEX);
				}

				validate_code = false;

				switch (IRRemoteInputSelector::readCode(this, code))
				{
				case IR_REMOTE_CODE_NONE:
					result = false;
					repeat_count = 0;
					accepted_code = false;
					break;
				case IR_REMOTE_CODE_ACCEPTED:
					result = true;
					accepted_code = true;
					previous_code = code;
					validate_code = true;
					repeat_count = 0;
					break;
				case IR_REMOTE_CODE_REPEATED:
					if (accepted_code)
					{
						size_t mask =
							(repeat_count < (4 *  0 + 8 * 5)) ? 8 :
							(repeat_count < (4 * 15 + 8 * 5)) ? 4 :
																2 ;
						if (mask > 2)
						{
							result = false;
							++repeat_count;
						}
						if (++repeated_code & mask) // Slow down the repeat rate
						{
							result = true;
							validate_code = true;
							repeated_code = 0;
							code = previous_code;
						}
					}
					validate_code = true;
					break;
				}

				recv.resume();
			}
		}

		return result;
	}
};
