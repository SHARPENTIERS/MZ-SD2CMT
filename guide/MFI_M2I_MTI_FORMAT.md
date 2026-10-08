# MFI / M2I / MTI playback metadata

Applies to firmware **2.0.2**.

External metadata names are explicit:

- `GAME.MZF` -> `GAME.MFI`
- `GAME.M12` -> `GAME.M2I`
- `TAPE.MZT` -> `TAPE.MTI`

There is no `.MZI` fallback. The internal source/API name `mzi_sidecar.*` is
kept only to avoid an unrelated project-wide rename.

## MFI for one MZF

Example:

```text
TYPE=NORMAL
SPEED=1:3
```

Intercopy and TurboCopy examples:

```text
TYPE=IC
SPEED=1:1
```

```text
TYPE=TC
SPEED=1:1
```

Loader without a SPEED field:

```text
TYPE=UL_MZ800
```

MFI is used only when PLAY loader selection is `AUTO`. A manually selected
loader/profile always has priority.

## M2I for one M12

M2I uses the same single-record parser, TYPE/SPEED syntax and semantics as MFI.
Only the companion extension differs. `GAME.MZF` + `GAME.MFI` and `GAME.M12` +
`GAME.M2I` can coexist. Lookup never crosses between MFI, M2I and MTI.

A compatible M12 supports the same manual profiles as MZF: all NORMAL speeds,
MZ700 1:1/FAST3, IC 1:1/1:2/1:3/1:4, TC 1:1/1:2/1:3 and UL/UL_MZ800/UL_MZ700.
Existing file-type, payload bounds, receiver placement, workspace/overlap and
loader-specific checks still apply. The extension does not select a machine.

Ready-to-copy example: [EXAMPLE.M2I](../examples/EXAMPLE.M2I) requests IC 1:2.
For `GAME.M12`, copy it to the same directory as `GAME.M2I` and select
`LOADER=AUTO`. See [example usage and companion mapping](../examples/README.md).

## MTI for an MZT container

Each MZT logical MZF record can use its own loader/profile. Record numbering is
1-based.

```text
RECORD=1
TYPE=NORMAL
SPEED=1:3

RECORD=2
TYPE=IC
SPEED=1:1

RECORD=3
TYPE=TC
SPEED=1:1

RECORD=4
TYPE=UL

RECORD=5
TYPE=UL_MZ800

RECORD=6
TYPE=UL_MZ700

RECORD=7
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
| `NORMAL` | `1:1`, `1:2`, `1:3`, `1:4` | native / historical MZ-800 timing family |
| `MZ700` | `1:1` | native MZ-700 timing |
| `MZ700` | `1:3` | MZ-700 FAST3 loader |
| `IC` | `1:1`, `1:2`, `1:3`, `1:4` | Intercopy/FASTIPL loader + IC payload timing |
| `TC` | `1:1`, `1:2`, `1:3` | Turbo Copy loader + TC payload timing |
| `UL` | none | classic Ultra Fast, automatic LOW/HIGH placement |
| `UL_MZ800` | none | MZ-800 header-only Ultra Fast |
| `UL_MZ700` | none | MZ-700 header-only Ultra Fast |

`TC 1:4` is intentionally not accepted: TurboCopy V1.22 provides the 1:1,
1:2 and 1:3 timing family used by this firmware; no verified TC 1:4 loader
readpoint/writer profile is defined.

### Copier speed bytes used by generated loaders

| Mode | Loader/readpoint byte | Source |
|---|---:|---|
| IC 1:1 | `$4D` | Intercopy V10.2 1200-Bd row |
| IC 1:2 | `$20` | Intercopy V10.2 2400-Bd row |
| IC 1:3 | `$16` | Intercopy V10.2 2800-Bd row |
| IC 1:4 | `$11` | Intercopy V10.2 3200-Bd row |
| TC 1:1 | `$52` | native MZ-800 1Z-013B DLY3 used by TC loader family |
| TC 1:2 | `$29` | TurboCopy loader family |
| TC 1:3 | `$1B` | TurboCopy loader family |

CRLF and LF are accepted. TYPE is compared case-insensitively.

## Selection rules

1. Manual PLAY loader/profile always wins.
2. MZF AUTO: missing/invalid MFI -> `NORMAL 1:1`.
3. MZT AUTO: missing MTI, missing `RECORD=n`, or invalid target section ->
   `NORMAL 1:1` for that record only. The next record is resolved again.
4. M12 AUTO: use matching valid M2I; missing/invalid M2I -> `NORMAL 1:1`.
5. `.MZI` is not read or written.

When a requested generated/turbo/UL loader fails preparation safety checks,
every Sharp source falls back to actual `NORMAL 1:1`: MZF, M12, MZT, logical
MZQ, QDF and physical HxC/FlashFloppy MFM records. Recovery resets the loader
and restores the complete normal pulse, header/data leader and mark profile;
no UL leader state survives. Direct NORMAL and MZ700 1:1 profiles retain their
selected timing. I/O errors remain errors.

The READY screen for a single Sharp file shows its effective profile (for
example `RDY N11  01:00` after rejection). The record selector likewise shows
the effective profile. The manual menu choice remains the requested setting,
so preparing another compatible record can use it again.

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
sidecar is used. M12 similarly shows `M2I` when AUTO reads a valid M2I.

## Per-record time

MZT does not display one total duration for the entire container. The active
clock and nominal duration belong only to the current logical record. When the
next MZT record becomes active, elapsed time resets to `00:00` and its own
nominal duration becomes the new total.

- NORMAL 1:1/1:2/1:3/1:4: exact current-record generated waveform duration
- MZ700 1:1: current-record duration
- MZ700 FAST3: current-record generated FAST3 duration including fixed start delay
- IC 1:1/1:2/1:3/1:4: patched header + IC payload duration
- TC 1:1/1:2/1:3: patched header + TC loader + TC payload duration
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

`.MFI`, `.M2I`, `.MTI`, and legacy `.MZI` files are metadata and are hidden from the
normal sorted browser. Filtering happens inside the shared SD browser-entry
filter, so hidden metadata does not count in the visible `N/N` position and does
not participate in previous/next sorting.
