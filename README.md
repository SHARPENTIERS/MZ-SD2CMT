# SD2MZCMT
SD card based CMT for MZ 80K series

## Parts Used
1. Arduino MEGA (but this should work on any Arduino UNO or NANO with minimal changes)
2. SD card module
3. 16x2 4-bit parallel LCD with 5 x Buttons

Examples of what I used:
- an Arduino MEGA 2560 R3 ATMEGA 16U2: https://www.amazon.fr/gp/product/B06XNPKSDK.
- a 16x2 LCD with 5 buttons: https://www.amazon.fr/gp/product/B01EYW5R5M.
- Dupont Wires to connect the Arduino to the SD card module and the MZ tape connector.
- a LED to show transfer activity.

## Installation
You need SdFat and CrystalLiquid libraries: they are available from Arduino IDE.

## Wiring

Arduino MEGA Pins:<br/>
A0        <- BUTTON (UP/DOWN/LEFT/RIGHT/SELECT)<br/>
        4 -> LCD D4<br/>
        5 -> LCD D5<br/>
        6 -> LCD D6<br/>
        7 -> LCD D7<br/>
        8 -> LCD RESET<br/>
        9 -> LCD ENABLE<br/>
RX3    15 <- MZCMT WRITE<br/>
TX2    16 <- MZCMT MOTOR<br/>
RX2    17 -> MZCMT READ<br/>
TX1    18 -> MZCMT /SENSE<br/>
RX1    19 -> MZCMT LED<br/>
       50 -> SD MOSI (SD Card MOSI PIN)<br/>
       51 -> SD MISO (SD Card MI PIN)<br/>
       52 -> SD SCK  (SD Card SCK PIN)<br/>
       53 -> SD SS   (SD Card slave select)<br/>
<br/>
<br/>
LCD PINS:<br/>
D4    <- 4 ARDUINO<br/>
D4    <- 5 ARDUINO<br/>
D4    <- 6 ARDUINO<br/>
D4    <- 7 ARDUINO<br/>
RESET <- 8 ARDUINO<br/>
VCC   <- 5v<br/>
GND   <- GND<br/>
<br/>
SD CARD PINS:<br/>
 GND <- GND<br/>
3.3V <- NC<br/>
  5V <- 5V<br/>
MOSI <- 50 ARDUINO<br/>
MISO -> 51 ARDUINO<br/>
 SCK <- 52 ARDUINO<br/>
SDCS <- 53 ARDUINO<br/>
<br/>
LED PIN:<br/>
 GND <- GND<br/>
  5V <- 19 ARDUINO<br/>
<br/>

## Usage
Wire up as above, and program the Arduino using the IDE.

Drop some LEP files (converted MZF Files through mzf2lep tool) onto a FAT32 formatted SD card, plug it into the sd2mzcmt, and power on.

Tool mzf2lep can convert a MZF/MZT/M12 file into a lep in five ways:
- conventional tape data (2 header blocks followed by 2 data blocks) at 1200 baud
- fast tape data (shorter gaps, 1 header block followed by a 1 data block)
- turbo x2 tape data (first turbo loader as a fast tape data then 1 header block and 1 data block read twice as fast)
- turbo x3 tape data (first turbo loader as a fast tape data then 1 header block and 1 data block read trice as fast)
- turbo x4 tape data (first turbo loader as a fast tape data then 1 header block and 1 data block read four times as fast)

## Issues

Turbo x4 may fail for some MZF files - especially the big ones. It still needs some tuning.  



