# MFI / MTI playback metadata

External metadata names are now explicit:

- `GAME.MZF` -> `GAME.MFI`
- `TAPE.MZT` -> `TAPE.MTI`

There is no `.MZI` fallback. The internal source/API name `mzi_sidecar.*` is
kept only to avoid an unrelated project-wide rename.

## MFI for one MZF

Example:

```text
TYPE=NORMAL
SPEED=1:3
```

Loader without a SPEED field:

```text
TYPE=UL_MZ800
```

MFI is used only when PLAY loader selection is `AUTO`. A manually selected
loader/profile always has priority.

## MTI for an MZT container

Each MZT logical MZF record can use its own loader/profile. Record numbering is
1-based.

```text
RECORD=1
TYPE=NORMAL
SPEED=1:3

RECORD=2
TYPE=UL

RECORD=3
TYPE=UL_MZ800

RECORD=4
TYPE=UL_MZ700

RECORD=5
TYPE=MZ700
SPEED=1:3
```

The MTI parser is streaming. It uses the existing shared SD work buffer and
does not build a record table in SRAM. The SD layer owns one sequential stream,
so AUTO MZT temporarily closes the MZT, reads only the requested MTI section,
then reopens the MZT and seeks back to the saved position.

## Supported TYPE / SPEED combinations

| TYPE | SPEED | Firmware mode |
|---|---|---|
| `NORMAL` | `1:1`, `1:2`, `1:3`, `1:4` | native MZ-800 timing |
| `MZ700` | `1:1` | native MZ-700 timing |
| `MZ700` | `1:3` | MZ-700 FAST3 loader |
| `IC` | `1:2`, `1:3`, `1:4` | IC turbo |
| `TC` | `1:2`, `1:3`, `1:4` | Turbo Copy loader/timing |
| `UL` | none | classic Ultra Fast, automatic LOW/HIGH placement |
| `UL_MZ800` | none | MZ-800 header-only Ultra Fast |
| `UL_MZ700` | none | MZ-700 header-only Ultra Fast |

CRLF and LF are accepted. TYPE is compared case-insensitively.

## Selection rules

1. Manual PLAY loader/profile always wins.
2. MZF AUTO: missing/invalid MFI -> `NORMAL 1:1`.
3. MZT AUTO: missing MTI, missing `RECORD=n`, or invalid target section ->
   `NORMAL 1:1` for that record only. The next record is resolved again.
4. M12 AUTO remains `NORMAL 1:1` and has no MFI/MTI lookup.
5. `.MZI` is not read or written.

## MZT record selector

Opening an MZT prepares record 1 but does not immediately arm/start the tape.
The PLAY screen first acts as a lightweight record selector:

- `UP` = previous record, with wrap
- `DOWN` = next record, with wrap
- `SELECT` = confirm/start the selected record
- `LEFT` = return to browser

No index array is retained. Every selection rescans the MZT headers and seeks to
the selected record. The LCD shows the selected record number/count, the MZF
header title, loader/profile and the duration of that record.

When an MTI exists, the selector explicitly shows `MTI`; MZT line 0 also marks
the record counter with `I`. For MZF, line 0 explicitly shows `MFI` when its
sidecar exists.

## Per-record time

MZT no longer displays one total duration for the entire container. The active
clock and nominal duration belong only to the current logical record. When the
next MZT record becomes active, elapsed time resets to `00:00` and its own
nominal duration becomes the new total.

- NORMAL 1:1/1:2/1:3/1:4: exact current-record generated waveform duration
- MZ700 1:1: current-record duration
- MZ700 FAST3: current-record generated FAST3 duration including its fixed start delay
- IC 1:2/1:3/1:4: current-record patched header + turbo payload duration
- TC 1:2/1:3/1:4: current-record patched header + TC loader + turbo payload duration
- UL / UL_MZ800 / UL_MZ700: unknown (`--:--`) because payload timing is governed
  by the live WRITE/SENSE handshake

MOTOR pauses are not included in the nominal record duration.

## UL / UL800 / UL700 boundaries inside MZT

A completed Ultra Fast payload returns control to the loaded program. Therefore
an UL record may **not** automatically feed the next MZT record.

For an MZT containing another record after UL/UL800/UL700:

1. finish the UL payload,
2. park at the next MZT boundary,
3. MOTOR mode requires a real `LOW -> HIGH` cycle before the next record starts,
4. MANUAL mode requires a new `SELECT` press.

This permits several UL records in one MZT as separate LOAD operations without
incorrectly assuming the Z80 is already waiting for the following header.

Every new MZT record is prepared from its original 128-byte header. UL/UL800/
UL700/MZ700 FAST3/IC/TC loader generation and LOW/HIGH placement are therefore
re-evaluated independently for that record, and the loader-visible file end is
clamped exactly to that record's payload.

## Browser behavior

`.MFI`, `.MTI`, and legacy `.MZI` files are metadata and are hidden from the
normal sorted browser. Filtering happens inside the shared SD browser-entry
filter, so hidden metadata does not count in the visible `N/N` position and does
not participate in previous/next sorting.
