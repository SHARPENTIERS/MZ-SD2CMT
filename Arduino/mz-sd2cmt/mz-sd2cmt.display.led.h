#pragma once

#define ddr_LED_OUTPUT (&DDRD)
#define port_LED_OUTPUT (&PORTD)
#define mask_LED_OUTPUT	(1 << PD2)

struct LedDisplay : DummyDisplay
{
	static inline void setLed(bool on)
	{
		set_port_bit(LED_OUTPUT, on);
	}

	static inline void setup()
	{
		set_ddr_bit (LED_OUTPUT, 1);
		set_port_bit(LED_OUTPUT, 0);
		
		Serial.println(F("Output device: LED activity."));
	}

	static inline void displayCode(DisplayCode code)
	{
	}
};