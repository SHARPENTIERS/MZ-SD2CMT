#include "methods.h"

// Global variables.
int speed_2 = 2;

// Definitions.
// This is the turbo loader in MZF format for MZ-700/800 only.
byte program[300] = {
  0x01,                                                 // Program type.

  0x0d, 0x0d, 0x0d, 0x0d, 0x0d,                         // Room for the
  0x0d, 0x0d, 0x0d, 0x0d, 0x0d,                         // image name.
  0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d,

  0x5a, 0x00,                                           // File size.
  0x00, 0xd4,                                           // Load adress.
  0x00, 0xd4,                                           // Execution adress.
  '[', 't', 'u', 'r', 'b', 'o', ']',                    // The first 7 bytes.

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Room for comment.
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // minus 7 bytes.
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,

  0xcd, 0x00,                                           // End Header.

  // Begin Program.
  0x3e, 0x08,       // D400: LD A, 08h
  0xd3, 0xce,       // D402: OUT (0ceh), A  ; Set video mode?

  0xe5,             // D404: PUSH HL

  0x21, 0x00, 0x00, // D405: LD HL, 0000h
  0xd3, 0xe4,       // D408: OUT (0e4h), A  ; Bank switch to ROM?

  0x7e,             // D40A: LD A, (HL)
  0xd3, 0xe0,       // D40B: OUT (0e0h), A  ; Bank switch to RAM?

  0x77,             // D40D: LD (HL), A
  0x23,             // D40E: INC HL

  0x7c,             // D40F: LD A, H
  0xfe, 0x10,       // D410: CP 10h
  0x20, 0xf4,       // D412: JR NZ, f4h    ; Jump 0xf4 forward if A != 0x10

  0x3a, 0x4b, 0xd4, // D414: LD A, (d44bh)
  0x32, 0x4b, 0x0a, // D417: LD (0a4bh), A ; (0x0a4b) <- (0xd44b)
  0x3a, 0x4c, 0xd4, // D41A: LD A, (d44ch)
  0x32, 0x12, 0x05, // D41D: LD (0512h), A ; (0x0512) <- (0xd44c)
  0x21, 0x4d, 0xd4, // D420: LD HL, d44dh
  0x11, 0x02, 0x11, // D423: LD DE, 1102h
  0x01, 0x0d, 0x00, // D426: LD BC, 000dh
  0xed, 0xb0,       // D429: LDIR          ; Copy 0x0d bytes from (HL) to (DE)

  0xe1,             // D42B: POP HL
  //0xc3, 0xad, 0x00, // D41F: JP 0111h      ; Jump to 0x0111h

  0x7c,             // D42C: LD A, H
  0xfe, 0xd4,       // D42D: CP d4h
  0x28, 0x12,       // D42F: JR Z, 12h     ; Jump to label #1 if A == 0xd4

  0x2a, 0x04, 0x11, // D431: LD HL, (1104h)
  0xd9,             // D434: EXX           ; BC/DE/HL <-> BC'/DE'/HL'
  0x21, 0x00, 0x12, // D435: LD HL, 1200h
  0x22, 0x04, 0x11, // D438: LD (1104h), HL
  0xcd, 0x2a, 0x00, // D43B: CALL 002ah    ; Read data subroutine.
  0xd3, 0xe4,       // D43E: OUT (0e4h), A ; Bank switch to ROM?
  0xc3, 0x9a, 0xe9, // D440: JP e99ah      ; Jump to 0xe99a

  0xcd, 0x2a, 0x00, // D443: CALL 002ah    ; Label #1 (read data sub).
  0xd3, 0xe4,       // D446: OUT (0e4h), A ; Bank switch to ROM?
  0xc3, 0x24, 0x01, // D448: JP (0124h)
  // End program.

  0x1f, 0x01,       // D44B: 

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,      // Room for the address information
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // + the first 7 bytes of comment.
};

// Public functions.
// Transfer file the fast way.
void trans(byte *image, int hdr)
{
	word	cs = 0x0;
	word	fs = getfilesize(image);
	word	i = 0x0;

	if (hdr)
	{
		gap(128);                      // Longish gap.
		tapemark(40);                   // Long tapemark.

		for (i = 0x0; i < 0x80; i++)    // The mzf header.
		{
			cs += writebyte(image[i]);
		}
		writecs(cs);                    // The checksum of the mzf header.
		gap(128);                      // Shortish gap.
	}
	else
	{
		gap(128);                      // Shortish gap.
	}
	tapemark(20);                   // Short tapemark.

	cs = 0x0;
	fs += 0x80;
	for (i = 0x80; i < fs; i++)      // The mzf body.
	{
		cs += writebyte(image[i]);
	}
	writecs(cs);                    // The checksum of the mzf body.
}//trans

// Transfer the file the conventional way.
void conv(byte *image, int hdr)
{
	word	cs = 0x0;
	word	fs = getfilesize(image);
	word	i = 0x0;

	//gap(128);
	gap(11000);                     // Long gap.
	tapemark(40);                   // Long tapemark.

	for (i = 0x0; i < 0x80; i++)    // The mzf header.
	{
		cs += writebyte(image[i]);
	}
	writecs(cs);                    // The checksum of the mzf header.
	gap(256);                       // 256 short pulses.

	for (i = 0x0; i < 0x80; i++)    // The copy of the mzf header.
	{
		writebyte(image[i]);
	}
	writecs(cs);                    // The copy of the checksum of the mzf header.
	//gap(128);
	gap(11000);                     // Short gap.
	tapemark(20);                   // Short tapemark.

	cs = 0x0;
	fs += 0x80;
	for (i = 0x80; i < fs; i++)     // The mzf body.
	{
		cs += writebyte(image[i]);
	}
	writecs(cs);                    // The checksum of the body.
	gap(256);                       // 256 short pulses.

	for (i = 0x80; i < fs; i++)     // The copy of the mzf body.
	{
		writebyte(image[i]);
	}
	writecs(cs);                    // The copy of checksum of the body.
}//conv

// First write a turbo loader, then write the image at high speed.
void turbo(byte *image, int hdr)
{
	int j = 0;

	for (j = 0x1; j < 0x12; j++)    // Copy the name.
	{
		program[j] = image[j];
	}
	for (j = 0x1f; j < 0x80; j++)   // Copy the comment.
	{
		program[j] = image[j];
	}
	for (j = 0x12; j < 0x1f; j++)   // Copy the info.
	{
		program[j + 0x3b + 0x80] = image[j];
	}

	switch (speed_2)
	{
	case 2:
		program[0x80 + 0x4b] = 0x1f;
		break;
	case 3:
		program[0x80 + 0x4b] = 0x0f; // 0x0f-0x15
		break;
	case 4:
		program[0x80 + 0x4b] = 0x07; // 0x0f
		break;
	default:
		program[0x80 + 0x4b] = 0x3f;
		break;
	}

	trans(program, 1);
	setspeed(speed_2);
	trans(image, 0);
}//turbo
