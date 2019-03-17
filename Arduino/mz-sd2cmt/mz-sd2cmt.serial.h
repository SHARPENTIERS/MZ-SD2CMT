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
		code = SerialCode::none;

		if (Serial.available())
		{
			(void)Serial.read();

			// Do something with Serial?
		}

		return false;
	}
};
