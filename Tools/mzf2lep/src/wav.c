#include <stdio.h>
#include "wav.h"
#include "physical.h"

// Externs.
extern FILE *WAV, *LEP;

// Global variables.
dword fs = 0;
// Numbers are little endian.
byte header[44] = { 'R', 'I', 'F', 'F',   // File description header.
					0x0, 0x0, 0x0, 0x0,   // Filesize - 8.
					'W', 'A', 'V', 'E',   // "WAVE" Description header.
					'f', 'm', 't', ' ',   // "fmt " Description header.
					0x10, 0x0, 0x0, 0x0,  // Size of WAVE section chunck.
					0x1, 0x0,             // Wave type format.
					0x1, 0x0,             // Mono or stereo.
					0x44, 0xac, 0x0, 0x0, // Sample rate.
					0x44, 0xac, 0x0, 0x0, // Bytes per second.
					0x1, 0x0,             // Block alignment.
					0x8, 0x0,             // Bits per sample.
					'd', 'a', 't', 'a',   // "data" Description header.
					0x0, 0x0, 0x0, 0x0 }; // Size of data chunk.

static int lep_count = 0;
static int lep_curr_sample;
static int lep_last_sample = -1;

#define UNIT 16 /* resolution: 16 µs */

static void lepb(int dir, int delay)
{
	int i;
	int delay127;
	int byte;
	delay127 = delay / 127;
	byte = delay - (127 * delay127);
	if (byte == 0) byte++;
	if (dir == 0) byte = -byte;
	fputc(byte, LEP);
	for (i = 0; i < delay127; i++) fputc(0, LEP);
}

// Public functions.
void outb(int value)
{
	if (WAV)
	{
		fprintf(WAV, "%c", value);
	}
	lep_count++;
	lep_curr_sample = (value >= 0x80) ? 1 : 0;
	if (lep_curr_sample != lep_last_sample)
	{
		if (lep_last_sample != -1) lepb(lep_last_sample, lep_count * 10000 / UNIT / 441);
		lep_last_sample = lep_curr_sample;
		lep_count = 0;
	}
}//outb

// Write the WAV header.
void writewavheader(void)
{
	if (WAV)
	{
		int i = 0;

		for (i = 0; i < 44; i++)
		{
			fprintf(WAV, "%c", header[i]);
		}
	}
}//writewavheader

// Set the filesizes in the WAV header. 
void setheader(void)
{
	if (WAV)
	{
		dword temp = fs;
		int i = 0;

		fseek(WAV, 4, SEEK_SET);
		fprintf(WAV, "%c", (temp & 0xff) + 36);
		fseek(WAV, 40, SEEK_SET);
		fprintf(WAV, "%c", temp & 0xff);
		temp >>= 8;
		for (i = 1; i < 4; i++)
		{
			fseek(WAV, 4 + i, SEEK_SET);
			fprintf(WAV, "%c", temp & 0xff);
			fseek(WAV, 40 + i, SEEK_SET);
			fprintf(WAV, "%c", temp & 0xff);
			temp >>= 8;
		}//for
	}
	// last LEP byte
	lepb(lep_last_sample, lep_count * 10000 / UNIT / 441);
}//setheader
