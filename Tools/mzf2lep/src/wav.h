// Definitions.
#define DEFAULT_LONG_UP       21 // Normal mode. 21+21+11+11 = 64
#define DEFAULT_LONG_DOWN     21
#define DEFAULT_SHORT_UP      11
#define DEFAULT_SHORT_DOWN    11

#define FAST_LONG_UP          11 // Fastest in normal mode. 11+21+11+12 = 55 => 1.16 times faster
#define FAST_LONG_DOWN        21
#define FAST_SHORT_UP         11
#define FAST_SHORT_DOWN       12

#define TURBO_2_LONG_UP       8  // Turbo 2x. 8+11+5+6 = 27 => 2.13 times faster
#define TURBO_2_LONG_DOWN     11
#define TURBO_2_SHORT_UP      5
#define TURBO_2_SHORT_DOWN    6
#define DEFAULT_BMARK_OFFSET  0

#define TURBO_3_LONG_UP       5  // Turbo 3x. 5+7+3+4 = 19 => 3.36 times faster
#define TURBO_3_LONG_DOWN     7
#define TURBO_3_SHORT_UP      3
#define TURBO_3_SHORT_DOWN    4

#define TURBO_FAST_LONG_UP    3  // Turbo 4x (fastest in turbo mode). 3+5+2+3 = 13 => 4.92 times faster
#define TURBO_FAST_LONG_DOWN  5
#define TURBO_FAST_SHORT_UP   2
#define TURBO_FAST_SHORT_DOWN 3

#define TURBO_ULTRAFAST_LONG_UP    2 // Turbo Ultrafast. MZ handshakes with WRITE signal
#define TURBO_ULTRAFAST_LONG_DOWN  2
#define TURBO_ULTRAFAST_SHORT_UP   1
#define TURBO_ULTRAFAST_SHORT_DOWN 1

// Prototypes.
void outb(int);
void writewavheader(void);
void setheader(void);
