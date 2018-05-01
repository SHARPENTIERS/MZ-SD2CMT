#include "wav.h"

// Defenitions.
typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned int dword;

// Prototypes.
void	reversepol(void);      // Reverse polarity.
void	gap(int);              // i short pulses.
void	tapemark(int);         // i long, i short and two long pulses.
void	writecs(word);         // Write the checksum.
void	setspeed(int);         // Define the waveform.
word	writebyte(byte);       // Write a byte and count the ones. 
word	getfilesize(byte *);   // Get the file size.
int		assert(byte *, word);  // See if the MZF file is valid.
