# MZ-SD2CMT2 – Reborn
## Quick Reference Guide

**Firmware 1.0**

This guide contains the basic controls and everyday operating steps only.

---

## Front-panel buttons

| Button | Basic function |
|---|---|
| **FFWD** | previous / up |
| **REWIND** | next / down |
| **STOP** | back / stop |
| **RECORD** | record |
| **PLAY** | select / play / confirm |

---

## SD-card browser

| Action | Control |
|---|---|
| Previous item | **FFWD** |
| Next item | **REWIND** |
| Enter folder / select file | short **PLAY** |
| Open PLAY settings | long **PLAY** |
| Go to parent folder | short **STOP** |
| Go directly to root | long **STOP** from a subfolder |
| Open SYSTEM menu | long **STOP** at root |
| Start recording with current settings | short **RECORD** |
| Open RECORD settings | long **RECORD** |

Holding **FFWD** or **REWIND** scrolls repeatedly.

### Browser counter

```text
3/28
```

means item 3 of 28.

```text
3I28
```

means item 3 of 28 and playback information is available for the selected MZF/MZT.

---

## Load and play a file

1. Select the file with **FFWD / REWIND**.
2. Press **PLAY**.
3. With `PLAY CTRL = MOTOR`, start the normal LOAD operation on the Sharp.
4. MZ-SD2CMT2 starts when the computer requests the tape.
5. Press **PLAY** during playback to pause or resume.
6. Press **STOP** to stop and return.

Recommended basic settings:

```text
PLAY CTRL = MOTOR
LOADER    = NORMAL
SPEED     = 1:1
```

If your library contains matching MFI/MTI information, use:

```text
PLAY CTRL = MOTOR
LOADER    = AUTO
```

---

## MZT multi-record files

Opening an MZT first shows its record selector.

| Action | Control |
|---|---|
| Previous record | **FFWD** |
| Next record | **REWIND** |
| Play selected record | **PLAY** |
| Return to browser | **STOP** |

During MZT playback, **STOP** first returns to the MZT record selector. Press **STOP** again to return to the main browser.

---

## PLAY controls

| Control | Action |
|---|---|
| **PLAY** | start / pause / resume |
| **STOP** | stop / back |
| **FFWD / REWIND** | select MZT record when the MZT selector is open |

Common display states:

| Display | Meaning |
|---|---|
| `RDY` | ready |
| `PLY` | playing |
| `PLA` | playing with information selected automatically |
| `PAU` | paused |
| `ERR` | error |

Pause reason at the end of the line:

```text
U = paused by user
M = paused by MOTOR control
```

---

## Recording

From the browser:

```text
short RECORD = start recording with current settings
long  RECORD = open RECORD settings
```

During recording:

| Control | Action |
|---|---|
| **PLAY** | pause / resume; also starts when manually armed |
| short **STOP** | finish and save |
| long **STOP** | cancel recording |

> **Important:** long **STOP** cancels the recording. If a capture file has already been created, it is deleted.

When stopping normally, wait while:

```text
SAVING ...
```

is displayed. The recording is finished when the saved screen appears.

All normal recordings are stored in:

```text
/RECORDINGS
```

---

## RECORD settings

Open the menu with long **RECORD**.

Use:

```text
FFWD / REWIND = move between settings
PLAY          = change / confirm
STOP          = return
```

Main choices are:

- recording type
- recording mode
- AutoName on/off

For an ordinary Sharp SAVE directly to MZF, a useful starting setup is:

```text
REC TYPE = MZF
REC MODE = MOTOR
AUTONAME = ON
```

### Recording modes

| Mode | Behaviour |
|---|---|
| `MOTOR` | follows the Sharp MOTOR signal |
| `AUTO` | waits for signal activity and stops automatically after inactivity |
| `MANUAL` | recording is controlled by the user |

---

## Display basics

A vertical bar at the right side of the display shows operating margin.

```text
high bar = good
low bar  = pay attention
```

Common messages:

| Display | Meaning / action |
|---|---|
| `INSERT CARD` | insert the SD card |
| `SD CARD ERROR` | press **RECORD** to retry |
| `EMPTY` | no visible files in the folder |
| `WAIT SIGNAL` | AUTO recording is armed |
| `PAUSED ... M` | waiting because of MOTOR control |
| `SAVING` | wait; do not remove the SD card |
| `SVD` | recording saved |
| `REC CANCELLED` | recording cancelled |
| `FILE DELETED` | cancelled recording file was deleted |

---

## PLAY settings

Open with long **PLAY** from the browser.

Use:

```text
FFWD / REWIND = move between settings
PLAY          = change / confirm
STOP          = return
```

For normal use, keep:

```text
PLAY CTRL = MOTOR
```

Use `MANUAL` only when you want playback to ignore MOTOR control.

---

## SYSTEM menu

At the root browser, long **STOP** opens the SYSTEM menu.

Use:

```text
FFWD / REWIND = move
PLAY          = change / confirm
STOP          = return
```

The ABOUT screen shows firmware version 1.0.

---

## Most important shortcuts

```text
FFWD          previous
REWIND        next
PLAY short    select / play
PLAY long     PLAY settings
STOP short    back / stop / save recording
STOP long     root or SYSTEM; during RECORD = CANCEL
RECORD short  start recording
RECORD long   RECORD settings
```

**Remember:** while recording, short **STOP** saves; long **STOP** cancels.
