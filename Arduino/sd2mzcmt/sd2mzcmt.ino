#include <Arduino.h>

//#include <DigitalIO.h>       // Sketch > Include library > Manage libraries > search: DigitalIO, by Bill Greiman
#include <SdFat.h>           // Sketch > Include library > Manage libraries > search: SdFat, by Bill Greiman
//#include <TimerOne.h>        // Sketch > Include library > Manage libraries > search: TimerOne, by Jesse Tane, Jérôme Despatis, Michael Polli, Dan Clemens, Paul Stoffregen

#define HAS_LCD16x2 1
#define HAS_ANALOG_BTNSET 1

#define LEP_UNIT      16     // LEP resolution in µs unit

#define SD0_MO        50     // SD MOSI
#define SD0_MI        51     // SD MISO
#define SD0_CK        52     // SD SCK
#define SD0_SS        53     // SS SS

#define MZT_DI        15     // MZTape WRITE (WRITE -> DI)
#define MZT_MI        16     // MZTape MOTOR ON (MOTOR ON -> MI) 
#define MZT_DO        17     // MZTape READ (DO -> READ)
#define MZT_CS        18     // MZTape /SENSE (CS -> /SENSE) 
#define MZT_LO        19     // MZTape LED OUTPUT

#define DIR_DEPTH     8
#define SFN_DEPTH     13
#define LFN_DEPTH     100

#define ENTRY_UNK     0
#define ENTRY_DIR     1
#define ENTRY_LEP     2
#define ENTRY_MZF     3

char          entry_type = ENTRY_UNK;

SdFat         sd;
SdFile        entry;
SdFile        dir[DIR_DEPTH];
char          sfn[SFN_DEPTH];
char          lfn[LFN_DEPTH+1];
bool          sd_ready = false;
int16_t       entry_index = 0;
int8_t        dir_depth = -1;
int16_t       dir_index[DIR_DEPTH] = {};
bool          canceled = false;

#if HAS_LCD16x2  

#include <LiquidCrystal.h>   // Built-in by Arduino

#define LCD_RS        8      // LCD RESET
#define LCD_EN        9      // LCD ENABLE
#define LCD_D4        4      // LCD D4
#define LCD_D5        5      // LCD D5
#define LCD_D6        6      // LCD D6
#define LCD_D7        7      // LCD D7

#define SCROLL_SPEED  250    // Text scroll delay
#define SCROLL_WAIT   3000   // Delay before scrolling starts

byte icon[8][8] =
{
  {
    B10000,
    B10000,
    B10000,
    B10000,
    B10000,
    B10000,
    B10000,
    B10000
  },
  {
    B11000,
    B11000,
    B11000,
    B11000,
    B11000,
    B11000,
    B11000,
    B11000
  },
  {
    B11100,
    B11100,
    B11100,
    B11100,
    B11100,
    B11100,
    B11100,
    B11100
  },
  {
    B11110,
    B11110,
    B11110,
    B11110,
    B11110,
    B11110,
    B11110,
    B11110
  },
  {
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111
  },
  {
    B11100,
    B10011,
    B11101,
    B10001,
    B10001,
    B10001,
    B10001,
    B11111
  },
  {
    B00111,
    B00100,
    B00100,
    B01110,
    B10101,
    B00100,
    B00100,
    B11100
  },
  {
    B10001,
    B11011,
    B10101,
    B10001,
    B11111,
    B00100,
    B01000,
    B11111
  }
};

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
byte          scroll_pos = 0;
unsigned long scroll_time = millis() + SCROLL_WAIT;

/// This function displays a text on the first line with a horizontal scrolling if necessary.
void scrollText(char* text)
{
  if (scroll_pos < 0)
  {
    scroll_pos = 0;
  }
  char outtext[17];
  outtext[0] = entry_type ? (entry_type + 4) : '?'; 
  for (int i = 1; i < 16; ++i)
  {
    int p = i + scroll_pos - 1;
    if (p < strlen(text)) 
    {
      outtext[i] = text[p];
    }
    else
    {
      outtext[i] = '\0';
    }
  }
  outtext[16] = '\0';

  lcd.setCursor(0,0);
  lcd.print(F("                    "));
  lcd.setCursor(0,0);
  lcd.print(outtext);
}
#endif

#define BTN_R         0      // Button RIGHT
#define BTN_U         1      // Button UP
#define BTN_D         2      // Button DOWN
#define BTN_L         3      // Button LEFT
#define BTN_S         4      // Button SELECT
#define BTN_N         5      // Button None

bool          btn_pressed = false;

/// This function reads an analogic pin to determine which button is pressed.
/// @return BTN_L, BTN_R, BTN_U, BTN_D or BTN_S if one pressed (left, right, up, down or select buttons), otherwise BTN_N (none)
int readButtons()
{
#if HAS_ANALOG_BTNSET
  int adc_key = analogRead(0);

  if (adc_key < 380)
  {
    if (adc_key < 50)
      return BTN_R;
    else
      return (adc_key < 195) ? BTN_U : BTN_D;
  }
  else
  {
    if (adc_key <  555)
      return BTN_L;
    else
      return (adc_key < 790) ? BTN_S : BTN_N;
  }
#else
  return BTN_N;
#endif
}

bool checkForLEP(char *filename)
{
  return !!strstr(strlwr(filename + (strlen(filename)-4)), ".lep");
}

bool checkForMZF(char *filename)
{
  return !!strstr(strlwr(filename + (strlen(filename)-4)), ".mzf");
}

void setupDisplay()
{
#if HAS_LCD16x2  
  lcd.begin(16, 2);
  
  lcd.createChar(0, icon[0]);
  lcd.createChar(1, icon[1]);
  lcd.createChar(2, icon[2]);
  lcd.createChar(3, icon[3]);
  lcd.createChar(4, icon[4]);
  lcd.createChar(5, icon[5]);
  lcd.createChar(6, icon[6]);
  lcd.createChar(7, icon[7]);
  lcd.clear();
  
  lcd.print(F("SD2MZCMT"));
#endif
}

void displayNoSDCard()
{
#if HAS_LCD16x2  
  lcd.clear();
  lcd.print(F("SD2MZCMT: No SD card"));
#endif
}

inline void displayEntryNameMessage(bool exists)
{
#if HAS_LCD16x2  
  scroll_time = millis() + SCROLL_WAIT;
  scroll_pos = 0;

  scrollText(lfn);
#endif

  if (exists)
  {
    entry.printFileSize(&Serial);
    Serial.write(' ');
    entry.printModifyDateTime(&Serial);
    Serial.write(' ');
    entry.printName(&Serial);
    if (entry.isDir())
    {
      Serial.write('/');
    }
    Serial.println();
  }
  else
  { 
    Serial.println(F("Error: fetchEntry - no file/directory!"));
  } 
}

inline void displayScrollingMessage()
{
#if HAS_LCD16x2  
  if ((millis() >= scroll_time) && (strlen(lfn)>15))
  {
    scroll_time = millis() + SCROLL_SPEED;
    scrollText(lfn);
    ++scroll_pos;
    if (scroll_pos > strlen(lfn))
    {
      scroll_pos = 0;
      scroll_time = millis() + SCROLL_WAIT;
      scrollText(lfn);
    }
  }
#endif
}

inline void displayStartPlayingMessage()
{
#if HAS_LCD16x2  
  lcd.clear();
  scroll_pos = 0;
  scrollText(lfn);
  lcd.setCursor(0, 1);
  lcd.print(F("Playing...[    ]"));
#endif
}      

inline void displayResumePlayingMessage()
{
#if HAS_LCD16x2  
  lcd.setCursor(0, 1);
  lcd.write(F("Play"));
#endif
}      

inline void displayStopPlayingMessage(unsigned long elapsed_time) 
{
#if HAS_LCD16x2  
  auto duration = (elapsed_time + 999) / 1000; 
  lcd.setCursor(0, 1);
  if (canceled)
  {
    lcd.print(F("Canceled in "));
    lcd.print(duration);
    lcd.print(F("s.  "));
  }
  else
  {
    lcd.print(F("Done in "));
    lcd.print(duration);
    lcd.print(F("s.      "));
  }
#endif
}

inline void displayPausingMessage()
{
#if HAS_LCD16x2  
  lcd.setCursor(0, 1);
  lcd.write(F("Paus"));
#endif
}      

inline void displayProgressBar(unsigned long new_progress)
{
#if HAS_LCD16x2  
  lcd.setCursor(11 + (new_progress / 5) , 1);
  lcd.write(new_progress % 5);
#endif
}

/// This function fetches an entry in the current directory.
/// @arg int16_t new_index index of a valid entry in the directory.
void fetchEntry(int16_t new_index)
{
  bool    found = true;
  int16_t index;
      
  entry.close();

  if (new_index < 0)
  {
    new_index = 0;
  }

  do
  {
    dir[dir_depth].rewind();
    index = 0;

    while (index <= new_index)
    {
      found = entry.openNext(&dir[dir_depth], O_READ);
      if (found)
      {
        if (!entry.isHidden() && !entry.isSystem())
        {
          if (index == new_index)
          {
            break;
          }
          ++index;        
        }
        entry.close();
      }
      else
      {
        break;
      }
    }

    if (!found)
    {
      new_index = entry_index;
    }
  }
  while (!found && index > 0);

  if (found)
  {
    entry.getSFN(sfn);
    entry.getName(lfn, LFN_DEPTH);

    /**/ if (entry.isDir())
    {
      entry_type = ENTRY_DIR;
    }
    else if (checkForLEP(lfn))
    {
      entry_type = ENTRY_LEP;
    }
    else if (checkForMZF(lfn))
    {
      entry_type = ENTRY_MZF;
    }
    else
    {
      entry_type = ENTRY_UNK;
    }

    displayEntryNameMessage(true);
    
    entry_index = new_index;
  }
  else
  {
    memset(sfn, 0, SFN_DEPTH);
    memset(lfn, 0, LFN_DEPTH + 1);
    strcpy(lfn, "<no file>");
    
    displayEntryNameMessage(false);
  }
}

/// This function enters a directory.
void enterDir()
{
  if (dir_depth < DIR_DEPTH - 2)
  {
    if (dir_depth < 0)
    {
      if (dir[0].openRoot(&sd))
      {
        ++dir_depth;

        fetchEntry(0);
      }
      else
      {
        Serial.println(F("Error: enterDir - cannot open root directory!"));
      }      
    }
    else if (entry.isOpen())
    {
      ++dir_depth;
      
      dir[dir_depth] = entry;
      dir_index[dir_depth] = entry_index;

      fetchEntry(0);                  
    }
    else
    {
        Serial.println(F("Error: enterDir - no subdirectory!"));
    }
  }
  else
  {
    Serial.println(F("Error: enterDir - directory depth exceeded!"));
  }  
}

/// This function leaves a subdirectory.
void leaveDir()
{
  // leave only subdirectory
  if (dir_depth > 0)
  {
    dir[dir_depth].close();

    entry_index = dir_index[dir_depth];

    --dir_depth;

    fetchEntry(entry_index);
  }
}
  
/// This function handles the case where LEFT button is pressed.
void leftPressed()
{
  if (!btn_pressed)
  {   
    leaveDir();
    
    btn_pressed = true;
  }  
}

/// This function handles the case where RIGHT button is pressed.
void rightPressed()
{
  if (!btn_pressed)
  {
    btn_pressed = true;
  }
}

/// This function handles the case where UP button is pressed.
void upPressed()
{
  if (!btn_pressed)
  {
    fetchEntry(entry_index - 1);
    
    btn_pressed = true;
  }
}

/// This function handles the case where DOWN button is pressed.
void downPressed()
{
  if (!btn_pressed)
  {
    fetchEntry(entry_index + 1);
    
    btn_pressed = true;
  }
}

/// This function handles the case where SELECT button is pressed.
void selectPressed()
{
  if (!btn_pressed)
  {
    switch (entry_type)
    {
      case ENTRY_DIR:
        {
          enterDir();
        }
        break;
      
      case ENTRY_LEP:
      case ENTRY_MZF:
        {
          displayStartPlayingMessage();

          entry.rewind();
          
          unsigned long start_time = millis();

          if (entry_type == ENTRY_LEP)
            playLEP();
          else
            playMZF();
      
          unsigned long stop_time = millis();

          displayStopPlayingMessage(stop_time - start_time);
        }
        break;

      default:
        break;
    }
    
    btn_pressed = true;
  }
}

/// This function handles the case where a button is released.
void nonePressed()
{
  if (btn_pressed)
  {
    btn_pressed = false;
  }  
}

/// This function plays a LEP file.
void playLEP()
{
  unsigned long period;                   // half-pulse period
  unsigned long delta;                    // maximum time to spend before moving to the next half-pulse
  unsigned long last_edge;                // elapsed time since the last rising / falling edge
  bool          level = HIGH;             // level of the DATA IN signal at the output of the CMT
  bool          led = LOW;                // led indicating the frequency of reading data
  char          data, next = 0;           // LEP bytes read from the SD
  unsigned long count = 0;                // number of LEP bytes read progressively
  unsigned long led_period = 0;           // blinking period for 512 bytes LEP read
  unsigned long total = entry.fileSize(); // total number of LEP bytes to read
  unsigned long old_progress = -1;
  unsigned long new_progress = 0;

  canceled = false;

  digitalWrite(MZT_DO, level); // DATA IN signal to 1 initially
  digitalWrite(MZT_LO, led); // led light off initially

  digitalWrite(MZT_CS, LOW); // signal /SENSE at 0 to acknowledge the MZ that data is ready

  last_edge = micros(); // initial elapsed time
  delta = 125000 * LEP_UNIT; // foolish MONITOR MOTOR call which waits for 2s
            
  while (count < total) // read all the LEP bytes from the file
  {
    if (digitalRead(MZT_MI) == LOW) // MOTOR at 0, pause
    {
      digitalWrite(MZT_DO, HIGH); // DAT IN signal at 1
      digitalWrite(MZT_LO, LOW); // LED off

      displayPausingMessage();

      while (digitalRead(MZT_MI) == LOW) // as long as MOTOR does not resume
      {
        if (Serial.available() || readButtons() == BTN_S) // but if you ask to cancel
        {
          canceled = true;
          digitalWrite(MZT_CS, HIGH);  // the signal /SENSE reset to 1
          return; // leave totally
        }
      }
      
      displayResumePlayingMessage();
           
      last_edge = micros(); // reset
      delta = 125000 * LEP_UNIT; // foolish MONITOR MOTOR call which waits for 2s
    }

    if (next) // the following LEP byte is immediately available
    {
      data = next; // take it
      next = 0;
      ++count;
    }
    else
    {
      data = entry.read(); // otherwise we read it from the SD
      ++count;

      if (data < 0) // if the new LEP byte is a falling edge
      {
        if (count < total)
        {
          next = entry.read(); // read in advance the following LEP byte

          new_progress = (5 * 4 * count) / total;
          if (old_progress != new_progress)
          {
            displayProgressBar(new_progress);

            old_progress = new_progress;
          }
        }
      }
    }

    /**/ if (data == 0) period = 127;   // very long period ...
    else if (data <  0) period = -data; // in absolute value
    else                period = +data; // in absolute value

    period *= LEP_UNIT; // converts to microseconds
    
    /**/ if (data > 0) level = HIGH; // if positive LEP byte, next half-pulse at 1
    else if (data < 0) level = LOW;  // if negative LEP byte, next half-pulse at 0
    
    while (micros() - last_edge < delta); // we pause in the desired period
    
    digitalWrite(MZT_DO, level); // and we update the level output DATA IN
    
    last_edge = micros(); // update the time reference for the period
    delta = period; // and the next period

    ++led_period;
    
    if (led_period & 512) // the control led is alternated every 512 LEP bytes processed
    {
      led = !led; // toggle the signal level of the LED indicator
      digitalWrite(MZT_LO, led);
    }
  }
  digitalWrite(MZT_LO, 0); // it's over, no more led.
  digitalWrite(MZT_CS, HIGH); // reset the /SENSE signal to 1
  digitalWrite(MZT_DO, HIGH);
}

// SCK = /SENSE ---> MZ inputs  MOTOR (PC4), MOTOR = not /SENSE
// SDI = READ   ---> MZ inputs  READ  (PC5) 
// SDO = WRITE  ---> MZ outputs WRITE (PC1)

void sendMZFByte(unsigned char c)
{
  // TODO    
  for (int i = 0; i < 8; ++i)
  {   
    unsigned long us = micros();

    bool level = (c & (1 << i)) ? HIGH : LOW;
    digitalWrite(MZT_DO, level);                // set SDI to level 
    digitalWrite(MZT_CS, (i & 1) ? HIGH : LOW); // toggle SCK to acknowledge MZ
    digitalWrite(MZT_LO, level);
    
    while(micros() < us + 16);
  }
}

/// This function plays a MZF file.
void playMZF()
{
  unsigned long   total = entry.fileSize();
  unsigned char   data;
  unsigned long   size = 0;
  unsigned short  load = 0;
  unsigned short  exec = 0;
  unsigned long   count;
  unsigned long   old_progress = -1;
  unsigned long   new_progress = 0;
  bool            led = LOW;

  canceled = false;

  data = entry.read(); /* 0x10F0: file attribute */

  if (data == 1 && total >= 128) // On ne lit que les fichiers de type EXECUTABLE BINAIRE
  {
    for (int i = 0x10F1; i < 0x1102; ++i) entry.read(); // skip file name

    size  = (/* 0x1102 */entry.read() & 255) << 0;   
    size += (/* 0x1103 */entry.read() & 255) << 8;

    load  = (/* 0x1104 */entry.read() & 255) << 0;
    load += (/* 0x1105 */entry.read() & 255) << 8;
    
    exec  = (/* 0x1106 */entry.read() & 255) << 0;
    exec += (/* 0x1107 */entry.read() & 255) << 8;
    
    for (int i = 0x1108; i < 0x1170; ++i) entry.read(); // skip the header

    sendMZFByte((size >> 0) & 255);
    sendMZFByte((size >> 8) & 255);
    sendMZFByte((exec >> 0) & 255);
    sendMZFByte((exec >> 8) & 255);
    sendMZFByte((load >> 0) & 255);
    sendMZFByte((load >> 8) & 255);

    count = 0;

    if (total - 128 == size)
    {
      while (count != size)
      {        
        data = entry.read();
        ++count;
  
        new_progress = (5 * 4 * count) / size;
        if (old_progress != new_progress)
        {
          displayProgressBar(new_progress);

          old_progress = new_progress;
        }
  
        sendMZFByte(data);    
      }
    }
    else
    {
      canceled = true;
    }

    digitalWrite(MZT_LO, LOW);
    digitalWrite(MZT_CS, HIGH);
  }
  else
  {
    canceled = true;
  }
}

void setup()
{
  Serial.begin(9600);

  setupDisplay();
  
  pinMode(SD0_SS, OUTPUT);
  
  pinMode(MZT_DO, OUTPUT);
  pinMode(MZT_DI, INPUT_PULLUP);
  pinMode(MZT_CS, OUTPUT);
  pinMode(MZT_MI, INPUT_PULLUP);
  pinMode(MZT_LO, OUTPUT);

  digitalWrite(MZT_DO, HIGH); // signal DATA IN à 1 initialement
  digitalWrite(MZT_CS, HIGH); // signal /SENSE à 1 (lecteur non disponible)
  digitalWrite(MZT_LO, LOW);  // témoin led éteint

  sd_ready = sd.begin(SD0_SS, SPI_FULL_SPEED);
   
  if (!sd_ready) // accès au SD en full-speed
  {
    sd.initErrorHalt();

    displayNoSDCard();
  }
}

void loop()
{
  Serial.println("SD2MZCMT");

  enterDir();
  
  while (!Serial.available())
  {
    switch (readButtons())
    {
    case BTN_R:
      rightPressed();
      break;
    case BTN_L:
      leftPressed();
      break;
    case BTN_U:
      upPressed();
      break;
    case BTN_D:
      downPressed();
      break;
    case BTN_S:
      selectPressed();
      break;
    default:
      nonePressed();
      break;
    }

    displayScrollingMessage();   
  }

  // Do something with Serial?
}
