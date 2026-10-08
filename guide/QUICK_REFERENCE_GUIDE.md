# MZ-SD2CMT2 – Reborn
## Quick Reference Guide

**Firmware 2.0.2**

This guide contains the basic controls and everyday operating steps only.

---

## Front-panel buttons

| Button | Basic function |
|---|---|
| **REWIND** | previous / up |
| **FFWD** | next / down |
| **STOP** | back / stop |
| **RECORD** | record |
| **PLAY** | select / play / confirm |

---

## SD-card browser

| Action | Control |
|---|---|
| Previous item | **REWIND** |
| Next item | **FFWD** |
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

If your library contains matching MFI/M2I/MTI information, use:

```text
PLAY CTRL = MOTOR
LOADER    = AUTO
```

Exact companions: MZF -> MFI, M12 -> M2I, MZT -> MTI. M2I uses MFI syntax;
there is no cross-format lookup. Manual selection wins. Compatible M12 files
support the same loaders as MZF. An unsafe generated/turbo/UL request falls
back to complete NORMAL 1:1 timing for all Sharp files, including physical
QD; READY/selector shows the effective profile. Metadata files are hidden.

For an IC 1:2 M12 example, copy [EXAMPLE.M2I](../examples/EXAMPLE.M2I) beside
your tape and rename it to the same basename (`GAME.M12` + `GAME.M2I`).
See [all sidecar templates](../examples/README.md).

---

## MZT, MZQ, QDF and QD multi-record images

Opening an MZT, MZQ, QDF or QD first shows its record selector. A QuickDisk entry is
played as one MZF-style tape record; the image itself remains read-only. QD
format selection is automatic for logical QDF/MZQ, HxC and FlashFloppy images.
QDF and physical images show `QD SCAN n% / STOP=CANCEL` during their initial scan.
Press **STOP** to cancel the scan and return to the SD-card browser. Up to 50
files can be imported from one QuickDisk image.

| Action | Control |
|---|---|
| Previous record | **REWIND** |
| Next record | **FFWD** |
| Play selected record | **PLAY** |
| Return to browser | **STOP** |

During container playback, **STOP** first returns to the record selector. Press **STOP** again to return to the main browser.

QuickDisk images are read-only. MFI/M2I/MTI sidecars do not apply to them; with
`LOADER=AUTO`, playback uses the safe `NORMAL 1:1` fallback.

---

## PLAY controls

| Control | Action |
|---|---|
| **PLAY** | start / pause / resume |
| **STOP** | stop / back |
| **FFWD / REWIND** | select an MZT/MZQ/QDF/QD record when its selector is open |

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

The ABOUT screen shows firmware version 2.0.

---

## Preparing files on a PC

[MZTools](https://github.com/bales0/MZTools) can prepare and convert MZF/MZT
files, generate MFI/MTI metadata and export LEP/L16/WAV waveforms.

---

## Most important shortcuts

```text
REWIND        previous
FFWD          next
PLAY short    select / play
PLAY long     PLAY settings
STOP short    back / stop / save recording
STOP long     root or SYSTEM; during RECORD = CANCEL
RECORD short  start recording
RECORD long   RECORD settings
```

**Remember:** while recording, short **STOP** saves; long **STOP** cancels.
