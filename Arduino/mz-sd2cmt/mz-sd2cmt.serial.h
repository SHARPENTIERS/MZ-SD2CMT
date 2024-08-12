#pragma once

enum class SerialCode : int8_t
{
	none
};

bool serial_debug = true;

struct SerialPrompt
{
	struct Config
	{
		bool enabled = false; 
	} cfg;

	void setup()
	{
		Serial.begin(115200);

		Serial.println(F("Input device: serial 115200."));
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
