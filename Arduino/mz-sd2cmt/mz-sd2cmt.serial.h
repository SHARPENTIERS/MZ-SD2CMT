#pragma once

enum class SerialCode : int8_t
{
	none
};

bool serial_debug = true;

struct SerialPrompt
{
	void setup()
	{
		Serial.begin(115200);
	}

	bool readCode(SerialCode& code)
	{
		if (Serial.available())
		{
			if (Serial.peek() == '|')
			{
				String command = Serial.readString();
				if (command == F("|debug"))
				{
					serial_debug = !serial_debug;
				}
				else
				{
					Serial.print(F("Unknown command: "));
					Serial.println(command);
				}

				return true;
			}		
		}

		return false;
	}
} SerialPrompt;
