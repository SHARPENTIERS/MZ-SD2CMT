#pragma once

void playMZF()
{
	playMZF_Legacy_Procedural();
}

//////////////////////////////////////////////////////////////////////

static const unsigned long sp1 = 240; // short period for signal 1 
static const unsigned long lp1 = 464; // long period for signal 1
static const unsigned long sp0 = 504; // short period for signal 0
static const unsigned long lp0 = 958; // long period for signal 0

void sendBit(unsigned long p1, unsigned long p0)
{
	osp.wait();
	osp.fire(p1, p0);
}

void sendEdge()
{
	sendBit(lp1, lp0);
}

void sendGap(unsigned short loop)
{
	while (loop--)
	{
		sendBit(sp1, sp0);
	}
}

void sendTapeMark(unsigned char loop)
{
	for (auto i = loop; i; --i)
	{
		sendBit(lp1, lp0);
	}
	for (auto i = loop; i; --i)
	{
		sendBit(sp1, sp0);
	}
}

unsigned short sendByte(char value)
{
	unsigned short checksum = 0;
	sendEdge();
	for (char i = 8; i; --i)
	{
		if (value & 0x80)
		{
			sendBit(lp1, lp0);
			++checksum;
		}
		else
		{
			sendBit(sp1, sp0);
		}
		value <<= 1;
	}
	return checksum;
}

void sendChecksum(unsigned short checksum)
{
	for (char j = 2; j; --j)
	{
		sendEdge();
		for (char i = 8; i; --i)
		{
			if (checksum & 0x8000)
			{
				sendBit(lp1, lp0);
			}
			else
			{
				sendBit(sp1, sp0);
			}
			checksum <<= 1;
		}
	}
}

void playMZF_Legacy_Procedural()
{
	unsigned long  total = entry.fileSize(); // total number of LEP bytes to read
	unsigned short checksum;
	unsigned long  size;
	unsigned long  addr;
	unsigned long  exec;

	canceled = false;

	if (total > 128)
	{
		total -= 128;
		if (entry.read(header_buffer, 128) != 128)
		{
			canceled = true;
			return;
		}
		else
		{
			total =
				(((unsigned long)header_buffer[0x12]) & 255) +
				(((unsigned long)header_buffer[0x13]) & 255) * 256;

			Serial.print(F("Program size: "));
			Serial.println(total);
		}
		if (entry.available() < total)
		{
			canceled = true;
			return;
		}

		size =
			total;

		addr =
			(((unsigned long)header_buffer[0x14]) & 255) +
			(((unsigned long)header_buffer[0x15]) & 255) * 256;


		exec =
			(((unsigned long)header_buffer[0x16]) & 255) +
			(((unsigned long)header_buffer[0x17]) & 255) * 256;

		Serial.print(F("SIZE: "));
		Serial.println(size | 0xC0DE0000, HEX);

		Serial.print(F("ADDR: "));
		Serial.println(addr | 0xC0DE0000, HEX);

		Serial.print(F("EXEC: "));
		Serial.println(exec | 0xC0DE0000, HEX);
	}

	digitalWrite(MZT_LO, LOW); // led light off initially
	digitalWrite(MZT_CS, LOW); // signal /SENSE at 0 to acknowledge the MZ that data is ready
	osp.start();

	int trial = 2;

retry:

	// HEADER

	delay(2000);

	sendGap(100);

	sendTapeMark(40);

	sendEdge();
	//sendEdge();

	checksum = 0;

	for (auto i = 0u; i < 128u; ++i)
	{
		checksum += sendByte(header_buffer[i]);
	}

	sendChecksum(checksum);

	sendEdge();
	sendEdge();

	// PROGRAM

	delay(2000);

	sendGap(100);

	sendTapeMark(20);

	sendEdge();
	//sendEdge();

	checksum = 0;

	for (auto i = total; i; --i)
	{
		checksum += sendByte(entry.read());
	}

	sendChecksum(checksum);

	sendEdge();
	sendEdge();

	delay(2000);

	Serial.print(F("CHKS: "));
	Serial.println(checksum | 0xC0DE0000, HEX);

	if (digitalRead(MZT_MI) == HIGH)
	{
		Serial.println(F("CHECKSUM Error!"));

		if (--trial)
		{
			entry.seekSet(128);
			goto retry;
		}
	}

	osp.stop();
	digitalWrite(MZT_CS, HIGH); // reset the /SENSE signal to 1
}
