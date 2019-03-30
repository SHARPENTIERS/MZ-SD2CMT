#pragma once

enum class SerialCode : int8_t
{
	none
};

struct SerialPrompt
{
	static void setup()
	{
		Serial.begin(115200);
	}

	static bool readCode(SerialCode& code)
	{
		if (Serial.available())
		{
			Serial.println(Serial.readString());

			// Do something with Serial?
			
			return true;
		}

		return false;
	}
};
