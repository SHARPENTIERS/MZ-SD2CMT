#pragma once

struct SerialDisplay : DummyDisplay
{
	static void displayCode(DisplayCode code)
	{
		switch (code)
		{
		case DisplayCode::set_entry_name:
			if (entry_exists)
			{
				entry.printFileSize(&Serial);
				Serial.write(' ');
				entry.printModifyDateTime(&Serial);
				Serial.write(' ');
				entry.printName(&Serial);
				if (entry.isDir())
				{
					Serial.write('/');
				}
				Serial.println();
			}
			else
			{
				Serial.println(F("Error: fetchEntry - no file/directory!"));
			}
			break;

		default:
			break;
		}
	}

	static void setup()
	{
		Serial.println(F("MZ-SD2CMT"));
	}

	static void configure()
	{
	}
};
