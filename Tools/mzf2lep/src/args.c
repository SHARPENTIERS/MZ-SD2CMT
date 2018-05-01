#include <stdio.h>
#include <stdlib.h> // Just to remove a warning (exit).
#include "args.h"
#include "methods.h"

// Externs.
extern int speed_1,
speed_2,
corr_1,
corr_2;
extern char *filename,
*wavfile,
*lepfile;
extern void(*method)(byte *, int);

// Private function prototypes.
int chtoi(char *), // Convert the first char of a string to an integer.
stoi(char *);  // Convert a string to an integer.

// Private functions.
// Convert the first char of a string to an integer and check the boundries.
int chtoi(char *string)
{
	int temp = 0;

	if (!string)
		return -1;
	temp = (int)string[0] - 48;
	if ((temp < 0) || (temp > 4))
		return -1;
	return temp;
}//chtoi

// Convert the a string to an integer and check the boundries.
int stoi(char *string)
{
	int i = 0,
		m = 1,
		temp = 0,
		ret = 0;

	if (!string)
		return -100;
	if (string[0] == '-')
	{
		m = -1;
		i++;
	}//if

	while (string[i])
	{
		temp = (int)string[i] - 48;
		if ((temp < 0) || (temp > 9))
			return -100;
		ret *= 10;
		ret += temp;
		i++;
	}//while
	if (ret > 50)
		return -100;
	return m * ret;
}//stoi

// Public functions.
// Print usage and return an error code.
void error(int i)
{
	printf("Usage: mzf2lep <-i n> <-t n> <-1 n> <-2 n> <-c> <-s> <-w>\n"
		"                  input.mzf/mzt/m12\n"
		"                  output.16us.lep [output.44100Hz.8bit.wav]\n"
		" -i sets initial speed mode (0, 1, 2, 3 or 4), default = 0.\n"
		" -t sets turbo speed mode (0, 1, 2, 3 or 4), default = 2.\n"
		" -1 sets correction for fast initial mode (-50 to 50).\n"
		" -2 sets correction for fast turbo mode (-50 to 50).\n"
		" -c sets conventional writing mode.\n"
		" -s sets fast writing mode (default).\n"
		" -w sets turbo writing mode.\n"
		" -p reverse polarity.\n");
	exit(i);
}//error

// Set the configuration variables.
int setvars(int argc, char **argv)
{
	int temp = 0,
		i = 1;

	while (i < argc)
	{
		if (argv[i][0] == '-')
		{
			switch (argv[i][1])
			{
			case 'i':                                   // Initial write speed.
				temp = chtoi(argv[i + 1]);
				if (temp == -1)
				{
					printf("Error: no valid initial speed given.\n");
					error(1);
				}//if
				speed_1 = temp;
				i++;
				break;
			case 't':                                   // Turbo write speed.
				temp = chtoi(argv[i + 1]);
				if (temp == -1)
				{
					printf("Error: no valid turbo speed given.\n");
					error(1);
				}//if
				speed_2 = temp;
				i++;
				break;
			case '1':                                   // Initial fast correction.
				temp = stoi(argv[i + 1]);
				if (temp == -100)
				{
					printf("Error: no valid fast initial correction given.\n");
					error(1);
				}//if
				corr_1 = temp;
				i++;
				break;
			case '2':                                   // Initial fast correction.
				temp = stoi(argv[i + 1]);
				if (temp == -100)
				{
					printf("Error: no valid fast turbo correction given.\n");
					error(1);
				}//if
				corr_2 = temp;
				i++;
				break;
			case 'c':
				method = conv;
				break;
			case 's':
				method = trans;
				break;
			case 'w':
				method = turbo;
				break;
			case 'p':
				reversepol();
				break;
			default:
				printf("Error: unknown option: %s\n", argv[i]);
				error(1);
			}//switch
		}
		else
		{
			if (!filename)
			{
				filename = argv[i];
				printf("MZF file: %s...\n", filename);
			}
			else if (!lepfile)
			{
				lepfile = argv[i];
				printf("LEP file: %s...\n", lepfile);
			}
			else
			{
				wavfile = argv[i];
				printf("WAV file: %s...\n", wavfile);
			}
		}
		i++;
	}//while
	if (!filename)
	{
		printf("Error: no input filename given.\n");
		error(1);
	}//if
	if (!lepfile)
	{
		printf("Error: no LEP filename given.\n");
		error(1);
	}//if

	return 0;
}//setvars
