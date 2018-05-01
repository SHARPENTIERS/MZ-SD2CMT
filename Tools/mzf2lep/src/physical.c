#include "physical.h"

// Definitions.
extern dword fs;

// Global variables.
byte	ZERO = 0x30;
byte	ONE = 0xd0;
int		LONG_UP = 0;    // These variables define the long wave.
int		LONG_DOWN = 0;
int		SHORT_UP = 0;   // These variables define the short wave.
int		SHORT_DOWN = 0;
int		corr_1 = 0;
int		corr_2 = 0;

// Private function prototypes.
void	lp(void); // Long pulse.
void	sp(void); // Short pulse.

// Private functions.
// Write a long pulse.
void lp(void)
{
	int j = 0;

	for (j = 0; j < LONG_UP; j++)
	{
		outb(ZERO);
	}
	for (j = 0; j < LONG_DOWN; j++)
	{
		outb(ONE);
	}
	fs += LONG_UP + LONG_DOWN;
}//lp

// Write a short pulse.
void sp(void)
{
	int j = 0;

	for (j = 0; j < SHORT_UP; j++)
	{
		outb(ZERO);
	}
	for (j = 0; j < SHORT_DOWN; j++)
	{
		outb(ONE);
	}
	fs += SHORT_UP + SHORT_DOWN;
}//sp

// Public functions.
// Reverse polarity.
void reversepol(void)
{
	ZERO = 0xd0;
	ONE = 0x30;
}//reversepol

// Write a gap of i short pulses.
void gap(int i)
{
	int j = 0;

	for (j = 0; j < i; j++)
	{
		sp();
	}
}//gap

// Write a tapemark of i long pulses, i short pulses and one long pulse.
void tapemark(int i)
{
	int j = 0;

	for (j = 0; j < i; j++)
	{
		lp();
	}
	for (j = 0; j < i; j++)
	{
		sp();
	}
	lp();
	lp();
}//tapemark

// Write the checksum.
void writecs(word cs)
{
	byte i = 0x0;
	int j = 0;

	cs &= 0xffff;
	for (j = 0x3; j; j >>= 1)
	{    // for (j = 0; j < 2; j++)
		for (i = 0xff; i; i >>= 1)
		{ // for (i = 0; i < 8; i++)
			if (cs & 0x8000) // If the most significant bit is set
			{
				lp();                    // wite a one.
			}
			else
			{
				sp();                    // Else write a zero.
			}
			cs <<= 1;                  // Go to the next bit.
		}//for 
		lp();
	}//for
	//lp(); // [hlide] remove this lp() because the monitor ROM code has no such LONG PULSE reading after checksum reading
}//writecs

// Define the waveform to use.
void setspeed(int i)
{
	switch (i)
	{
	case 1: // Fastest in normal mode. Probably unstable...
		LONG_UP = FAST_LONG_UP + corr_1;
		LONG_DOWN = FAST_LONG_DOWN + corr_2;
		SHORT_UP = FAST_SHORT_UP + corr_1;
		SHORT_DOWN = FAST_SHORT_DOWN + corr_2;
		break;
	case 2: // Turbo mode 2x.
		LONG_UP = TURBO_2_LONG_UP + corr_1;
		LONG_DOWN = TURBO_2_LONG_DOWN + corr_2;
		SHORT_UP = TURBO_2_SHORT_UP + corr_1;
		SHORT_DOWN = TURBO_2_SHORT_DOWN + corr_2;
		break;
	case 3: // Turbo mode 3x.
		LONG_UP = TURBO_3_LONG_UP + corr_1;
		LONG_DOWN = TURBO_3_LONG_DOWN + corr_2;
		SHORT_UP = TURBO_3_SHORT_UP + corr_1;
		SHORT_DOWN = TURBO_3_SHORT_DOWN + corr_2;
		break;
	case 4: // Fastest in turbo mode. Probably unstable...
		LONG_UP = TURBO_FAST_LONG_UP + corr_1;
		LONG_DOWN = TURBO_FAST_LONG_DOWN + corr_2;
		SHORT_UP = TURBO_FAST_SHORT_UP + corr_1;
		SHORT_DOWN = TURBO_FAST_SHORT_DOWN + corr_2;
		break;
	case 5: // Ultrafast in turbo mode.
		LONG_UP = TURBO_ULTRAFAST_LONG_UP;
		LONG_DOWN = TURBO_ULTRAFAST_LONG_DOWN;
		SHORT_UP = TURBO_ULTRAFAST_SHORT_UP;
		SHORT_DOWN = TURBO_ULTRAFAST_SHORT_DOWN;
		break;
	default: // Normal mode.
		LONG_UP = DEFAULT_LONG_UP;
		LONG_DOWN = DEFAULT_LONG_DOWN;
		SHORT_UP = DEFAULT_SHORT_UP;
		SHORT_DOWN = DEFAULT_SHORT_DOWN;
	}//switch
}//setspeed

// Write a byte and count the ones for the checksum.
word writebyte(byte b)
{
	word cs = 0x0;
	byte i = 0x0;

	for (i = 0xff; i; i >>= 1)
	{
		if (b & 0x80)
		{
			lp();
			cs++;
		}//if
		else
		{
			sp();
		}
		b <<= 1;
	}//for
	lp();
	return cs;
}//writebyte

// Get the file size.
word getfilesize(byte *image)
{
	return image[0x12] | (image[0x13] << 8);
}//getfilesize

// See if the MZF file is valid.
int assert(byte *image, word i)
{
	word fs = getfilesize(image);

	if (fs + 0x80 != i)
	{
		if (i - fs > 0x200)
		{
			return 2;
		}
		if (i < fs)
		{
			return 2;
		}
		return 1;
	}//if
	return 0;
}//assert
