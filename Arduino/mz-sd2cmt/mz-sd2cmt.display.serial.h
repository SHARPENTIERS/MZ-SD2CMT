#pragma once

struct SerialDisplay : DummyDisplay
{
	static void displayCode(DisplayCode code)
	{
		switch (code)
		{
		case DisplayCode::set_entry_name:
			if (Storage::entry_exists)
			{
				Storage::entry.printFileSize(&Serial);
				Serial.write(' ');
				Storage::entry.printModifyDateTime(&Serial);
				Serial.write(' ');
				Storage::entry.printName(&Serial);
				if (Storage::entry.isDir())
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
