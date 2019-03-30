# MZ-SD²CMT
SD card based CMT for MZ 80K series (MZ-80 K/K2/K2E/C, MZ-80 A, MZ-1200, MZ-700).

## Parts Used
1. Arduino MEGA (but this should work on any Arduino UNO or NANO with minimal changes)
2. SD card module
3. 16x2 4-bit parallel LCD with 5 x Buttons or 128x32 I²C OLED
4. KEYES IR remote

Examples of what I used:
- an Arduino MEGA 2560 R3 ATMEGA 16U2:
![71iRODIgxUL _SL1500_](https://user-images.githubusercontent.com/56785/55276248-0c186780-52f2-11e9-8fc0-8e57b2302ac0.jpg)
- a 0.91" I2C 128x32 OLED:
![71b-NMspBIL _SL1500_](https://user-images.githubusercontent.com/56785/55276439-0754b300-52f4-11e9-9a63-78de07897087.jpg)
- a 16x2 LCD with 5 buttons:
![71By4b4xsRL _SL1200_](https://user-images.githubusercontent.com/56785/55276357-30287880-52f3-11e9-9958-61ec5a2bd57e.jpg)
- a KEYES IR remote:
![61-sEwpOfmL _SL1200_](https://user-images.githubusercontent.com/56785/55276268-4da91280-52f2-11e9-9a47-09ee81b730af.jpg)
- Dupont Wires to connect the Arduino to the SD card module and the MZ tape connector.
- a LED to show transfer activity.

## Installation
You need SdFat, CrystalLiquid, IRremote and SSD1306Ascii libraries: they are available from Arduino IDE.

## Wiring

Arduino MEGA Pins:

 Name | Number | Direction | Description                       
 ---- | ------:|:---------:|:-----------
 A0   |        | <-        | BUTTON (UP/DOWN/LEFT/RIGHT/SELECT)
 A1   |        | <-        | IR remote data line
 .    | 4      | ->        | LCD D4
 .    | 5      | ->        | LCD D5
 .    | 6      | ->        | LCD D6
 .    | 7      | ->        | LCD D7
 .    | 8      | ->        | LCD RESET
 .    | 9      | ->        | LCD ENABLE
 RX3  | 15     | <-        | MZCMT WRITE
 TX2  | 16     | <-        | MZCMT MOTOR
 OC3B | 2      | ->        | MZCMT READ
 TX1  | 18     | ->        | MZCMT SENSE
 RX1  | 19     | ->        | MZCMT LED
 SDA  | 20     | ->        | OLED SDA (I²C)
 SCL  | 21     | ->        | OLED SCK (I²C)
 .    | 50     | <-        | SD MISO (SD Card MISO PIN)
 .    | 51     | ->        | SD MOSI (SD Card MOSI PIN)
 .    | 52     | ->        | SD SCK  (SD Card SCK PIN)
 .    | 53     | ->        | SD SS   (SD Card slave select)


LCD Pins:

 Name | Direction | Connected to                       
 ---- |:---------:|:------------
 D4   | <-        | ARDUINO #4
 D5   | <-        | ARDUINO #5
 D6   | <-        | ARDUINO #6
 D7   | <-        | ARDUINO #7
 RESET| <-        | ARDUINO #8
 VCC  | <-        | ARDUINO 5v
 GND  | <-        | ARDUINO GND

OLED Pins:

 Name | Direction | Connected to                       
 ---- |:---------:|:------------
 SDA  | <-        | ARDUINO #20
 SCK  | <-        | ARDUINO #21
 VCC  | <-        | ARDUINO 5v
 GND  | <-        | ARDUINO GND

IR RECV Pins:

 Name | Direction | Connected to                       
 ---- |:---------:|:------------
 DATA | <-        | ARDUINO #55
 VCC  | <-        | ARDUINO 5v
 GND  | <-        | ARDUINO GND


SD CARD Pins:

 Name | Direction | Connected to                       
 ---- |:---------:|:------------
 GND  | <-        | ARDUINO GND
3.3V  | <-        | NC
  5V  | <-        | ARDUINO 5V
MISO  | ->        | ARDUINO #50
MOSI  | <-        | ARDUINO #51
 SCK  | <-        | ARDUINO #52
SDCS  | <-        | ARDUINO #53

LED Pins:

 Name | Direction | Connected to                       
 ---- |:---------:|:------------
 GND  | <-        | ARDUINO GND
  5V  | <-        | ARDUINO #19

## Usage
Wire up as above, and program the Arduino using the IDE.

The following picture is showing how to connect Arduino to 8255:  
![mz-700 - 8255 <-> Arduino](https://user-images.githubusercontent.com/56785/47266539-4eb26880-d538-11e8-9fdb-7d2fadc24ca2.png)
Note: SDI/SDO/SSI/SSO are signals used for Ultra-fast mode.
- SDI: Serial Data Input, connected to WRITE. Also used to acknowledge Arduino to set the next data bit when MZ reads. 
- SDO: Serial Data Output, connected to READ. Also used to acknowledge MZ to set the next data bit when Arduino reads.
- SSI: Serial Synchronisation Input, connected to MOTOR. Only used when MZ writes.
- SSO: Serial Synchronisation Output, connected to SENSE. Only used when MZ reads.


Drop some MZF/M12/MZT files onto a FAT32 formatted SD card, plug it into the mz-sd²cmt, and power on.

An Ultra-fast mode is provided with MZF-like files to allow around 20000 baud transfer. You must press `RIGHT` button to toggle Ultra-fast mode (disabled by default).  

The following picture is showing the ultra-fast protocol when MZ reads:
![ultra-fast mode](https://user-images.githubusercontent.com/56785/43679133-2cf8ead4-9820-11e8-97a8-876965b69e71.jpg)

Legend: `*` means the MZ reads the READ bit and `><` means Arduino is setting the next READ bit.
Notes:
- CS is also named SSO.
- PC5 is connected to SDO as the current bit data when MZ reads.
- PC1 is connected to SDI as a read acknowledge sent by MZ.
- PC4 is ignored by Arduino when MZ reads. 

## Old usage
Drop some LEP or WAV files (converted MZF Files through MZF2LEP tool) onto a FAT32 formatted SD card, plug it into the mz-sd²cmt, and power on.

Tool mzf2lep can convert a MZF/MZT/M12 file into a LEP and/or WAV file in five ways:
- conventional tape data (2 header blocks followed by 2 data blocks) at 1200 baud
- fast tape data (shorter gaps, 1 header block followed by a 1 data block)
- turbo x2 tape data (first turbo loader as a fast tape data then 1 data block read twice as fast)
- turbo x3 tape data (first turbo loader as a fast tape data then 1 data block read trice as fast)
- turbo x4 tape data (first turbo loader as a fast tape data then 1 data block read four times as fast)

Note that WAV files have a limitation: 8-bit mono channel. The frequency can be any, included the usual 44.1KHz and probably even 48KHz. Yes, it works on a 16MHz AVR and probably even on a 8MHz AVR too.

## Issues

LEP file is supported. Suffixes .LEP and .L16 are for time resolution 16µs and .L50 for 50µs (As the original LEP from SDLEP-READER - Daniel Coulon). The only interest is for a program needing to read severals blocks. Maybe the same thing can be handled through a MZT file (with multiple data blocks) by listening to MOTOR signal to separate block readings. But unlike LEP, there is no way to say whether the next block is a header block or a data block.

Some programs are a set of blocks in the tape: the first program will read the rest in one or several blocks. Right now, MZF, M12 and MZT don't handle them correctly (no indication whether the next block is a header or a data so you can emit the right prolog). Maybe defining a new binary file with those indication may help to allow reading multiple data. 

Turbo x2, x3 and x4 are available through WAV/LEP files built by MZF2LEP tool. However, Turbo x4 may not work perfectly with big program to load.
