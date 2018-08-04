# MZ-SD²CMT
SD card based CMT for MZ 80K series (MZ-80 K/K2/K2E/C, MZ-80 A, MZ-1200, MZ-700).

## Parts Used
1. Arduino MEGA (but this should work on any Arduino UNO or NANO with minimal changes)
2. SD card module
3. 16x2 4-bit parallel LCD with 5 x Buttons

Examples of what I used:
- an Arduino MEGA 2560 R3 ATMEGA 16U2: [Amazon](https://www.amazon.fr/gp/product/B06XNPKSDK), [MiniInTheBox](https://www.miniinthebox.com/fr/p/funduino-mega-2560-conseil-du-developpement-r3_p903322.html).
- a 16x2 LCD with 5 buttons: [Amazon](https://www.amazon.fr/gp/product/B01EYW5R5M), [MiniInTheBox](https://www.miniinthebox.com/fr/p/16-x-2-bouclier-clavier-lcd-pour-arduino-uno-duemilanove-mega_p340888.html).
- Dupont Wires to connect the Arduino to the SD card module and the MZ tape connector.
- a LED to show transfer activity.

## Installation
You need SdFat and CrystalLiquid libraries: they are available from Arduino IDE.

## Wiring

Arduino MEGA Pins:

 Name | Number | Direction | Description                       
 ---- | ------:|:---------:|:-----------
 A0   |        | <-        | BUTTON (UP/DOWN/LEFT/RIGHT/SELECT)
 .    | 4      | ->        | LCD D4
 .    | 5      | ->        | LCD D5
 .    | 6      | ->        | LCD D6
 .    | 7      | ->        | LCD D7
 .    | 8      | ->        | LCD RESET
 .    | 9      | ->        | LCD ENABLE
 RX3  | 15     | <-        | MZCMT WRITE
 TX2  | 16     | <-        | MZCMT MOTOR
 RX2  | 17     | ->        | MZCMT READ
 TX1  | 18     | ->        | MZCMT SENSE
 RX1  | 19     | ->        | MZCMT LED
 .    | 50     | ->        | SD MOSI (SD Card MOSI PIN)
 .    | 51     | <-        | SD MISO (SD Card MI PIN)
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
 VCC  | <-        | ARDUINO 5v<br/>
 GND  | <-        | ARDUINO GND<br/>


SD CARD Pins:

 Name | Direction | Connected to                       
 ---- |:---------:|:------------
 GND  | <-        | ARDUINO GND
3.3V  | <-        | NC
  5V  | <-        | ARDUINO 5V
MOSI  | <-        | ARDUINO #50
MISO  | ->        | ARDUINO #51
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
![mz-700 - 8255 <-> Arduino](https://user-images.githubusercontent.com/56785/42876668-a6c55d26-8a87-11e8-8afd-9b2e94d2cd26.png)
Note: you must disregard the blue connection (CMT SCK) as it is now handled through the green connection (CMT SENSE).

Drop some MZF/M12/MZT files onto a FAT32 formatted SD card, plug it into the sd2mzcmt, and power on.

An Ultra-fast mode is provided with MZF-like files to allow around 20000 baud transfer. You must press `RIGHT` button to toggle Ultra-fast mode (disabled by default).  

![ultra-fast mode](https://user-images.githubusercontent.com/56785/43679133-2cf8ead4-9820-11e8-97a8-876965b69e71.jpg)
Legend: `*` means the MZ reads the READ bit and `><` means Arduino is setting the next READ bit.

## Old usage
Drop some LEP files (converted MZF Files through mzf2lep tool) onto a FAT32 formatted SD card, plug it into the sd2mzcmt, and power on.

Tool mzf2lep can convert a MZF/MZT/M12 file into a LEP file in five ways:
- conventional tape data (2 header blocks followed by 2 data blocks) at 1200 baud
- fast tape data (shorter gaps, 1 header block followed by a 1 data block)
- turbo x2 tape data (first turbo loader as a fast tape data then 1 data block read twice as fast)
- turbo x3 tape data (first turbo loader as a fast tape data then 1 data block read trice as fast)
- turbo x4 tape data (first turbo loader as a fast tape data then 1 data block read four times as fast)

NOTE: The same usage applies to WAV files.

## Issues

WAV file is supported but may be buggy with big program to load.

LEP file is supported. Suffixes .LEP and L16 are for time resolution 16µs and L50 for 50µs (As the original LEp from SDLEP-READER - Daniel Coulon). The only interest is for a program needing to read severals blocks. Maybe the same thing can be handled through a MZT file (with multiple data blocks) by listening to MOTOR signal to separate block readings. But unlike LEP, there is no way to say whether the next block is a header block or a data block.

Some programs are a set of blocks in the tape: the first program will read the rest in one or several blocks. Right now, MZF, M12 and MZT don't handle them correctly (no indication whether the next block is a header or a data so you can emit the right prolog). Maybe defining a new binary file with those indication may help to allow reading multiple data. 

Turbo x2, x3 and x4 are available through WAV/LEP files built by MZF2LEP tool. However, Turbo x4 may not work perfect with big program to load.
