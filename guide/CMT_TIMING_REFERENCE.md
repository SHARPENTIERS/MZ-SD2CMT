# SD2CMT2 CMT Timing Reference
## ROM/loader-derived calibration reference

**Document purpose:** stable calibration source for future changes to CMT/PWM timing in `bales0/MZ-SD2CMT2`.

**Reference revision:** 2 (2026-08-29). This revision closes the native MZ-800 low-monitor analysis and separates MZ-800 IC, MZ-800 TC and MZ-700 FAST3 turbo-loader timing.

**Scope:** Sharp MZ CMT modes implemented in SD2CMT2 at baseline commit
`fabf8fc0d032a0363b991be2c1e388fc5657d2c5` (`TAP playback percentage adjustment`).

**Explicitly out of scope:** TAP/Sinclair. TAP must receive its own ROM/loader audit.

**Important:** this document intentionally distinguishes three different things that must never be conflated:

1. a waveform physically **generated** by a monitor ROM,
2. a receiver **decision/read point** implemented by a loader,
3. an empirically selected SD2CMT2 waveform that is merely required to fall safely on opposite sides of that read point.

The old 44.1 kHz WAV references remain useful as regression material, but they are **not the primary timing authority**.

---

## 1. Frozen inputs

### 1.1 Supplied ROM images

| File | Meaning | Size [B] | SHA-256 |
|---|---|---:|---|
| `1z-013a(2).rom` | MZ-700 PAL monitor 1Z-013A | 4096 | `ae1d65717414d4693158cbbd9ab707aa43e4e1ac0b2ae0e13ee3f68c66cba81b` |
| `mz700_1z-009a_jp(1).rom` | MZ-700 Japanese monitor 1Z-009A | 4096 | `b0d16889ac3e2a80cc3bc9445bc95bc9988df7b6115124f284850667cf45ff9f` |
| `9Z_504M(4).ROM` | MZ-800 high monitor 9Z-504M | 8192 | `68a91b82517d5642e250cdddb519de780fb20bcde39b2de8baee387ca6bf446a` |
| `Mz700a(1).rom` | MZ-800 low monitor 1Z-013B (binary identifies itself as `** MONITOR 1Z-013B **`) | 4096 | `fa65299ff588e3402973afbb1285dc5a7993efe9265213b64ba8e3a6facd2acf` |

### 1.2 SD2CMT2 source baseline

Repository:

`https://github.com/bales0/MZ-SD2CMT2`

Frozen commit:

`fabf8fc0d032a0363b991be2c1e388fc5657d2c5`

The repository head was re-checked on 2026-08-29; this commit is still the latest commit on `master`.

Important files at that commit:

- `src/formats/mz_tape_profiles.cpp`
- `src/formats/mz_tape_profiles.h`
- `src/formats/mz_loader_profiles.cpp`
- `src/play/mzf_loader.cpp`
- `src/play/mzf_playback.cpp`
- `src/play/mz700_fast3.cpp`
- `src/play/loader_mode.h`
- `TEST/MZ700/verify_mz700_fast3.ps1`

Relevant source blob IDs at the frozen commit:

```text
mz_tape_profiles.cpp   482414a8bba21e5787c325f02eed231eabcdf247
mzf_playback.cpp       f8429deb2fb9007a27626355242ef7b1f610dcfd
mzf_loader.cpp         1f45f2a7e3c20e91b6313e7d5c99d86f3880b7cc
mz_loader_profiles.cpp 3df3f1a41d67c582db30ff4cdaafdbcd502da029
mz700_fast3.cpp        ef24f178d995c13a6c16259536a75051b6a2fdcc
```

Embedded loader byte arrays additionally fingerprint to:

```text
IC loader, 96 B: SHA-256 a52224f33e5ad8ca6d436257f58ef5802c4a4a8a7c8e0798e31a0a968a6eeab2
TC loader, 90 B: SHA-256 4ab47c752ae0d04ce341d9be221336ed51b30a9c698d09aed4370172917ee790
```

If any of these inputs changes, this document is no longer automatically authoritative for the changed code.

---

## 2. Evidence and confidence rules

Use the following precedence when calibrating or reviewing a timing change.

| Level | Label | Meaning |
|---|---|---|
| 1 | `ROM-PATH-HIGH` | Supplied ROM bytes + deterministic Z80 path + documented hardware wait model |
| 1 | `LOADER-PATH-HIGH` | Exact loader bytes + exact patch value + deterministic RAM-executed Z80 path |
| 2 | `OFFICIAL-HW` | Sharp service/technical manual timing or source listing |
| 3 | `HW-TESTED` | Proven on a physical MZ machine, but exact timing origin may be empirical |
| 4 | `EMPIRICAL-WAV` | Derived from sampled WAV/PCM; quantized and not a source of exact CPU timing |
| special | `HANDSHAKE` | Mode is not pulse-width PWM; SHORT/LONG timing is not applicable |
| open | `TRACE-PENDING` | Static analysis exists, but cycle-accurate runtime trace is still desirable |

A WAV can confirm polarity, framing and compatibility. It must not silently overwrite a ROM/loader-derived timing equation.

---

## 3. Clock and timer model

### 3.1 Z80 clocks used here

```text
MZ-700 PAL / MZ-800 PAL: 3,546,875 Hz
MZ-700 Japanese/NTSC:    3,579,545 Hz
ATmega2560 SD2CMT2:     16,000,000 Hz
```

For ATmega2560 Timer3 with prescaler 1:

```text
1 timer tick = 62.5 ns
ticks = round(seconds * 16,000,000)
```

### 3.2 Monitor-ROM wait-state model

The published 1Z-013A source listing annotates timings such as:

```text
PUSH AF       12 T
LD A,n         9 T
LD (nn),A     16 T
CALL nn       20 T
DEC A          5 T
JP NZ,nn      13 T
RET            11 T
```

These are consistent with the standard Z80 timing plus one wait state for
each instruction byte fetched from monitor ROM. Example:

```text
LD A,n:      7 + 2 ROM-byte waits = 9 T
CALL nn:    17 + 3 ROM-byte waits = 20 T
JP NZ,nn:   10 + 3 ROM-byte waits = 13 T
```

This matters enormously. A PC emulator that executes the same instructions
with plain Z80 timings can produce a waveform roughly 20–30 % too fast even
though the instruction sequence itself is correct.

The supplied `MZ700_monitor_speed.wav` observed earlier is therefore treated
only as an emulator-output reference. Its 8/9 and 16/17 sample pulse halves
are consistent with missing/reduced real monitor-ROM wait time and 44.1 kHz
quantization; they are **not** the calibration authority used below.

For loaders that first copy the monitor to RAM and execute the patched CMT
receiver from RAM, this ROM-fetch penalty is removed. For those paths this
audit uses normal Z80 instruction timings. A future cycle-accurate runtime
trace should still verify that no machine-specific RAM wait is inserted.

---

## 4. Critical concept: a ROM pulse does not always have one fixed LOW width

`SHORT` and `LONG` explicitly create the rising and falling CMT writes, but
after the falling edge the routine returns to its caller. The time until the
next rising edge therefore includes caller instructions.

Consequently:

```text
HIGH width = largely intrinsic to SHORT/LONG routine
LOW width  = intrinsic delay + caller/framing/next-bit path
```

For example, a SHORT in the leader, a SHORT in a tape mark and a SHORT before
a LONG data bit do not have exactly the same LOW duration.

Therefore a firmware structure containing only:

```text
short_high, short_low, long_high, long_low
```

is necessarily a fixed approximation of a software-generated ROM waveform.
This is not a bug in SD2CMT2; it is a modeling decision that must be explicit.

---

## 5. MZ-700 monitor ROM analysis

### 5.1 Common delay routines

Both supplied MZ-700 ROMs contain:

```asm
0759  3E 15       LD A,$15        ; DLY1
075B  3D          DEC A
075C  C2 5B 07    JP NZ,$075B
075F  C9          RET

0760  3E 13       LD A,$13        ; DLY2
0762  3D          DEC A
0763  C2 62 07    JP NZ,$0762
0766  C9          RET
```

Using the monitor-ROM wait model:

```text
CALL DLY1 = 20 + 18*21 + 20 = 418 T
CALL DLY2 = 20 + 18*19 + 20 = 382 T
```

### 5.2 1Z-013A PAL

Supplied ROM SHA-256:

`ae1d65717414d4693158cbbd9ab707aa43e4e1ac0b2ae0e13ee3f68c66cba81b`

Relevant routines:

```text
WBYTE $0767
SHORT $0A01
LONG  $0A1B
DLY4  $09A9, operand $59 = 89
DLY3  $0A4A, operand $3F = 63
EDGE  $0601
RBYTE $0624
```

`SHORT` uses two `DLY1` calls per phase.

Exact rising-to-falling interval:

```text
16 + 2*418 + 9 = 861 T
861 T / 3,546,875 Hz = 242.749 us
```

`LONG` uses `DLY4`, whose call-inclusive time is:

```text
CALL DLY4 = 20 + 18*89 + 20 = 1642 T

LONG HIGH = 16 + 1642 + 9 = 1667 T
          = 469.991 us
```

### 5.3 1Z-009A Japanese/NTSC

Supplied ROM SHA-256:

`b0d16889ac3e2a80cc3bc9445bc95bc9988df7b6115124f284850667cf45ff9f`

Relevant routines:

```text
WBYTE $076D
SHORT $0D47
LONG  $0D60
DLY3  $0A4A, operand $45 = 69
EDGE  $0601
RBYTE $0624
```

`SHORT` is cycle-equivalent to 1Z-013A.

The Japanese `LONG` is important: it is not the same implementation as the
PAL ROM. Its first phase uses:

```text
DLY1 + DLY1 + DLY1 + DLY2
```

and its second phase uses:

```text
DLY1 + DLY1 + DLY1 + DLY1
```

Hence:

```text
LONG HIGH = 16 + 3*418 + 382 + 9
          = 1661 T
          = 464.025 us at 3.579545 MHz
```

This result is extremely close to Sharp's nominal MZ-700 service value of
464 us.

### 5.4 Exact context-dependent MZ-700 pulse table

Each cell below is `T-states / microseconds`.

| ROM | CPU [Hz] | SHORT HIGH | SHORT LOW leader | SHORT LOW mark | SHORT LOW S→L | SHORT LOW S→S | LONG HIGH | LONG LOW mark | LONG LOW L→L | LONG LOW L→S |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `MZ700_1Z013A_PAL` | 3,546,875 | 861 / 242.749 | 946 / 266.714 | 934 / 263.330 | 938 / 264.458 | 951 / 268.123 | 1667 / 469.991 | 1740 / 490.573 | 1757 / 495.366 | 1770 / 499.031 |
| `MZ700_1Z009A_JP` | 3,579,545 | 861 / 240.533 | 946 / 264.279 | 934 / 260.927 | 938 / 262.044 | 951 / 265.676 | 1661 / 464.025 | 1770 / 494.476 | 1787 / 499.225 | 1800 / 502.857 |

Notes:

- `leader`: repeated SHORT in the BC leader loop.
- `mark`: repeated pulse in the D/E tape-mark loop.
- `S→L`, `S→S`, `L→L`, `L→S`: steady WBYTE data-loop transitions.
- The LOW path after the last bit/checksum can have another caller-dependent
  value; do not infer a universal LOW value from this table.

### 5.5 Why the current MZ-700 profile works

Current SD2CMT2:

```text
SHORT = 240 us HIGH + 264 us LOW
LONG  = 464 us HIGH + 494 us LOW
```

For 1Z-009A this is an excellent fixed representation of the canonical
pulse portions:

| Quantity | 1Z-009A derived | SD2CMT2 | Error |
|---|---:|---:|---:|
| SHORT HIGH | 240.533 us | 240 us | -0.533 us |
| SHORT LOW, leader | 264.279 us | 264 us | -0.279 us |
| LONG HIGH | 464.025 us | 464 us | -0.025 us |
| LONG LOW, mark | 494.476 us | 494 us | -0.476 us |

This also matches the Sharp MZ-700 service-manual nominal values
240/264/464/494 us.

**Calibration conclusion:** do not replace the current MZ-700 NORMAL
240/264/464/494 profile with the much shorter values measured from the
emulator WAV. The current profile has substantially stronger ROM/hardware
support than the WAV-derived alternative.

The 1Z-013A PAL ROM is measurably different. If a future project version
needs strict PAL-1Z-013A waveform fidelity, add a separate profile instead
of silently redefining `MZ700_NORMAL_1X`.

### 5.6 Reference Timer3 ticks for MZ-700 source values

These are *reference conversions*, not an instruction to change firmware.

| ROM/path | T | us | nearest Timer3 ticks @16 MHz |
|---|---:|---:|---:|
| 1Z-013A SHORT HIGH | 861 | 242.749 | 3884 |
| 1Z-013A SHORT LOW leader | 946 | 266.714 | 4267 |
| 1Z-013A LONG HIGH | 1667 | 469.991 | 7520 |
| 1Z-013A LONG LOW mark | 1740 | 490.573 | 7849 |
| 1Z-009A SHORT HIGH | 861 | 240.533 | 3849 |
| 1Z-009A SHORT LOW leader | 946 | 264.279 | 4228 |
| 1Z-009A LONG HIGH | 1661 | 464.025 | 7424 |
| 1Z-009A LONG LOW mark | 1770 | 494.476 | 7912 |


### 5.7 MZ-700 NORMAL receive decision timing

The same ROMs also allow a source-based check of the normal receiver.

For monitor-ROM execution, after a successful rising-edge sample inside
`EDGE`, the next data sample in `RBYTE` is modeled as:

```text
detected-edge -> data sample = 103 + 18*N T
```

where `N` is the byte at `$0A4B` used by `DLY3`.

The rising-edge polling loop in ROM is approximately 62 T, therefore a
physical edge can be detected anywhere within approximately one 62-T
polling interval:

```text
earliest sample = 103 + 18*N
latest sample   = earliest + 62
```

For the supplied ROMs:

| ROM | DLY3 N | earliest | latest | window [us] |
|---|---:|---:|---:|---:|
| 1Z-013A PAL | $3F = 63 | 1237 T | 1299 T | 348.758–366.238 |
| 1Z-009A JP | $45 = 69 | 1345 T | 1407 T | 375.746–393.067 |

The 1Z-013A late edge of this static window is very close to Sharp's
nominal MZ-700 read point of 368 us. This is an additional consistency
check for the ROM wait-state model.

These values also explain why pulse profiles that are not perfectly
waveform-faithful can still load reliably: the receiver classifies by a
decision window, not by requiring an exact pulse duration.

---

## 6. MZ-800 native monitor 1Z-013B — exact Z80 pulse audit

The newly supplied 4096-byte image `Mz700a(1).rom` identifies itself at
ROM `$06E7` as:

```text
**  MONITOR 1Z-013B  **
```

SHA-256:

`fa65299ff588e3402973afbb1285dc5a7993efe9265213b64ba8e3a6facd2acf`

The binary matches the 1Z-013B source organization in the MZ-800 Technical
Reference Manual and closes the major open item from revision 1 of this
document.

### 6.1 Why MZ-800 timing constants differ from MZ-700

The relevant 1Z-013B timing constants are:

```text
DLY1 $0759 : LD A,$1B = 27
DLY2 $0760 : LD A,$19 = 25
DLY4 $09A9 : LD A,$73 = 115
DLY3 $0A4A : LD A,$52 = 82
```

The important point is that these larger counters are not evidence of a
slower CMT. They compensate for the different monitor execution timing.

The MZ-700 1Z-013A source uses a delay-loop model of approximately 18 T per
loop count. The MZ-800 1Z-013B constants are chosen for the standard
14-T `DEC A / JP NZ` loop:

```text
MZ700 DLY1: 21 * 18 = 378
MZ800 DLY1: 27 * 14 = 378

MZ700 DLY4: 89 * 18 = 1602
MZ800 DLY4:115 * 14 = 1610

MZ700 DLY3: 63 * 18 = 1134
MZ800 DLY3: 82 * 14 = 1148
```

This is a strong code-level explanation for why simply copying delay
operands between MZ-700 and MZ-800 is wrong.

For 1Z-013B this audit uses standard Z80 instruction timing. The assembler
listing's own comments (`DLY1 ~111.39 us`, `DLY2 ~103.45 us`,
`DLY4 ~458.81 us`, `DLY3 ~331.35 us`) are consistent with this model at
3.546875 MHz.

### 6.2 Exact delay equations

For a normal RAM/no-added-wait Z80 loop:

```text
DLY(N) routine body
 = LD A,N + N*(DEC A + JP NZ) + RET
 = 7 + N*(4+10) + 10
 = 17 + 14*N T

CALL DLY(N), inclusive
 = 17 + (17 + 14*N)
 = 34 + 14*N T
```

Thus for 1Z-013B:

```text
CALL DLY1(27) = 412 T
CALL DLY4(115)= 1644 T
```

The `DLY3` routine is special because it executes `LD A,N; JP $0762`,
entering DLY2 at the loop body:

```text
CALL DLY3(N) = 44 + 14*N T
CALL DLY3(82)= 1192 T
```

### 6.3 Exact MZ-800 generated HIGH widths

`SHORT` at `$0A01` contains two DLY1 calls between its `$E003` writes.

```text
SHORT HIGH
 = 2*CALL_DLY1 + LD A,$02 + LD ($E003),A
 = 2*412 + 7 + 13
 = 844 T
 = 237.956 us
```

`LONG` at `$0A1A` contains one DLY4 call between its writes.

```text
LONG HIGH
 = CALL_DLY4 + LD A,$02 + LD ($E003),A
 = 1644 + 7 + 13
 = 1664 T
 = 469.145 us
```

These are the most context-independent pulse quantities because they are
fully inside the pulse routine.

### 6.4 Exact context-dependent LOW widths

The LOW phase includes the remainder of `SHORT`/`LONG` plus caller code
before the next rising edge. Therefore there is no single exact LOW value.

| Context | T-states | time [us] | nearest Timer3 ticks @16 MHz |
|---|---:|---:|---:|
| SHORT HIGH | 844 | 237.956 | 3807 |
| SHORT LOW, repeated leader (`GAP1`) | 918 | 258.819 | 4141 |
| SHORT LOW, repeated tape mark (`GAP3`) | 908 | 256.000 | 4096 |
| SHORT LOW, data S→L | 896 | 252.617 | 4042 |
| SHORT LOW, data S→S | 906 | 255.436 | 4087 |
| LONG HIGH | 1664 | 469.145 | 7506 |
| LONG LOW, repeated tape mark (`GAP2`) | 1728 | 487.189 | 7795 |
| LONG LOW, data L→L | 1716 | 483.806 | 7741 |
| LONG LOW, data L→S | 1726 | 486.626 | 7786 |

Examples of the low-phase equations:

```text
SHORT tail after falling edge
 = 2*CALL_DLY1 + POP AF + RET
 = 824 + 10 + 10
 = 844 T

SHORT LOW, leader
 = SHORT_tail
 + DEC BC + LD A,B + OR C + JR NZ
 + CALL SHORT + PUSH AF + LD A,$03 + LD ($E003),A
 = 844 + 6 + 4 + 4 + 12 + 17 + 11 + 7 + 13
 = 918 T

SHORT LOW, data S->L
 = 844 + RLCA + CALL C,LONG + pulse-prefix
 = 844 + 4 + 17 + 31
 = 896 T

SHORT LOW, data S->S
 = 844 + RLCA + CALL C(not taken) + CALL NC,SHORT + pulse-prefix
 = 844 + 4 + 10 + 17 + 31
 = 906 T
```

The corresponding LONG equations differ only by replacing the `SHORT` tail
with the 1664-T LONG tail.

### 6.5 Comparison with Sharp service values

Sharp's MZ-800 service documentation gives nominal values:

```text
SHORT HIGH 240 us
SHORT LOW  278 us
LONG  HIGH 470 us
LONG  LOW  494 us
READ POINT diagram: 379 us
READ POINT prose:   368 us
```

Direct Z80 execution gives:

```text
SHORT HIGH 237.956 us
SHORT LOW  approximately 252.617..258.819 us in steady/common contexts
LONG  HIGH 469.145 us
LONG  LOW  approximately 483.806..487.189 us in steady/common contexts
```

The HIGH portions agree extremely well with the manual. The software-derived
LOW portions are shorter than the manual's nominal LOW values. The manual
values must therefore be treated as hardware/nominal waveform guidance,
while the table above is the exact digital `$E003` software-edge schedule.

Do not collapse these two evidence types into one fake “exact” value.

### 6.6 Native MZ-800 receive decision window

The successful rising-edge read in `EDGE` is at `$061A`. In `RBYTE`, the
later data read is reached after:

```text
AND $20             7 T
JR Z,EDGE-loop       7 T  (not taken after successful rising edge)
RET                 10 T
JP C,RBY3           10 T  (not taken)
CALL DLY3(N)       44 + 14*N T
LD A,(DE)            7 T  (data sample)
-----------------------------------------------
detected-edge -> sample = 85 + 14*N T
```

For native MZ-800 `N=$52=82`:

```text
earliest detected-edge sample
 = 1233 T
 = 347.630 us
```

The successful rising-edge polling loop repeats every approximately 52 T,
so the physical edge can precede the detected edge by up to one poll:

```text
static physical-edge sample window
 = 1233..1285 T
 = 347.630..362.291 us
```

This is close to the service-manual 368/379-us read-point data once input
circuit propagation and documentation rounding are allowed for.

### 6.7 Current SD2CMT2 MZ-800 NORMAL versus Z80

Current project:

```text
SHORT 250/250 us
LONG  500/500 us
```

Compared with the exact Z80 schedule:

| Quantity | Z80 reference | current | delta |
|---|---:|---:|---:|
| SHORT HIGH | 237.956 us | 250 us | 12.044 us (+5.06 %) |
| SHORT LOW leader | 258.819 us | 250 us | -8.819 us (-3.41 %) |
| SHORT LOW data S→S | 255.436 us | 250 us | -5.436 us (-2.13 %) |
| LONG HIGH | 469.145 us | 500 us | 30.855 us (+6.58 %) |
| LONG LOW tape mark | 487.189 us | 500 us | 12.811 us (+2.63 %) |
| LONG LOW data L→S | 486.626 us | 500 us | 13.374 us (+2.75 %) |

**Calibration interpretation:** current 250/250/500/500 is a usable symmetric
compatibility approximation, but it is not ROM-exact.

If future work wants waveform fidelity rather than a symmetric model, the
best solution is not another single four-number guess. It is to allow
context-dependent LOW timing, or at minimum document which context was
chosen for a fixed profile.

A reasonable *reference-only* rounded fixed approximation of common 1Z-013B
software edges would be near:

```text
SHORT HIGH ~238 us
SHORT LOW  ~253..259 us
LONG  HIGH ~469 us
LONG  LOW  ~484..487 us
```

Do not change firmware to these values without real-MZ regression.

---

## 7. MZ-800 turbo loader — Intercopy (IC)

**Target machine:** MZ-800.

**Project modes:**

```text
IC_1_4
IC_1_3
IC_1_2
```

**Embedded loader:** 96 bytes, SHA-256
`a52224f33e5ad8ca6d436257f58ef5802c4a4a8a7c8e0798e31a0a968a6eeab2`.

### 7.1 What IC changes in the monitor

The IC loader operates on the RAM copy of the low monitor and performs two
timing-relevant modifications.

First, it replaces the `RET` at monitor `EDGE+$1E`, address `$061F`, with:

```asm
JP $115C
```

The loader byte sequence is visible directly in the embedded code:

```text
3E C3       LD A,$C3
32 1F 06    LD ($061F),A
21 5C 11    LD HL,$115C
22 20 06    LD ($0620),HL
```

Second, it obtains the selected speed byte from the fake header metadata and
writes it to the DLY3 operand at `$0A4B`.

In the current SD2CMT2 fake header:

```text
metadata byte $18 = original file type
metadata byte $19 = IC speed byte
```

The IC loader reads the pair via `$1108`, then uses the high byte for
`$0A4B`.

Project speed operands:

```text
IC_1_4 -> $11 = 17
IC_1_3 -> $16 = 22
IC_1_2 -> $20 = 32
```

### 7.2 IC EDGE return hook cost

Hook at `$115C`:

```asm
PUSH BC
LD   A,($1110)
XOR  $0C
LD   ($1110),A
LD   BC,$06CF
OUT  (C),A
POP  BC
RET
```

Standard RAM-executed Z80 cost:

```text
11 + 13 + 7 + 13 + 10 + 12 + 10 + 10 = 86 T
```

The original `$061F` instruction was `RET` (10 T). It is replaced by a
10-T `JP $115C` plus the 86-T hook, therefore the **net receiver delay added
by IC is +86 T**.

### 7.3 IC exact receiver equation

For RAM execution:

```text
base detected-edge -> sample = 85 + 14*N T
IC detected-edge -> sample   = 85 + 14*N + 86 T
poll uncertainty             = 0..52 T
```

So:

```text
IC earliest = 171 + 14*N T
IC latest   = 223 + 14*N T
```

### 7.4 IC receiver windows and current PWM

| Mode | DLY3 | sample T window | sample [us] | current SHORT H/L [us] | current LONG H/L [us] | SHORT-high margin [us] | LONG-high margin [us] |
|---|---:|---:|---:|---:|---:|---:|---:|
| `IC_1_4` | `0x11` (17) | 409–461 | 115.313–129.974 | 112/80 | 176/160 | 3.313 | 46.026 |
| `IC_1_3` | `0x16` (22) | 479–531 | 135.048–149.709 | 112/96 | 224/192 | 23.048 | 74.291 |
| `IC_1_2` | `0x20` (32) | 619–671 | 174.520–189.181 | 144/112 | 256/224 | 30.520 | 66.819 |

Margin definitions:

```text
SHORT margin = earliest sample - SHORT HIGH
LONG margin  = LONG HIGH - latest sample
```

The IC_1_4 SHORT margin is the tightest of all current turbo modes. That mode
should be the first regression target if IC timings are ever changed.

### 7.5 IC phase/polarity in current SD2CMT2

`mzf_stage_uses_low_first_timing()` returns true for IC turbo stages.
Therefore current IC pulses are physically started LOW-first. This is a
phase decision separate from pulse width and must be preserved during any
calibration change unless a hardware trace proves otherwise.

### 7.6 IC calibration meaning

The loader does **not** generate the transmitted waveform. It defines a
receiver decision window.

Therefore the code-derived requirement is:

```text
SHORT must be LOW before the receiver sample window
LONG must still be HIGH through the receiver sample window
```

The current asymmetric IC H/L values are compatibility waveforms chosen
around that decision window. Their LOW widths determine cadence and the next
edge, but cannot be derived from the single receiver sample point alone.

---

## 8. MZ-800 turbo loader — TurboCopy (TC)

**Target machine:** MZ-800.

**Project modes:**

```text
TC_1_3
TC_1_2
```

**Embedded loader:** 90 bytes loaded at `$D400`, SHA-256
`4ab47c752ae0d04ce341d9be221336ed51b30a9c698d09aed4370172917ee790`.

### 8.1 TC monitor modification

The TC loader carries its speed byte at loader offset `$4B`:

```text
runtime address $D44B
```

It executes:

```asm
LD A,($D44B)
LD ($0A4B),A
```

so the byte directly becomes the DLY3 operand.

Current SD2CMT2 values:

```text
TC_1_3 -> $1B = 27
TC_1_2 -> $29 = 41
```

TC does **not** install the IC `$061F -> $115C` EDGE hook.

The TC loader also arranges for the low monitor to execute from RAM before
the accelerated `RDDAT` path, therefore use normal no-wait Z80 instruction
timing for the patched receive routine.

### 8.2 TC exact receiver equation

With no IC hook:

```text
earliest sample = 85 + 14*N T
latest sample   = earliest + 52 T
```

### 8.3 TC receiver windows and current PWM

| Mode | DLY3 | sample T window | sample [us] | current SHORT H/L [us] | current LONG H/L [us] | SHORT-high margin [us] | LONG-high margin [us] |
|---|---:|---:|---:|---:|---:|---:|---:|
| `TC_1_3` | `0x1B` (27) | 463–515 | 130.537–145.198 | 112/112 | 204/204 | 18.537 | 58.802 |
| `TC_1_2` | `0x29` (41) | 659–711 | 185.797–200.458 | 144/144 | 288/288 | 41.797 | 87.542 |

Both current TC modes have wide classification margin.

### 8.4 TC polarity and framing details

Current SD2CMT2 sets:

```text
mzf_wave_invert_signal = true
```

for TC turbo. Thus TC's physical waveform polarity is automatically inverted
relative to the direct MZF/IC path. This inversion does not change timing
intervals, but it is part of the proven loader compatibility and must be
treated as a first-class calibration parameter.

Current TC-specific framing parameters include:

```text
TC_1_3 turbo leader: 15130 SHORT pulses
TC_1_2 turbo leader: 11239 SHORT pulses
TC trailing:         98 SHORT pulses
TC start delay:      110 ms
```

These values are **framing/transport parameters**, not DLY3 readpoint
parameters. Do not derive or modify them from the equation above.

### 8.5 TC calibration meaning

Exactly as with IC, the source-derived truth is the receiver sample window.
The current symmetric `112/112, 204/204` and `144/144, 288/288` waveforms
are implementation choices with substantial readpoint margin, not literal
pulse widths generated by TurboCopy itself.

---

## 9. MZ-700 turbo loader — FAST3

**Target machine:** MZ-700 monitor family.

This section is intentionally separate from IC/TC because FAST3 is built
around the supplied MZ-700 1Z-009A monitor logic, not the MZ-800 loader
family.

### 9.1 Exact project transformation

The project embeds the exact 1Z-009A QADCN table and constructs a header-only
runtime. Before accelerated data loading the runtime:

1. maps/copies the monitor so the CMT code executes from RAM,
2. writes only the DLY3 operand at `$0A4B`,
3. restores original SIZE/LOAD fields,
4. calls the public `RDDAT` vector at `$002A`.

The timing patch is:

```asm
LD A,$15
LD ($0A4B),A
```

so:

```text
FAST3 DLY3 N = $15 = 21
```

The project verification explicitly rejects the old `$0512` speed patch and
locks `$0A4B` as the only timing patch.

### 9.2 FAST3 exact receiver equation

FAST3 installs no IC EDGE hook. Once the monitor code is in RAM:

```text
earliest sample = 85 + 14*21
                = 379 T

latest sample   = 431 T
```

For the supplied Japanese 1Z-009A clock 3.579545 MHz:

```text
sample window = 105.879..120.406 us
```

For use on a 3.546875-MHz PAL-compatible machine, the same RAM instruction
path would be:

```text
sample window = 106.855..121.515 us
```

The distinction is small relative to the current pulse margin, but the clock
must still be recorded in any future audit.

### 9.3 FAST3 current PWM and margins

Current SD2CMT2:

```text
SHORT = 80 us HIGH + 80 us LOW
LONG  = 160 us HIGH + 160 us LOW
```

For the Japanese MZ-700 clock:

```text
SHORT-high margin
 = 105.879 - 80
 = 25.879 us

LONG-high margin
 = 160 - 120.406
 = 39.594 us
```

Thus both bit classes remain well separated from the complete static
readpoint window.

### 9.4 FAST3 phase and boundary behavior

Current project deliberately keeps FAST3 **HIGH-first**, unlike IC.

It also holds READ LOW for:

```text
400 ms
```

between the synthetic header and FAST3 payload stage.

That 400-ms interval is an inter-stage transport/startup behavior. It is not
part of SHORT/LONG pulse timing and must remain outside the DLY3 timing
equation.

### 9.5 FAST3 calibration meaning

FAST3 is a receiver-timing modification, not a transmitter pulse generator.

The old comment saying the input sample is “about 97 us after the edge”
counts only part of the delay mechanism. The complete static
detected-edge-to-sample path is the window above.

Therefore `80/160 us` should be justified by **decision margin**, not by
claiming that the patched ROM “generates 80/160-us pulses”.

---

## 10. Current NORMAL 2x and 3x modes

Current project values:

```text
MZ800 NORMAL 2x:
  SHORT = 2178 Timer3 ticks = 136.125 us per half
  LONG  = 4425 Timer3 ticks = 276.5625 us per half

MZ800 NORMAL 3x:
  SHORT = 1811 Timer3 ticks = 113.1875 us per half
  LONG  = 3265 Timer3 ticks = 204.0625 us per half
```

The current source explicitly says these came from 44.1 kHz PCM references.

Crucially, selecting `LOADER_MODE_NORMAL_1_2` or
`LOADER_MODE_NORMAL_1_3` does **not** install a receiver patch comparable to
IC/TC/FAST3.

Therefore these profiles are classified:

```text
EMPIRICAL-WAV / EXTERNAL-RECEIVER-ASSUMPTION
```

An AI agent must not label them ROM-derived or loader-derived. Before future
recalibration, establish which receiving monitor/loader is expected to
decode them.

---

## 11. UL LOW/HIGH modes are not PWM modes

The project's own UL receivers (`LOW` and `HIGH`, including MZ-700 UL)
use a software handshake:

```text
Z80 waits for WRITE phase
Z80 samples READ
Z80 changes $E003
AVR sees phase change
AVR drives next READ bit
```

The receiver loop contains operations equivalent to:

```asm
LD A,(DE)
BIT 4,A
JR Z/NZ,wait
```

The steady polling loop is about 27 standard Z80 T-states when running from
RAM.

`LOW` and `HIGH` in these loader names describe loader placement/address
choice, **not pulse polarity and not SHORT/LONG timing**.

Classification:

```text
type = HANDSHAKE
short_high = N/A
short_low  = N/A
long_high  = N/A
long_low   = N/A
```

Calibration of UL must use handshake turnaround/timeout analysis, not the
PWM table.

---

## 12. Framing values: keep separate from pulse calibration

Current project also contains leader/tape-mark counts. They are not pulse
lengths and should not be silently changed as part of a half-wave audit.

Current profile highlights:

```text
MZ700 NORMAL: header 22000 SHORT, data 11000 SHORT
MZ800 NORMAL: header 6344 SHORT, data 6344 SHORT
IC payload:   5500 SHORT
TC_1_3:       15130 SHORT
TC_1_2:       11239 SHORT
```

Sharp service documentation uses 22000/11000 for standard cassette framing.
The current MZ800 6344/6344 behavior is therefore a separate framing/design
question. Preserve it until a dedicated framing audit and hardware A/B test
are performed.

Likewise, MZ-700 documentation describes duplicated blocks separated by
SHORT pulses; current profile behavior should be audited separately from
pulse-width calibration.

---

## 13. Recommended calibration policy for SD2CMT2

### NORMAL ROM-compatible modes

Prefer:

```text
supplied ROM bytes
-> exact CMT write path
-> machine clock + verified wait-state model
-> contextual pulse timing
-> fixed Timer3 approximation
-> real-hardware regression
```

For a fixed four-value profile, explicitly state which contexts were chosen
as representative.

### Turbo receiver modes

Prefer:

```text
exact loader bytes
-> exact DLY3/EDGE patches
-> sample window
-> choose SHORT/LONG HIGH widths with margin
-> choose LOW widths for safe cadence/framing
-> real-hardware regression
```

Do **not** ask “what pulse does the loader generate?” when the loader is only
a receiver.

### UL modes

Do not put UL into a PWM pulse table. Audit handshake timing separately.

---

## 14. Machine-readable summary for future AI agents

The block below is deliberately redundant with the prose. It is intended as
a stable extraction target.

```yaml
schema: sd2cmt2-cmt-timing-reference-v2
project:
  repo: bales0/MZ-SD2CMT2
  commit: fabf8fc0d032a0363b991be2c1e388fc5657d2c5
  latest_checked: 2026-08-29
  avr_hz: 16000000

scope:
  tap: excluded

roms:
  mz700_1z013a_pal:
    file: 1z-013a(2).rom
    sha256: ae1d65717414d4693158cbbd9ab707aa43e4e1ac0b2ae0e13ee3f68c66cba81b
    cpu_hz: 3546875
    execution_model: mz700_monitor_wait_timing
    dly3_operand: 0x3f
    confidence: ROM-PATH-HIGH

  mz700_1z009a_jp:
    file: mz700_1z-009a_jp(1).rom
    sha256: b0d16889ac3e2a80cc3bc9445bc95bc9988df7b6115124f284850667cf45ff9f
    cpu_hz: 3579545
    execution_model: mz700_monitor_wait_timing
    dly3_operand: 0x45
    canonical_fixed_us:
      short_high: 240
      short_low: 264
      long_high: 464
      long_low: 494
    confidence: ROM-PATH-HIGH

  mz800_1z013b:
    file: Mz700a(1).rom
    sha256: fa65299ff588e3402973afbb1285dc5a7993efe9265213b64ba8e3a6facd2acf
    identity_string: "**  MONITOR 1Z-013B  **"
    cpu_hz: 3546875
    execution_model: standard_z80_timing_for_1z013b_delay_code
    delay_operands:
      dly1: 0x1b
      dly2: 0x19
      dly4: 0x73
      dly3: 0x52
    generated_waveform_t:
      short_high: 844
      short_low:
        leader: 918
        tape_mark: 908
        data_S_to_L: 896
        data_S_to_S: 906
      long_high: 1664
      long_low:
        tape_mark: 1728
        data_L_to_L: 1716
        data_L_to_S: 1726
    receiver_sample_t:
      earliest_from_detected_edge: 1233
      latest_from_physical_edge_static_model: 1285
    confidence: ROM-PATH-HIGH

  mz800_9z504m:
    file: 9Z_504M(4).ROM
    sha256: 68a91b82517d5642e250cdddb519de780fb20bcde39b2de8baee387ca6bf446a
    role: high_monitor
    delegates_rddat_vector_002a: true
    paired_low_monitor: mz800_1z013b

turbo_loaders:
  mz800_ic:
    target: MZ800
    loader_bytes: 96
    loader_sha256: a52224f33e5ad8ca6d436257f58ef5802c4a4a8a7c8e0798e31a0a968a6eeab2
    execution: low_monitor_copy_in_ram
    edge_patch:
      address: 0x061f
      replacement: JP_0x115c
      net_extra_t: 86
    modes:
      IC_1_4:
        dly3_n: 17
        sample_t: [409, 461]
        current_us:
          short: [112, 80]
          long: [176, 160]
      IC_1_3:
        dly3_n: 22
        sample_t: [479, 531]
        current_us:
          short: [112, 96]
          long: [224, 192]
      IC_1_2:
        dly3_n: 32
        sample_t: [619, 671]
        current_us:
          short: [144, 112]
          long: [256, 224]
    physical_phase: LOW-first
    confidence: LOADER-PATH-HIGH

  mz800_tc:
    target: MZ800
    loader_bytes: 90
    load_address: 0xd400
    loader_sha256: 4ab47c752ae0d04ce341d9be221336ed51b30a9c698d09aed4370172917ee790
    execution: low_monitor_copy_in_ram
    edge_hook_t: 0
    speed_source_address: 0xd44b
    speed_patch_address: 0x0a4b
    modes:
      TC_1_3:
        dly3_n: 27
        sample_t: [463, 515]
        current_us:
          short: [112, 112]
          long: [204, 204]
      TC_1_2:
        dly3_n: 41
        sample_t: [659, 711]
        current_us:
          short: [144, 144]
          long: [288, 288]
    physical_polarity: inverted_by_sd2cmt2
    confidence: LOADER-PATH-HIGH

  mz700_fast3:
    target: MZ700
    source_rom: mz700_1z009a_jp
    execution: monitor_copy_in_ram
    dly3_n: 21
    speed_patch_address: 0x0a4b
    edge_hook_t: 0
    sample_t: [379, 431]
    current_us:
      short: [80, 80]
      long: [160, 160]
    physical_phase: HIGH-first
    pre_payload_low_delay_ms: 400
    confidence: LOADER-PATH-HIGH

non_pwm:
  ul_low: HANDSHAKE
  ul_high: HANDSHAKE
  mz700_ul_low: HANDSHAKE
  mz700_ul_high: HANDSHAKE

empirical_profiles:
  mz800_normal_2x:
    short_half_ticks: 2178
    long_half_ticks: 4425
    source: 44.1kHz_PCM
  mz800_normal_3x:
    short_half_ticks: 1811
    long_half_ticks: 3265
    source: 44.1kHz_PCM
```

---

## 15. AI maintenance contract

Any future AI/code agent changing CMT timing in this project **must**:

1. identify the exact mode/profile being changed;
2. state whether it is `generated waveform`, `receiver readpoint`, or
   `handshake`;
3. identify the exact ROM/loader hash or frozen source commit;
4. identify the CPU clock;
5. state whether code executes from ROM or RAM;
6. state the wait-state model;
7. show the exact Z80 T-state equation;
8. show conversion to microseconds;
9. show conversion to Timer3 ticks;
10. preserve signal phase/polarity (`HIGH-first`/`LOW-first`, inversion);
11. compare against the current profile;
12. report SHORT and LONG decision margins for receiver-derived modes;
13. run or request a real-hardware regression before removing a
    hardware-proven profile;
14. never use a quantized WAV as a higher authority than a verified
    ROM/loader execution path without an explicit documented reason.

If a value cannot be proven, label it `EMPIRICAL`, `MANUAL`, or
`TRACE-PENDING`; do not invent precision.

---

## 16. Open items

Native MZ-800 timing is now statically closed to the same level as the
available ROM-path analysis: the supplied 1Z-013B binary is identified,
hashed and cycle-counted.

The remaining work is **validation and automation**, not missing source data:

- acquire a cycle-accurate runtime trace of `$E003` writes and `$E002` reads
  on a model that reproduces the machine's memory timing;
- capture real MZ-700 and MZ-800 digital CMT edges with a logic analyzer
  before the analog cassette circuitry when possible;
- compare the trace against this document's T-state equations;
- A/B test any future replacement of the current symmetric MZ-800
  `250/250/500/500` profile;
- separately audit framing/leader counts, because this document intentionally
  treats leader count and pulse width as different calibration dimensions.

### Recommended automated trace format

A future debug build/emulator should emit:

```text
cycle=<absolute Z80 T> pc=<PC> addr=E003 write=<0|1> map=<ROM|RAM>
cycle=<absolute Z80 T> pc=<PC> addr=E002 read=<0|1>  map=<ROM|RAM>
```

For receiver tests, feed a deterministic pattern such as:

```text
00 FF 55 AA
```

The analysis should automatically report:

```text
S->S
S->L
L->S
L->L
rising-edge polling latency
detected-edge -> sample
physical-edge -> sample window
```

The trace must include memory mapping so ROM execution and RAM-executed
turbo monitor code cannot be confused.

---

## 17. External reference documents

These are supporting references; the supplied binaries and frozen project
source remain the primary reproducible inputs.

- Sharp MZ-700 Owner's Manual / 1Z-013A source listing:
  `https://eaw.app/Downloads/Manuals/Sharp/MZ-700_Owners_Manual.pdf`
- Sharp MZ-700 Service Manual:
  `https://old.sharpmz.org/mz-700/download/sm700.pdf`
- Sharp MZ-800 Technical Reference Manual / 1Z-013B source listing (used to cross-check the supplied low-monitor binary):
  `https://www.radeksuk.cz/sharp/gdg/dokumentace/MZ800_Technical_reference_manual.pdf`
- Sharp MZ-800 Service Manual:
  `https://www.idealine.info/sharpmz/mz-800/download/sm800.pdf`
- MZ-800 ROM layout reference:
  `https://www.idealine.info/sharpmz/mz-800/dldrom.htm`

---

## 18. Current audit verdict

The audit now has a source-derived Z80 basis for both native monitor
families and for every implemented MZ turbo receiver mode.

### Native MZ-700

The earlier WAV-first conclusion remains corrected:

**retain MZ-700 NORMAL 240/264/464/494 us as the current fixed compatibility
profile unless a dedicated PAL/JP split is introduced.**

The supplied ROMs show that exact software-edge timing is contextual, but
the current profile is strongly supported by the Japanese 1Z-009A ROM and
Sharp's nominal hardware values.

### Native MZ-800

The supplied `1Z-013B` binary closes the missing-ROM gap.

The exact Z80 software-edge schedule is approximately:

```text
SHORT HIGH 237.956 us
SHORT LOW  252.617..258.819 us in common steady contexts
LONG  HIGH 469.145 us
LONG  LOW  483.806..487.189 us in common steady contexts
```

Therefore current `250/250/500/500 us` is a **symmetric compatibility
approximation**, not an exact reproduction of 1Z-013B.

No firmware change is made by this audit. Any later change should compare
real-hardware reliability against both the current profile and the
ROM-derived contextual reference.

### MZ-800 Intercopy (IC)

IC is a receiver modification of the RAM monitor copy. It patches DLY3 and
adds a **net +86 T EDGE-return hook**. The three current waveforms have
source-derived decision windows; IC_1_4 has the tightest SHORT margin and
deserves the most conservative treatment.

### MZ-800 TurboCopy (TC)

TC patches DLY3 directly from `$D44B` to `$0A4B`, with no IC EDGE hook.
Both current TC modes have substantial static readpoint margin. TC's signal
inversion and 98-SHORT trailing sequence are separate compatibility
parameters and must not be lost in a timing refactor.

### MZ-700 FAST3

FAST3 is explicitly an MZ-700 receiver mode. It copies the monitor into RAM,
patches only `$0A4B` to `$15`, and uses the normal EDGE path. Current
`80/80, 160/160 us` is justified by the derived readpoint window, not by a
claim that the ROM itself generates those widths.

### Overall rule

For future SD2CMT2 changes:

```text
native NORMAL:
    ROM-generated edge schedule is the timing source

IC / TC / FAST3:
    loader receiver window is the timing source

UL:
    handshake turnaround is the timing source

WAV:
    regression evidence only, not the highest timing authority
```
