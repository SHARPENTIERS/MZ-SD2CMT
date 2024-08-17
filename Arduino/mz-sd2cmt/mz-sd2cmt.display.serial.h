#pragma once

struct SerialDisplay : DummyDisplay
{
	static void displayCode(DisplayCode code, Device *device)
	{
		switch (code)
		{
		case DisplayCode::set_entry_name:
			if (Storage.entry_exists)
			{
				Storage.entry.printFileSize(&Serial);
				Serial.write(' ');
				Storage.entry.printModifyDateTime(&Serial);
				Serial.write(' ');
				Storage.entry.printName(&Serial);
				if (Storage.entry.isDir())
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

		case DisplayCode::calibrate_device:
			if (device)
			{
				Serial.print(F("Calibrating "));
			 	Serial.print(device->name);
				Serial.println(F("..."));
				Serial.println(device->message);
			}
			break;

		default:
			break;
		}
	}

	static void setup()
	{
		Serial.println(F("Output device: serial 115200."));
	}

	static void configure()
	{
	}
};
