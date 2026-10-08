# Sharp MZ CMT — Intercopy 10.2 & TurboCopy 1.22 Timing Reference

**Purpose:** source reference for defining timing constants in **MZTools** and **MZ-SD2CMT2**.

**Analysis date:** 2026-09-14

This document is intended to be used together with:

- `CMT_TIMING_REFERENCE.md`
- `LEADER_PULSES_UNIFICATION.md`

It does not revisit the complete Sharp ROM timing or leader-count policy. It supplements them with a direct analysis of two historical tape-copying programs:

- **Intercopy V10.2**
- **Turbo Copy V1.22 / TC1.22**

Main principle:

> Always distinguish **writer-generated waveform**, **receiver decision timing**, **project compatibility waveform**, and **leader/framing policy**. These four concepts are not interchangeable.

---

# 1. Input binaries

## 1.1 Intercopy V10.2

File:

```text
Incopy102(3).mzf
```

SHA-256:

```text
b7b8669791a12c0212b046defdd37ca1f2687e595589c1cddf07bdb2ef443439
```

Identification in the MZF filename:

```text
Intercopy V10.2
```

Note: the MZ character set is not ASCII, but the `V10.2` suffix is directly readable in the header.

---

## 1.2 Turbo Copy V1.22

File:

```text
Tc122(4).mzf
```

SHA-256:

```text
aa1ee60b85ff8d2ef9f5cc0ebf43b199b7e5e0e715c85ee6b28e09aa9f68e384
```

MZF:

```text
TYPE = $01
LOAD = $1200
EXEC = $2D98
SIZE = 7083 B
```

The filename ends with:

```text
V1.22
```

The startup routine copies the main working image from `$1200` to the region starting at `$E471`.

Relocation offset used below:

```text
$E471 - $1200 = $D271
```

This allows the code stored in the MZF to be mapped directly to TurboCopy runtime addresses.

---

# 2. Evidence labels used in this document

```text
COPIER-WRITER-EXACT
    instructions from the actual writer of a specific tape-copying program
    + deterministic calculation of Z80/timer timings

COPIER-TIMER-EXACT
    exact programmed hardware counter value / timer mode;
    conversion to µs may still depend on the exact physical clock frequency

RECEIVER-EXACT
    exact loader/monitor receive path and decision window

HW-NOMINAL
    value from hardware/service documentation, e.g. CKMS = 1.10 MHz

PROJECT-COMPAT
    deliberately chosen fixed waveform with a good receive margin;
    may differ from the historical writer

PROJECT-FRAMING
    the project's canonical leader/tape-mark count policy;
    must not be derived from pulse width
```

When deciding whether to change constants, the following distinction applies:

```text
writer-exact != receiver-exact != compatibility waveform != framing
```

---

# 3. Intercopy V10.2 — distinguishing NORMAL and FAST IPL

## 3.1 Mode selector

Intercopy uses a mode byte at `$16F3`.

Program analysis gives:

```text
0 = NORMAL
1 = SINCLAIR
2 = CPM
3 = TURBO
4 = FAST IPL
```

This matters because several modes share the same FM writer routine.

---

## 3.2 NORMAL uses the shared FM writer directly

For NORMAL mode, the Intercopy code path is approximately:

```text
$2035
  -> $2054
  -> $19EE
  -> $1E39
  -> $1E09        select speed row
  -> table $16F7
  -> $1DF5        self-modify pulse delays
  -> $1D8E        physical CMT pulse writer
```

Therefore, the timing derived from `$16F7/$1D8E` **is not mistakenly limited to FAST IPL timing**.

NORMAL uses this writer directly at the selected speed.

---

## 3.3 FAST IPL uses the same physical FM writer differently

FAST IPL has a separate setup sequence.

The routine around `$201C`:

1. saves the currently selected turbo speed,
2. temporarily selects `1200 Bd`,
3. writes the loader/header,
4. restores the original speed,
5. writes the turbo body using the shared FM writer at the selected speed.

Thus:

```text
FAST IPL header
    -> 1200 Bd physical FM timing

FAST IPL turbo body
    -> selected 2400 / 2800 / 3200 Bd physical FM timing
```

FAST IPL framing is a separate concern governed by `LEADER_PULSES_UNIFICATION`.

---

# 4. Intercopy speed table `$16F7`

Relevant rows:

```text
speed   readpoint   t1   t2   t3   t4
--------------------------------------
1200      $4D      $40  $80  $48  $87
2400      $20      $1F  $40  $26  $47
2800      $16      $18  $30  $22  $3D
3200      $11      $15  $2B  $20  $31
```

Meaning:

```text
readpoint
    receiver parameter / DLY3 family

t1
    SHORT first half-cycle

t2
    LONG first half-cycle

t3
    SHORT second half-cycle

t4
    LONG second half-cycle
```

`$1DF5` inserts four timing bytes directly into the self-modifying pulse routine `$1D8E`.

This is direct evidence that the Intercopy waveform can be asymmetric.

---

# 5. Intercopy writer `$1D8E` — calculating pulse widths

For the normal steady-state data path, the intervals can be expressed as:

```text
first half-cycle:
    N = ($F2 + timing_operand) & $FF
    T = 182 + 13*N

second half-cycle:
    N = ($F3 + timing_operand) & $FF
    T = 169 + 13*N
```

PAL MZ-800 CPU clock used:

```text
3,546,875 Hz
```

Conversion:

```text
time_us = T / 3,546,875 * 1,000,000
```

Note:

- the table below gives the reference steady-state data waveform;
- some leader/tape-mark contexts may have a slight caller-dependent variation;
- for example, certain repeated LONG contexts may differ by approximately 1 T;
- for a fixed four-value profile, the values below are the correct code-derived reference.

Evidence:

```text
COPIER-WRITER-EXACT
```

---

# 6. Intercopy V10.2 — code-derived physical waveform

## 6.1 1200 Bd

```text
SHORT HIGH =  832 T = 234.573 us
SHORT LOW  =  936 T = 263.894 us

LONG  HIGH = 1664 T = 469.145 us
LONG  LOW  = 1755 T = 494.802 us
```

This result also provides a useful sanity check against the standard MZ-800 CMT waveform.

---

## 6.2 2400 Bd

```text
SHORT HIGH = 403 T = 113.621 us
SHORT LOW  = 494 T = 139.278 us

LONG  HIGH = 832 T = 234.573 us
LONG  LOW  = 923 T = 260.229 us
```

Historical factor:

```text
2:1
```

---

## 6.3 2800 Bd

```text
SHORT HIGH = 312 T =  87.965 us
SHORT LOW  = 442 T = 124.617 us

LONG  HIGH = 624 T = 175.930 us
LONG  LOW  = 793 T = 223.577 us
```

Historical speed ratio:

```text
7:3 = 2.333333...
```

In current project terminology, this speed setting is often referred to as:

```text
NORMAL 1:3
IC 1:3
```

but **it is not mathematically 3.000×**.

---

## 6.4 3200 Bd

```text
SHORT HIGH = 273 T =  76.969 us
SHORT LOW  = 416 T = 117.286 us

LONG  HIGH = 559 T = 157.604 us
LONG  LOW  = 637 T = 179.595 us
```

Historical speed ratio:

```text
8:3 = 2.666666...
```

In current project terminology, this speed setting is often referred to as:

```text
NORMAL 1:4
IC 1:4
```

but **it is not mathematically 4.000×**.

---

# 7. Critical conclusion for NORMAL 3 / NORMAL 4

For historical reproduction of Intercopy NORMAL:

```text
NORMAL "3" / 2800 Bd:
    SHORT 87.965 / 124.617 us
    LONG  175.930 / 223.577 us

NORMAL "4" / 3200 Bd:
    SHORT 76.969 / 117.286 us
    LONG  157.604 / 179.595 us
```

Therefore, historical Intercopy NORMAL 3200 **is not**:

```text
112 / 80 us
176 / 160 us
```

nor is it:

```text
96 / 96 us
192 / 192 us
```

It is asymmetric by design / as a result of the algorithm.

For NORMAL 3200, the ratio of the first half-cycles, which determine the receiver's decision, is also important:

```text
LONG_HIGH / SHORT_HIGH
= 157.604 / 76.969
≈ 2.048
```

This gives substantially greater SHORT/LONG separation than the profile:

```text
176 / 112 ≈ 1.571
```

This matters for a Barbarian-style adaptive receiver.

---

# 8. Intercopy IC loader — receiver timing is not the writer waveform

`CMT_TIMING_REFERENCE` already defines the exact receiver windows for IC.

IC loader:

- runs a copy of the low monitor from RAM,
- patches DLY3,
- adds an EDGE-return hook with a net delay of `+86 T`.

Receiver equation:

```text
IC earliest = 171 + 14*N T
IC latest   = 223 + 14*N T
```

Speed bytes:

```text
IC 1:2 -> $20 = 32
IC 1:3 -> $16 = 22
IC 1:4 -> $11 = 17
```

Receiver windows:

```text
IC 1:2:
    619..671 T
    174.520..189.181 us

IC 1:3:
    479..531 T
    135.048..149.709 us

IC 1:4:
    409..461 T
    115.313..129.974 us
```

Current SD2CMT2 IC waveforms:

```text
IC 1:2:
    SHORT 144 / 112 us
    LONG  256 / 224 us

IC 1:3:
    SHORT 112 /  96 us
    LONG  224 / 192 us

IC 1:4:
    SHORT 112 /  80 us
    LONG  176 / 160 us
```

These values are:

```text
PROJECT-COMPAT / HW-tested compatibility waveform
```

They are not:

```text
COPIER-WRITER-EXACT Intercopy waveform
```

Therefore, neither MZTools nor SD2CMT2 may automatically use a single table for both:

```text
NORMAL 3200 exact
and
IC 1:4 compatibility
```

---

# 9. Recommended separation of Intercopy profiles in code

Both projects should conceptually maintain two layers.

## Historical writer reference

```text
INTERCOPY_NORMAL_1200
INTERCOPY_NORMAL_2400
INTERCOPY_NORMAL_2800
INTERCOPY_NORMAL_3200
```

with the code-derived H/L values from section 6.

The same physical timing engine also applies to the FAST IPL turbo body.

## Receiver-compatible SD2CMT2 profiles

```text
IC_1_2_COMPAT
IC_1_3_COMPAT
IC_1_4_COMPAT
```

with the existing hardware-proven values.

Changing one layer must not automatically change the other.

---

# 10. MZTools WAV — impact of sample rate

At 44.1 kHz, one sample lasts:

```text
22.675737 us
```

Intercopy NORMAL 3200 then corresponds approximately to:

```text
SHORT HIGH  76.969 us -> 3.394 samples
SHORT LOW  117.286 us -> 5.172 samples

LONG HIGH   157.604 us -> 6.950 samples
LONG LOW    179.595 us -> 7.920 samples
```

This is a coarse grid, especially for the shortest 77-us half-cycle.

Therefore, for waveform-fidelity export:

```text
88.2 kHz
```

is substantially more suitable than 44.1 kHz.

If MZTools remains at 44.1 kHz, it must use a continuous fractional/error accumulator; individual pulses will still be substantially quantized.

---

# 11. Turbo Copy V1.22 — new direct writer analysis

The earlier reference stated:

```text
TC loader/readpoint = exact
TC historical writer loop = not yet directly established
```

`Tc122(4).mzf` closes this gap for **Turbo Copy V1.22**.

Caution:

> This section is source-exact for TC1.22. It must not be assumed without further evidence that V1.0, V1.2, V1.21, and V1.22 have bit-identical writers.

---

# 12. TurboCopy V1.22 — runtime relocation

The startup code at `$2D98` relocates the main working region into the high monitor/work RAM area.

Relevant mapping:

```text
source $1974 -> runtime $EBE5
source $19A0 -> runtime $EC11
source $19A8 -> runtime $EC19
```

`$EBE5` is the physical CMT writer.

---

# 13. TurboCopy V1.22 — 8253 counter 0

TC1.22 programs the 8253 control register with:

```text
$36
```

Decoding the 8253 control word:

```text
counter = 0
RW      = LSB then MSB
mode    = MODE 3
format  = binary
```

MODE3 is a square-wave generator.

The Sharp MZ-800 service manual specifies the following for counter #0:

```text
CLK0 / CKMS = nominal 1.10 MHz
```

and OUT0 is also connected to:

```text
Z80 PIO PA4
```

TurboCopy therefore uses a hardware timer as the writer's time base.

Evidence:

```text
COPIER-TIMER-EXACT
+
HW-NOMINAL for conversion to µs
```

---

# 14. TurboCopy V1.22 — writer `$EBE5`

Simplified interpretation of the runtime routine:

```asm
LD HL,(E25E)        ; SHORT timer count
JR NC,short
ADD HL,HL           ; Carry => LONG = 2 * SHORT

wait OUT0 phase
LD A,$03
OUT ($D3),A         ; one CMT WRITE edge

...

wait opposite OUT0 phase
LD A,$02
OUT ($D3),A         ; the other CMT WRITE edge

CALL $E83D          ; reload 8253 counter 0 from HL
```

Important conclusions:

```text
SHORT counter = E25E
LONG  counter = 2 * E25E
```

This is direct writer code.

The LONG/SHORT ratio in the timer-count domain is therefore exactly:

```text
2:1
```

---

# 15. TurboCopy V1.22 — calculating the SHORT counter value

The runtime code at `$E87C/$E89E` uses:

```text
base = 480
ratio numerator   = byte E49C
ratio denominator = byte E49B
offset = 71
```

The result is:

```text
SHORT_COUNT =
    floor(480 * numerator / denominator) + 71
```

and:

```text
LONG_COUNT = 2 * SHORT_COUNT
```

This is:

```text
COPIER-TIMER-EXACT
```

In the supplied TC1.22 binary, the default state is:

```text
E49B = 2
E49C = 1
```

therefore:

```text
SHORT_COUNT
= floor(480 * 1 / 2) + 71
= 240 + 71
= 311

LONG_COUNT = 622
```

This corresponds to 2:1 mode.

For a 3:1 ratio:

```text
SHORT_COUNT
= floor(480 * 1 / 3) + 71
= 160 + 71
= 231

LONG_COUNT = 462
```

---

# 16. TurboCopy V1.22 — MODE3 timer half-periods

For 8253 MODE3:

- even count: HIGH and LOW each last `N/2` timer clocks,
- odd count: one phase lasts `(N+1)/2`, the other `(N-1)/2`.

## TC 2:1

```text
SHORT count = 311
    MODE3 halves = 156 / 155 CKMS ticks

LONG count = 622
    MODE3 halves = 311 / 311 CKMS ticks
```

At the nominal frequency specified in the service manual:

```text
CKMS = 1.10 MHz
```

the raw OUT0 reference is:

```text
SHORT:
    156 ticks = 141.818 us
    155 ticks = 140.909 us

LONG:
    311 ticks = 282.727 us
    311 ticks = 282.727 us
```

## TC 3:1

```text
SHORT count = 231
    MODE3 halves = 116 / 115 CKMS ticks

LONG count = 462
    MODE3 halves = 231 / 231 CKMS ticks
```

At 1.10 MHz:

```text
SHORT:
    116 ticks = 105.455 us
    115 ticks = 104.545 us

LONG:
    231 ticks = 210.000 us
    231 ticks = 210.000 us
```

These values describe **timer OUT0 timing**, not yet the exact PC1 cassette-WRITE edge schedule.

---

# 17. TurboCopy writer — why timer half-periods cannot simply be treated as exact WRITE µs

The TurboCopy writer does not drive CMT WRITE directly from the OUT0 hardware output.

Z80:

1. polls PIO PA4 / OUT0,
2. detects a timer phase change,
3. only then executes `OUT ($D3),A`.

The relevant polling loop takes approximately:

```text
IN + AND + JP = 25 Z80 T
```

At 3.546875 MHz:

```text
25 T ≈ 7.048 us
```

Both physical WRITE edges have similar subsequent instruction overhead, but the instant at which the timer edge is detected is quantized by polling.

Therefore:

```text
exact historical TC cassette WRITE waveform
```

is not ideally represented by a single perfectly constant set of four H/L values.

The code-exact historical representation is:

```text
MODE3 counter model
+
counter values
+
Z80 polling
```

A fixed four-value profile requires an approximation.

---

# 18. TurboCopy receiver — exact readpoints

`CMT_TIMING_REFERENCE` defines the TurboCopy receiver path.

The 90-byte loader is loaded at:

```text
$D400
```

The speed byte is at:

```text
$D44B
```

and the loader executes:

```asm
LD A,($D44B)
LD ($0A4B),A
```

without the Intercopy `+86 T` EDGE hook.

Receiver equation:

```text
earliest = 85 + 14*N T
latest   = earliest + 52 T
```

## TC 1:2

```text
N = $29 = 41

sample window:
659..711 T
185.797..200.458 us
```

## TC 1:3

```text
N = $1B = 27

sample window:
463..515 T
130.537..145.198 us
```

Evidence:

```text
RECEIVER-EXACT
```

---

# 19. TurboCopy current project waveform vs writer reference

SD2CMT2 currently uses:

```text
TC 1:2:
    SHORT 144 / 144 us
    LONG  288 / 288 us

TC 1:3:
    SHORT 112 / 112 us
    LONG  204 / 204 us
```

These values provide a good receiver margin.

However, they do not literally represent every TC1.22 writer edge.

Their status remains:

```text
PROJECT-COMPAT
```

The new TC1.22 analysis provides a historical timer reference alongside them:

```text
TC 2:1:
    timer-centered SHORT ≈ 141 us
    timer-centered LONG  ≈ 283 us

TC 3:1:
    timer-centered SHORT ≈ 105 us
    timer-centered LONG  ≈ 210 us
```

with approximately ±one polling quantum on individual software-detected edges.

Therefore, replacing the existing SD2CMT2 TC constants with timer-centered values without hardware regression testing is not recommended.

---

# 20. TurboCopy V1.22 loader fingerprint

TC1.22 contains a 90-byte loader family loaded at `$D400`.

In the supplied binary, the extracted 90-byte template has the following SHA-256 before runtime metadata patching:

```text
8f90221c447fe891a84364a437e66fae3d348bcc72d7cc2bc4d70433e7d7a0d1
```

This hash is not identical to that of the previously documented V1.21-derived SD2CMT2 template.

This implies:

> Do not confuse "the same loader family / timing mechanism" with "a bit-identical version of the tape-copying program".

However, the timing-critical receiver section of TC1.22 directly implements the principle:

```text
speed byte -> $0A4B
```

just like the analyzed TC family.

---

# 21. Leader/framing policy — use the separate document

Pulse timing must not change leader counts.

For MZTools/SD2CMT2, use `LEADER_PULSES_UNIFICATION` as the project policy.

Canonical values:

```text
NATIVE NORMAL:
    HEADER = 22000 SHORT
    DATA   = 11000 SHORT

SPECIAL LOADER:
    HEADER       = 11000 SHORT
    loader DATA  =  5500 SHORT
    turbo DATA   =  5500 SHORT
```

For IC:

```text
11000 / 5500
```

also has strong historical and hardware support.

For TC:

```text
11000 / 5500 / 5500
```

is the **SD2CMT2 canonical/optimized framing policy**, not a claim about the exact historical framing of every TurboCopy version.

This policy must not be confused with the pulse-width analysis in this document.

---

# 22. Recommended constants model for MZTools

MZTools should distinguish at least:

```text
HistoricalWriterProfile
CompatibilityReceiverProfile
FramingProfile
```

## Intercopy historical writer

```text
NORMAL_1200:
    short = 234.573 / 263.894 us
    long  = 469.145 / 494.802 us

NORMAL_2400:
    short = 113.621 / 139.278 us
    long  = 234.573 / 260.229 us

NORMAL_2800:
    short = 87.965 / 124.617 us
    long  = 175.930 / 223.577 us

NORMAL_3200:
    short = 76.969 / 117.286 us
    long  = 157.604 / 179.595 us
```

If MFI/MTI retains the historical project names:

```text
NORMAL 1:3 -> Intercopy 2800 / 7:3
NORMAL 1:4 -> Intercopy 3200 / 8:3
```

this mapping must be explicitly documented.

---

# 23. Recommended constants model for SD2CMT2

## NATIVE / historical reproduction path

If the mode is intended to reproduce Intercopy NORMAL:

```text
use the Intercopy writer-exact reference
```

## IC hardware-compat path

If the target is the current hardware-proven IC loader:

```text
retain the IC compatibility waveforms
```

until the writer-exact values have been independently tested on real MZ hardware.

## TC hardware-compat path

The current values:

```text
TC 1:2 = 144/144, 288/288
TC 1:3 = 112/112, 204/204
```

provide good receiver margins.

The TC1.22 source-exact writer is better modeled using:

```text
8253 MODE3 + counter values + polling
```

than by claiming that the historical writer used a single absolutely constant symmetric set of four values.

---

# 24. Recommended table of authoritative sources

| Profile / item | Authoritative value | Evidence |
|---|---|---|
| MZ-800 NORMAL 1× ROM | per `CMT_TIMING_REFERENCE` | ROM-PATH-HIGH |
| Intercopy NORMAL 1200 | 234.573/263.894, 469.145/494.802 us | COPIER-WRITER-EXACT |
| Intercopy NORMAL 2400 | 113.621/139.278, 234.573/260.229 us | COPIER-WRITER-EXACT |
| Intercopy NORMAL 2800 | 87.965/124.617, 175.930/223.577 us | COPIER-WRITER-EXACT |
| Intercopy NORMAL 3200 | 76.969/117.286, 157.604/179.595 us | COPIER-WRITER-EXACT |
| IC receiver 1:2 | 174.520..189.181 us read window | RECEIVER-EXACT |
| IC receiver 1:3 | 135.048..149.709 us read window | RECEIVER-EXACT |
| IC receiver 1:4 | 115.313..129.974 us read window | RECEIVER-EXACT |
| IC current waveform | 144/112–256/224; 112/96–224/192; 112/80–176/160 | PROJECT-COMPAT / HW-proven |
| TC1.22 2:1 writer | SHORT count 311, LONG 622 | COPIER-TIMER-EXACT |
| TC1.22 3:1 writer | SHORT count 231, LONG 462 | COPIER-TIMER-EXACT |
| TC 1:2 receiver | 185.797..200.458 us | RECEIVER-EXACT |
| TC 1:3 receiver | 130.537..145.198 us | RECEIVER-EXACT |
| TC current 1:2 waveform | 144/144, 288/288 us | PROJECT-COMPAT |
| TC current 1:3 waveform | 112/112, 204/204 us | PROJECT-COMPAT |
| native NORMAL leaders | 22000 / 11000 | PROJECT-FRAMING / Sharp standard |
| special loader leaders | 11000 / 5500 | PROJECT-FRAMING |
| TC canonical 11000/5500/5500 | SD2CMT2 policy | PROJECT-FRAMING |

---

# 25. Distinctions that must not be confused again

## 25.1 NORMAL 3200 is not the IC 1:4 waveform

Invalid inference:

```text
IC 1:4 speed byte = $11
=> NORMAL 1:4 must be 112/80,176/160
```

Correct interpretation:

```text
$11 is the receiver/readpoint speed byte
+
The Intercopy 3200 writer uses its own t1..t4 row
```

---

## 25.2 Intercopy 3/4 are not mathematically 3×/4×

```text
"3" -> 2800 Bd -> 7:3 ≈ 2.333×
"4" -> 3200 Bd -> 8:3 ≈ 2.667×
```

---

## 25.3 TurboCopy receiver delay is not the writer pulse width

```text
$29 / $1B
```

are receive DLY3 values.

The TC1.22 writer uses:

```text
8253 MODE3
E25E counter
```

---

## 25.4 A fixed TC µs profile is not a bit-exact TC writer

The historical writer follows this sequence:

```text
HW timer edge
-> Z80 polling
-> software CMT WRITE edge
```

A fixed waveform is therefore necessarily an approximation.

---

## 25.5 Leader count is not pulse duration

Use `LEADER_PULSES_UNIFICATION`.

A faster waveform must not automatically change the number of leader pulses.

---

# 26. Recommendations for further implementation

## MZTools

1. Correct NORMAL 1:4 so that it does not duplicate IC 1:4 compatibility timing.
2. If the names 1:3/1:4 represent Intercopy slots:
   - 1:3 -> 2800 Bd code-derived row,
   - 1:4 -> 3200 Bd code-derived row.
3. For WAV at 3200 Bd, prefer a sample rate of 88.2 kHz or higher.
4. Separate writer-fidelity profiles from loader-compatibility profiles.
5. Add automated edge-duration tests for all waveform profiles.

## SD2CMT2

1. Do not change hardware-proven IC/TC compatibility constants solely on the basis of historical fidelity.
2. If a faithful Intercopy NORMAL profile is introduced, use the code-derived row.
3. If a faithful TurboCopy profile is introduced, ideally model the MODE3 count and polling rather than just four absolute µs values.
4. Make any IC/TC timing changes only with regression testing on real MZ hardware.
5. Keep leader counts consistent with `LEADER_PULSES_UNIFICATION`.

---

# 27. Machine-readable reference

```yaml
schema: sharp-mz-copier-timing-reference-v1

clock:
  mz800_cpu_hz: 3546875
  tc_8253_ckms_nominal_hz: 1100000
  tc_8253_ckms_status: HW-NOMINAL

intercopy_v10_2:
  sha256: b7b8669791a12c0212b046defdd37ca1f2687e595589c1cddf07bdb2ef443439
  writer_evidence: COPIER-WRITER-EXACT

  normal_1200:
    readpoint: 0x4d
    short_us: [234.573, 263.894]
    long_us:  [469.145, 494.802]

  normal_2400:
    readpoint: 0x20
    short_us: [113.621, 139.278]
    long_us:  [234.573, 260.229]

  normal_2800:
    historical_ratio: "7:3"
    project_alias: "1:3"
    readpoint: 0x16
    short_t: [312, 442]
    long_t:  [624, 793]
    short_us: [87.965, 124.617]
    long_us:  [175.930, 223.577]

  normal_3200:
    historical_ratio: "8:3"
    project_alias: "1:4"
    readpoint: 0x11
    short_t: [273, 416]
    long_t:  [559, 637]
    short_us: [76.969, 117.286]
    long_us:  [157.604, 179.595]

ic_receiver:
  "1:2":
    dly3: 0x20
    sample_t: [619, 671]
    sample_us: [174.520, 189.181]
    current_compat_short_us: [144, 112]
    current_compat_long_us: [256, 224]

  "1:3":
    dly3: 0x16
    sample_t: [479, 531]
    sample_us: [135.048, 149.709]
    current_compat_short_us: [112, 96]
    current_compat_long_us: [224, 192]

  "1:4":
    dly3: 0x11
    sample_t: [409, 461]
    sample_us: [115.313, 129.974]
    current_compat_short_us: [112, 80]
    current_compat_long_us: [176, 160]

turbocopy_v1_22:
  sha256: aa1ee60b85ff8d2ef9f5cc0ebf43b199b7e5e0e715c85ee6b28e09aa9f68e384
  writer_evidence: COPIER-TIMER-EXACT
  timer:
    chip: "8253 counter 0"
    mode: 3
    control_word: 0x36
    short_count_formula: "floor(480*numerator/denominator)+71"
    long_count_formula: "2*short_count"

  "1:2":
    ratio_pair: [1, 2]
    short_count: 311
    long_count: 622
    mode3_short_ticks: [156, 155]
    mode3_long_ticks: [311, 311]
    nominal_out0_short_us_at_1_10mhz: [141.818, 140.909]
    nominal_out0_long_us_at_1_10mhz: [282.727, 282.727]
    receiver_dly3: 0x29
    receiver_window_us: [185.797, 200.458]
    current_compat_short_us: [144, 144]
    current_compat_long_us: [288, 288]

  "1:3":
    ratio_pair: [1, 3]
    short_count: 231
    long_count: 462
    mode3_short_ticks: [116, 115]
    mode3_long_ticks: [231, 231]
    nominal_out0_short_us_at_1_10mhz: [105.455, 104.545]
    nominal_out0_long_us_at_1_10mhz: [210.000, 210.000]
    receiver_dly3: 0x1b
    receiver_window_us: [130.537, 145.198]
    current_compat_short_us: [112, 112]
    current_compat_long_us: [204, 204]

framing_policy:
  native_normal:
    header_short_pulses: 22000
    data_short_pulses: 11000

  special_loader:
    header_short_pulses: 11000
    loader_data_short_pulses: 5500
    turbo_data_short_pulses: 5500
```

---

# 28. Final decision rule

Before defining a new constant in MZTools or SD2CMT2, first answer:

```text
Do I want:
A) a historically faithful writer waveform?
B) a waveform with the best receiver margin?
C) canonical project framing?
```

Then:

```text
A -> use this document / COPIER-WRITER-EXACT or COPIER-TIMER-EXACT

B -> use the CMT_TIMING_REFERENCE receiver windows
     + HW regression

C -> use LEADER_PULSES_UNIFICATION
```

Never derive one of these layers automatically from another.
