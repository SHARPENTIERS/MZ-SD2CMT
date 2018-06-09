# MZ-SD2CMT
SD card based CMT for MZ 80K series

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
 TX1  | 18     | ->        | MZCMT /SENSE
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

Drop some LEP files (converted MZF Files through mzf2lep tool) onto a FAT32 formatted SD card, plug it into the sd2mzcmt, and power on.

Tool mzf2lep can convert a MZF/MZT/M12 file into a LEP file in five ways:
- conventional tape data (2 header blocks followed by 2 data blocks) at 1200 baud
- fast tape data (shorter gaps, 1 header block followed by a 1 data block)
- turbo x2 tape data (first turbo loader as a fast tape data then 1 data block read twice as fast)
- turbo x3 tape data (first turbo loader as a fast tape data then 1 data block read trice as fast)
- turbo x4 tape data (first turbo loader as a fast tape data then 1 data block read four times as fast)

## Issues

Turbo x4 may fail for some MZF files - especially the big ones. It still needs some tuning.  



