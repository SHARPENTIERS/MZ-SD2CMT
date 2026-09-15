# MZ-SD2CMT2 - Reborn

**Extended SD-card CMT emulator / recorder for the Sharp MZ-800**

<p align="center">
  <img src="images/mzsd2cmt2_case_top.png" alt="MZ-SD2CMT2 enclosure" width="720">
</p>

> **MZ-SD2CMT2 is based directly on the original [MZ-SD2CMT](https://github.com/SHARPENTIERS/MZ-SD2CMT) project by Christophe Avoinne (hlide).**  
> The purpose of MZ-SD2CMT2 is to **continue and extend the original project**: improve the firmware and user interface, add playback and recording capabilities, and provide an optional integrated hardware implementation, while preserving compatibility with the original modular hardware concept.

The primary target is the **Sharp MZ-800**. The project acts as a solid-state CMT replacement using an SD card, but its design goal is not only functional replacement: the dedicated hardware version is also intended to look and feel at home next to original Sharp MZ equipment.

## Project goals

MZ-SD2CMT2 has three main goals: **preserve compatibility with the original MZ-SD2CMT modular hardware**, extend the firmware with additional playback/recording features, and provide an optional dedicated PCB and enclosure for a cleaner stand-alone build.

The dedicated version remains based on the same core CMT interface, but adds convenience hardware and a mechanical design inspired by original Sharp MZ cassette equipment.

## Main features

- SD-card file browser and direct playback without conversion on a PC
- playback of **MZF, MZT, M12, WAV, LEP, L16 and TAP** files
- recording to **MZF, WAV, LEP and L16**
- WAV recording at **44.1 kHz or 22.05 kHz, 8-bit mono**
- recording control by **MOTOR**, automatic activity detection, or manual control
- optional **automatic naming** of recordings from detected Sharp MZ tape metadata
- multiple Sharp MZ tape timing and loader profiles, including normal and turbo modes
- **Ultra Fast** loading for compatible MZF files, with automatic loader placement and fallback to standard loading when necessary
- selectable ZX Spectrum **TAP playback speed**
- optional `.MZI` sidecar metadata for MZF files to preserve an explicit playback loader/speed profile
- automatic sound-monitor activity during physical tape waveform playback/recording
- persistent hardware, playback and recording settings stored in EEPROM

This README is intentionally a **project overview rather than an operating manual**. Detailed controls, menus, recording workflow and configuration will be documented separately.

## Hardware variants

### 1. Original-compatible modular build

The original MZ-SD2CMT concept is deliberately retained as a fully supported hardware option. MZ-SD2CMT2 can therefore be built from readily available modules without the dedicated PCB. This is useful for reproducing the original project, experimenting with the firmware, or building a compact version from standard parts.

The modular implementation uses the same basic building blocks as MZ-SD2CMT: an ATmega2560 board, a 16×2 LCD/keypad interface, an SPI SD-card module and the four Sharp MZ CMT signals. Two ATmega2560 board styles are practical:

- a standard **Arduino Mega 2560 R3**, which can accept the LCD Keypad Shield directly,
- a compact **Mega 2560 Pro Mini / Mini Mega**, which provides the same ATmega2560 resources in a much smaller module and is convenient for a self-contained enclosure.

#### Arduino Mega 2560 R3

<p align="center">
  <img src="images/module_mega2560_r3.png" alt="Arduino Mega 2560 R3 used by the original-compatible modular build" width="720">
</p>

The full-size Mega 2560 follows the most straightforward construction style: the LCD Keypad Shield can be fitted directly on the Arduino headers and the SD-card module and CMT signals are connected with wiring. This is the closest arrangement to the original MZ-SD2CMT module-based implementation.

**What to look for when buying:** a full-size **Mega 2560 R3-compatible 5 V / 16 MHz board** with the ATmega2560 and the standard Mega shield header layout. The photograph above is a real board and is intended as a visual identification aid.

#### Mini Mega / Mega 2560 Pro Mini

<p align="center">
  <img src="images/module_mega2560_pro_mini.png" alt="Mini Mega 2560 module" width="720">
</p>

The compact Mini Mega version keeps the ATmega2560 platform while reducing the physical size substantially. It is especially suitable when the modular electronics are to be placed in a small stand-alone case rather than left as separate development boards.

**What to look for when buying:** this board is commonly listed as **Mega 2560 Pro Mini**, **Mini Mega 2560**, **Mega Pro Embed** or similar. Choose the **ATmega2560, 5 V, 16 MHz** version. The photograph above shows the compact board style used as the reference for this project.

For this version the repository already contains printable enclosure parts in the `STL/KeypadShield` directory:

- [`STL/KeypadShield/Cover.stl`](STL/KeypadShield/Cover.stl) — enclosure/cover,
- [`STL/KeypadShield/Button.stl`](STL/KeypadShield/Button.stl) — printable button part.

These STL files allow the Mini Mega + keypad-shield implementation to be built as a compact finished unit while still using the original modular electrical concept.

#### 16×2 LCD Keypad Shield

<p align="center">
  <img src="images/module_keypad_shield.png" alt="16x2 LCD Keypad Shield" width="720">
</p>

The firmware retains support for the familiar 16×2 LCD with five analogue-keypad buttons. The keypad shield provides the display and the basic navigation/transport controls without requiring a custom front-panel PCB.

**What to look for when buying:** the common **1602 / HD44780-compatible LCD Keypad Shield** with five navigation buttons read through the analogue resistor ladder on **A0**. The real photograph above matches the classic shield style used by the original MZ-SD2CMT project.

#### SD-card module

<p align="center">
  <img src="images/module_sd_card.png" alt="SPI SD-card module with 5 V level shifting" width="720">
</p>

The modular build uses a standard SPI SD-card adapter with 5 V-compatible level shifting. On the Mega 2560 the hardware SPI interface is connected through pins **50–53** (`MISO`, `MOSI`, `SCK`, `SS/CS`).

**What to look for when buying:** use an **SD-card SPI module intended for 5 V Arduino boards**, with an onboard **3.3 V regulator and logic-level conversion**. The original-style full-size module usually exposes `GND`, `3.3V`, `5V`, `CS/SDCS`, `MOSI`, `SCK`, `MISO` and `GND`. The real photograph above is included as a visual guide; modules with the same electrical function may have a different PCB colour or layout.

Connection details for the Sharp MZ CMT interface are collected in the dedicated **CMT interface and compatibility** section below, so the wiring reference is shown only once in this README.

### 2. Dedicated MZ-SD2CMT2 hardware

The dedicated hardware combines the electronics into a purpose-built unit and adds:

- **SD-card presence detection**, allowing the firmware to detect card insertion/removal instead of relying only on failed file access
- a selectable **internal MZ-SD2CMT2 / external CMT path**, allowing a real external cassette recorder or another CMT source to remain connected and be selected without rewiring
- **audio monitoring** of the tape signal during playback and recording
- integrated connectors and wiring intended for a permanent installation
- a dedicated mechanical arrangement for the LCD, five control buttons, SD-card access and CMT connections

#### USB programming from PlatformIO

The dedicated PCB is designed so that the Mega/ATmega2560 firmware can be programmed and updated through the **USB connection directly from PlatformIO**. For normal firmware updates this avoids the need to connect a separate ISP programmer.

> **Important:** direct USB upload requires an **Arduino Mega 2560-compatible bootloader** to be present in the ATmega2560. If a blank/replacement ATmega2560 is fitted, or the Mega module does not contain the bootloader, the Arduino Mega 2560 bootloader must first be programmed via **ISP**. Once the bootloader is installed, subsequent firmware uploads can be performed normally through USB using PlatformIO/AVRDUDE.

#### Mechanical design

A major goal of the dedicated version is for the finished device to **visually resemble an original Sharp MZ CMT/cassette unit as closely as is practical**, rather than looking like a generic Arduino box.

The enclosure is therefore designed around the visual language of period Sharp hardware: a low, slightly wedge-shaped body, an inclined upper control panel, a recessed display area, a row of transport-style controls and horizontal grille/vent details. The intention is **not to make an exact 1:1 reproduction of a specific Sharp cassette mechanism**, but to create a modern SD-based CMT replacement that looks appropriate beside an MZ-800 and preserves the character of the original equipment.

The dedicated PCB is designed together with the enclosure, so the positions of the display, buttons, SD-card access and connectors are part of the mechanical concept rather than an afterthought.

For the dedicated PCB version, the repository contains the matching **3D-printable mechanical parts** in the [`STL/PCB`](STL/PCB) directory:

- [`STL/PCB/Cover.stl`](STL/PCB/Cover.stl) — enclosure cover,
- [`STL/PCB/Distancer.stl`](STL/PCB/Distancer.stl) — mechanical spacer,
- [`STL/PCB/FFD_REW.stl`](STL/PCB/FFD_REW.stl) — fast-forward / rewind control,
- [`STL/PCB/Play.stl`](STL/PCB/Play.stl) — PLAY control,
- [`STL/PCB/Rec.stl`](STL/PCB/Rec.stl) — REC control,
- [`STL/PCB/Stop.stl`](STL/PCB/Stop.stl) — STOP control.

These STL files form the mechanical counterpart to the dedicated PCB and allow the enclosure and transport-style controls shown below to be reproduced without having to design a separate case. There is a space between knobs and tact switches for double adhesive foam to dampen too much click feel.

<table>
<tr>
<td align="center"><b>PCB - component side</b></td>
<td align="center"><b>PCB - opposite side</b></td>
</tr>
<tr>
<td><img src="images/mzsd2cmt2_pcb_top.png" alt="MZ-SD2CMT2 PCB top" width="420"></td>
<td><img src="images/mzsd2cmt2_pcb_bottom.png" alt="MZ-SD2CMT2 PCB bottom" width="420"></td>
</tr>
</table>

<p align="center">
  <img src="images/mzsd2cmt2_case_open.png" alt="MZ-SD2CMT2 internal arrangement" width="760">
</p>

<p align="center"><em>Internal arrangement of the dedicated PCB below the upper control panel.</em></p>

## CMT interface and compatibility with MZ-SD2CMT

MZ-SD2CMT2 retains the CMT signal arrangement of the original MZ-SD2CMT project. This is the key hardware compatibility point that allows both the original-style modular build and the dedicated PCB to use the same firmware architecture.

<p align="center">
  <img src="images/original_8255_connection.png" alt="MZ-SD2CMT compatible Sharp MZ 8255 CMT wiring reference" width="900">
</p>

<p align="center"><em>Original MZ-SD2CMT 8255/CMT connection diagram and pin assignment.</em></p>

| Signal | Arduino Mega pin | Function |
|---|---:|---|
| `WRITE / SDI` | 15 | data from the Sharp MZ to the CMT interface |
| `MOTOR / SSI` | 16 | motor/control input from the Sharp MZ |
| `READ / SDO` | 2 | data from the CMT interface to the Sharp MZ |
| `SENSE / SSO` | 18 | tape sense/control output to the Sharp MZ |

These four signals are also used by the original MZ-SD2CMT Ultra-Fast transfer method. The dedicated MZ-SD2CMT2 PCB adds card detection, external-CMT switching and audio-monitor circuitry around this core interface without changing the basic connection concept.

> **Dedicated PCB schematic:** the electrical schematic of the MZ-SD2CMT2 board will be documented separately with the hardware files. The PCB/enclosure images in this README are mechanical/3D design renders, not the electrical schematic.

## Software structure

The current firmware targets the **ATmega2560**. Time-critical tape generation and capture use AVR timers and interrupts, while SD access, file parsing, UI and metadata processing run in the foreground.

The source tree is divided into functional areas including:

- `src/play` — playback engines, loaders and timing profiles
- `src/record` — WAV/LEP/L16/MZF recording and automatic naming
- `src/formats` — file-format detection, tape profiles and MZI metadata
- `src/drivers` — LCD, keypad, SD card, CMT I/O, external-CMT switching and sound monitor
- `src/ui` — browser, menus and transport screens

## Origins, references and acknowledgements

MZ-SD2CMT2 builds on earlier work from the Sharp MZ and retro-computing community. The following projects, formats and authors were used either as a direct starting point or as technical references during development:

| Project / format | Author(s) | Contribution / relevance |
|---|---|---|
| **[MZ-SD2CMT](https://github.com/SHARPENTIERS/MZ-SD2CMT)** | **Christophe Avoinne (hlide)** / SHARPENTIERS | **Direct predecessor and primary foundation of MZ-SD2CMT2.** Hardware concept, Sharp MZ CMT interface, file playback and Ultra-Fast transfer work. |
| **MZF2LEP** ([included in MZ-SD2CMT](https://github.com/SHARPENTIERS/MZ-SD2CMT/tree/master/Tools/mzf2lep)) | **Christophe Avoinne (hlide)** | MZF-to-LEP/WAV conversion and Sharp MZ tape waveform/timing work. MZF2LEP itself was based on MZF2WAV. |
| **MZF2WAV** | **Jeroen F. J. Laros** | Earlier MZF-to-audio conversion code on which MZF2LEP was based. |
| **[SDLEP-READER / LEP format](https://dcmoto.pages-perso.free.fr/bricolage/sdlep-reader/index.html)** | **Daniel Coulom** | LEP pulse-duration format and SD-based virtual cassette reader concept. |
| **[ZX Spectrum TAP format](https://sinclair.wiki.zxnet.co.uk/wiki/TAP_format)** | ZX Spectrum community / historical TAP documentation | TAP block structure and ZX Spectrum tape encoding/timing reference used for TAP playback support. |
| **[mz800emu](https://github.com/michalhucik/mz800emu)** | **Michal Hucik (ordoz)** | Reference material for Sharp MZ hardware, CMT behaviour and file formats. |
| **[TapeMZ](https://github.com/michalhucik/TapeMZ)** | **Michal Hucik (ordoz)** | Research and implementation covering Sharp MZ tape formats, loaders, timing profiles, WAV analysis and the TMZ archival format. |

Special thanks to the original authors, maintainers and contributors. MZ-SD2CMT2 is intended to extend this body of work while preserving clear attribution to the projects from which code, ideas, file formats or technical information originated.

## Development status

MZ-SD2CMT2 is under active development. Some functionality, timing profiles and hardware options may still change as testing on real Sharp MZ hardware continues.

For detailed operating instructions, use the separate project guide rather than this README.

## License

See [`LICENSE`](LICENSE).
