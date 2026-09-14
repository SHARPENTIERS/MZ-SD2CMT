# Zadání: sjednocení CMT timingů pro QDTool a MZ-SD2CMT2

## Účel

Tento dokument je implementační zadání pro úpravu časových konstant a generování CMT waveformu v projektech:

- `QDTool`
- `MZ-SD2CMT2`

Primární autorita je pouze:

1. přímý disassembly **MZ-800 monitor ROM 1Z-013B** pro native NORMAL 1×,
2. přímý disassembly **Intercopy V10.2** pro NORMAL 2×–4× a IC 2×–4×,
3. přímý disassembly **Turbo Copy V1.22** pro TC 2×–3×,
4. nová jednotná leader politika tohoto dokumentu: **HEADER = 11000 SHORT, DATA = 5500 SHORT pro všechny režimy**.

Nepoužívat staré WAV-derived, empirické nebo „compatibility“ timing tabulky jako zdroj časových konstant, pokud se liší od níže uvedeného disassembly.

---

# 1. Vstupní zdroje

## Intercopy V10.2

```text
soubor: Incopy102(3).mzf
size:   13440 B
SHA256: b7b8669791a12c0212b046defdd37ca1f2687e595589c1cddf07bdb2ef443439
```

## Turbo Copy V1.22

```text
soubor: Tc122(4).mzf
size:   7211 B
SHA256: aa1ee60b85ff8d2ef9f5cc0ebf43b199b7e5e0e715c85ee6b28e09aa9f68e384
```

## MZ-800 native monitor

Použít výsledky z `CMT_TIMING_REFERENCE` pro low monitor `1Z-013B` při:

```text
CPU = 3,546,875 Hz
```

---

# 2. Základní pravidlo

Nesmí se už míchat:

```text
writer timing
receiver readpoint
leader count
```

Pro generování WAV/PWM se používá **writer timing**.

Pro loader se používá příslušný **receiver speed byte / DLY3**.

Pro leader se používá jednotná politika tohoto dokumentu:

```text
HEADER = 11000 SHORT
DATA   =  5500 SHORT
```

Tato politika platí pro všechny režimy bez výjimky.

---

# 3. Finální kanonická tabulka timingů

## 3.1 NORMAL 1× — MZ-800 ROM 1Z-013B

Native NORMAL 1× se odvozuje z ROM, ne z Intercopy.

### Exact ROM HIGH widths

```text
SHORT HIGH = 844 T = 237.956 us
LONG  HIGH = 1664 T = 469.145 us
```

### Exact ROM LOW widths jsou kontextové

```text
SHORT LOW leader      = 918 T  = 258.819 us
SHORT LOW tape mark   = 908 T  = 256.000 us
SHORT LOW data S->L   = 896 T  = 252.617 us
SHORT LOW data S->S   = 906 T  = 255.436 us

LONG LOW tape mark    = 1728 T = 487.189 us
LONG LOW data L->L    = 1716 T = 483.806 us
LONG LOW data L->S    = 1726 T = 486.626 us
```

### Implementační požadavek

Pokud waveform engine umí kontextové LOW intervaly, implementovat je přesně podle tabulky výše.

Pokud současná architektura dovoluje pouze jeden H/L pár pro SHORT a LONG, nepoužívat staré 250/250, 500/500 jako „exact“. V takovém případě nejprve rozšířit model, aby native NORMAL 1× mohl mít context-dependent LOW.

Receiver native MZ-800:

```text
DLY3 = $52 = 82
sample window = 1233..1285 T
              = 347.630..362.291 us
```

---

## 3.2 NORMAL 2× — Intercopy 2400 Bd

Intercopy writer row:

```text
readpoint = $20
```

Kanonický waveform:

```text
SHORT HIGH = 113.621 us
SHORT LOW  = 139.278 us
LONG  HIGH = 234.573 us
LONG  LOW  = 260.229 us
```

Speed ratio:

```text
2400 / 1200 = 2:1
```

---

## 3.3 NORMAL 3× — Intercopy 2800 Bd

Intercopy writer row:

```text
readpoint = $16
```

Kanonický waveform:

```text
SHORT HIGH =  87.965 us
SHORT LOW  = 124.617 us
LONG  HIGH = 175.930 us
LONG  LOW  = 223.577 us
```

Skutečný historický poměr rychlosti:

```text
2800 / 1200 = 7:3 = 2.333333...
```

Projektový název `1:3` je pouze label, ne matematické 3.000×.

---

## 3.4 NORMAL 4× — Intercopy 3200 Bd

Intercopy writer row:

```text
readpoint = $11
```

Kanonický waveform:

```text
SHORT HIGH =  76.969 us
SHORT LOW  = 117.286 us
LONG  HIGH = 157.604 us
LONG  LOW  = 179.595 us
```

Skutečný poměr rychlosti:

```text
3200 / 1200 = 8:3 = 2.666666...
```

Projektový název `1:4` je pouze label, ne matematické 4.000×.

Toto je povinná oprava proti staré hodnotě:

```text
112 / 80
176 / 160
```

Ta není NORMAL 4× writer timing z Intercopy.

---

# 4. IC 2×–4×

Intercopy FAST IPL používá pro turbo data stejný fyzický FM writer a stejné speed rows jako Intercopy NORMAL.

Proto pro IC turbo payload použít stejné pulsy jako příslušný Intercopy speed slot.

## IC 2×

```text
speed byte / DLY3 family = $20

SHORT HIGH = 113.621 us
SHORT LOW  = 139.278 us
LONG  HIGH = 234.573 us
LONG  LOW  = 260.229 us
```

## IC 3×

```text
speed byte / DLY3 family = $16

SHORT HIGH =  87.965 us
SHORT LOW  = 124.617 us
LONG  HIGH = 175.930 us
LONG  LOW  = 223.577 us
```

## IC 4×

```text
speed byte / DLY3 family = $11

SHORT HIGH =  76.969 us
SHORT LOW  = 117.286 us
LONG  HIGH = 157.604 us
LONG  LOW  = 179.595 us
```

### IC loader receiver

IC loader patchuje monitor a přidává EDGE hook.

Speed bytes:

```text
IC 2× = $20
IC 3× = $16
IC 4× = $11
```

Receiver windows z disassembly:

```text
IC 2×: 174.520 .. 189.181 us
IC 3×: 135.048 .. 149.709 us
IC 4×: 115.313 .. 129.974 us
```

### IC header timing

FAST IPL header se v Intercopy zapisuje při 1200 Bd pomocí Intercopy writeru.

Použít:

```text
SHORT HIGH = 234.573 us
SHORT LOW  = 263.894 us
LONG  HIGH = 469.145 us
LONG  LOW  = 494.802 us
```

Nezaměňovat s native ROM NORMAL 1×. Oba jsou kompatibilní 1200-Bd waveformy, ale jejich software-edge schedule není identický.

---

# 5. TurboCopy 2×–3×

Turbo Copy V1.22 nepoužívá Intercopy software-delay writer.

Používá:

```text
8253 counter 0
MODE 3
control word = $36
```

Writer vypočítává:

```text
SHORT_COUNT = floor(480 * numerator / denominator) + 71
LONG_COUNT  = 2 * SHORT_COUNT
```

Nominal hardware clock counteru:

```text
CKMS = 1.10 MHz
```

---

## 5.1 TC 2×

Ratio:

```text
1 / 2
```

Counter values:

```text
SHORT_COUNT = 311
LONG_COUNT  = 622
```

8253 MODE3 half periods:

```text
SHORT = 156 / 155 CKMS ticks
LONG  = 311 / 311 CKMS ticks
```

Při 1.10 MHz:

```text
SHORT HIGH = 141.818 us
SHORT LOW  = 140.909 us
LONG  HIGH = 282.727 us
LONG  LOW  = 282.727 us
```

TC loader speed byte:

```text
DLY3 = $29 = 41
```

Receiver window:

```text
185.797 .. 200.458 us
```

---

## 5.2 TC 3×

Ratio:

```text
1 / 3
```

Counter values:

```text
SHORT_COUNT = 231
LONG_COUNT  = 462
```

8253 MODE3 half periods:

```text
SHORT = 116 / 115 CKMS ticks
LONG  = 231 / 231 CKMS ticks
```

Při 1.10 MHz:

```text
SHORT HIGH = 105.455 us
SHORT LOW  = 104.545 us
LONG  HIGH = 210.000 us
LONG  LOW  = 210.000 us
```

TC loader speed byte:

```text
DLY3 = $1B = 27
```

Receiver window:

```text
130.537 .. 145.198 us
```

---

# 6. Jedna finální tabulka pro implementaci

| Režim | Zdroj | SHORT H [us] | SHORT L [us] | LONG H [us] | LONG L [us] | loader DLY3 / readpoint |
|---|---|---:|---:|---:|---:|---|
| NORMAL 1× | MZ-800 ROM | 237.956 | context | 469.145 | context | `$52` |
| NORMAL 2× | Intercopy 2400 | 113.621 | 139.278 | 234.573 | 260.229 | `$20` row |
| NORMAL 3× | Intercopy 2800 | 87.965 | 124.617 | 175.930 | 223.577 | `$16` row |
| NORMAL 4× | Intercopy 3200 | 76.969 | 117.286 | 157.604 | 179.595 | `$11` row |
| IC 2× | Intercopy 2400 | 113.621 | 139.278 | 234.573 | 260.229 | `$20` |
| IC 3× | Intercopy 2800 | 87.965 | 124.617 | 175.930 | 223.577 | `$16` |
| IC 4× | Intercopy 3200 | 76.969 | 117.286 | 157.604 | 179.595 | `$11` |
| TC 2× | TurboCopy 1.22 / 8253 | 141.818 | 140.909 | 282.727 | 282.727 | `$29` |
| TC 3× | TurboCopy 1.22 / 8253 | 105.455 | 104.545 | 210.000 | 210.000 | `$1B` |

NORMAL 1× `context` znamená použít přesnou ROM tabulku:

```text
SHORT LOW leader    258.819 us
SHORT LOW mark      256.000 us
SHORT LOW S->L      252.617 us
SHORT LOW S->S      255.436 us

LONG LOW mark       487.189 us
LONG LOW L->L       483.806 us
LONG LOW L->S       486.626 us
```

---

# 7. Leader/framing — jednotná politika pro všechny režimy

Použít jednu společnou leader politiku:

```text
HEADER = 11000 SHORT
DATA   =  5500 SHORT
```

To platí pro všechny režimy:

| Režim | HEADER leader | Loader DATA leader | Turbo/PWM DATA leader |
|---|---:|---:|---:|
| NORMAL 1× | 11000 S | — | 5500 S |
| NORMAL 2× | 11000 S | — | 5500 S |
| NORMAL 3× | 11000 S | — | 5500 S |
| NORMAL 4× | 11000 S | — | 5500 S |
| IC 2× | 11000 S | — | 5500 S |
| IC 3× | 11000 S | — | 5500 S |
| IC 4× | 11000 S | — | 5500 S |
| TC 2× | 11000 S | 5500 S | 5500 S |
| TC 3× | 11000 S | 5500 S | 5500 S |

`S` = jeden kompletní SHORT puls.

Pro TC platí 5500 SHORT jak pro loader DATA blok, tak pro následný turbo payload.

QDTool i SD2CMT2 mají používat **11000/5500 jednotně pro všechny režimy**.

Leader counts se nesmí měnit podle délky pulsu.

---

# 8. Požadované změny v QDTool

1. Zrušit použití jedné obecné NORMAL tabulky založené na starých aproximacích.
2. Zavést explicitní profile table dle kapitoly 6.
3. NORMAL 1× musí používat ROM-derived context-dependent LOW timing.
4. NORMAL 2×–4× musí používat Intercopy writer rows.
5. IC 2×–4× musí používat stejné Intercopy writer rows pro turbo payload.
6. IC header musí používat Intercopy 1200 timing.
7. TC 2×–3× musí používat TurboCopy/8253 timing, ne Intercopy timing.
8. Pro TC je preferovaný interní model založený na timer count (`311/622`, `231/462`), nikoli pouze na zaokrouhlených µs.
9. WAV exporter nesmí být závislý na integer-sample pulse length bez error accumulation.
10. Pro rychlé profily preferovat 88.2 kHz nebo vyšší sample rate.
11. Přidat unit test, který z exportovaného WAV změří edge-to-edge intervaly a ověří každý profil proti této tabulce.
12. Přidat round-trip testy alespoň pro NORMAL 1×–4×, IC 2×–4× a TC 2×–3×.

---

# 9. Požadované změny v MZ-SD2CMT2

1. Přepsat playback timing source-of-truth na tabulku z kapitoly 6.
2. Native NORMAL 1× generovat podle ROM timingů, včetně context-dependent LOW.
3. NORMAL 2×–4× generovat podle Intercopy writer rows.
4. IC 2×–4× generovat podle stejných Intercopy writer rows.
5. Zachovat IC speed bytes:

```text
2× = $20
3× = $16
4× = $11
```

6. TC 2×–3× generovat z TurboCopy timer modelu.
7. Zachovat TC speed bytes:

```text
2× = $29
3× = $1B
```

8. Leader counts nastavit jednotně na **11000 SHORT pro HEADER a 5500 SHORT pro každý DATA/turbo blok**.
9. Neměnit leader count kvůli změně pulse widths.
10. Do komentářů u každého profilu uvést zdroj:

```text
MZ800-ROM-1Z013B
INTERCOPY-V10.2-WRITER
TURBOCOPY-V1.22-8253
```

11. Přidat compile-time nebo unit-test kontrolu konstant.
12. Přidat hardware regression matrix:

```text
NORMAL 1×
NORMAL 2×
NORMAL 3×
NORMAL 4×
IC 2×
IC 3×
IC 4×
TC 2×
TC 3×
```

---

# 10. Staré hodnoty, které nesmí zůstat jako hlavní source-of-truth

Nepoužívat jako kanonické timingy:

```text
NORMAL 1× = 250/250, 500/500
NORMAL 3× = 113/113, 204/204
NORMAL 4× = 112/80, 176/160

IC 2× = 144/112, 256/224
IC 3× = 112/96, 224/192
IC 4× = 112/80, 176/160

TC 2× = 144/144, 288/288
TC 3× = 112/112, 204/204
```

Tyto hodnoty mohou zůstat pouze v historii/test fixtures, ne jako hlavní timing reference pro novou implementaci.

---

# 11. Přesná mapování názvů

## Intercopy

```text
NORMAL 1× -> native ROM 1200 Bd
NORMAL 2× -> Intercopy 2400 Bd / 2:1
NORMAL 3× -> Intercopy 2800 Bd / 7:3
NORMAL 4× -> Intercopy 3200 Bd / 8:3

IC 2× -> Intercopy 2400 Bd / DLY3 $20
IC 3× -> Intercopy 2800 Bd / DLY3 $16
IC 4× -> Intercopy 3200 Bd / DLY3 $11
```

## TurboCopy

```text
TC 2× -> 2:1 / SHORT_COUNT 311 / DLY3 $29
TC 3× -> 3:1 / SHORT_COUNT 231 / DLY3 $1B
```

Nepřepočítávat názvy `1:3` a `1:4` na matematické 3× a 4× tam, kde Intercopy používá 7:3 a 8:3.

---

# 12. Akceptační kritéria

Implementace je hotová pouze pokud:

1. QDTool a SD2CMT2 používají stejnou kanonickou tabulku timingů.
2. NORMAL 1× je odvozen z MZ-800 ROM 1Z-013B.
3. NORMAL 2×–4× jsou odvozeny z Intercopy V10.2 writeru.
4. IC 2×–4× používají Intercopy V10.2 writer timing a odpovídající DLY3 bytes.
5. TC 2×–3× používají TurboCopy V1.22 8253 count model a odpovídající DLY3 bytes.
6. Leader counts jsou ve všech režimech sjednoceny na **HEADER 11000 SHORT / DATA 5500 SHORT**; u TC platí 5500 také pro loader DATA i turbo DATA.
7. NORMAL 4× už nepoužívá `112/80,176/160`.
8. NORMAL 3× už nepoužívá `113/113,204/204` jako hlavní timing.
9. IC 4× turbo payload používá Intercopy 3200 timing `76.969/117.286,157.604/179.595 us`.
10. TC 3× používá count `231/462`, nikoliv Intercopy timing.
11. Exportovaný waveform projde automatickou edge-duration kontrolou.
12. Reálné názvy rychlostí 7:3 a 8:3 jsou v kódu/komentářích dokumentovány.

---

# 13. Krátká source-of-truth tabulka pro vložení do kódu

```text
NORMAL_1X  ROM 1Z013B
  S.H = 237.956
  S.L = context: 258.819 / 256.000 / 252.617 / 255.436
  L.H = 469.145
  L.L = context: 487.189 / 483.806 / 486.626

NORMAL_2X / IC_2X  INTERCOPY 2400
  S = 113.621 / 139.278
  L = 234.573 / 260.229
  IC DLY3 = 0x20

NORMAL_3X / IC_3X  INTERCOPY 2800 (7:3)
  S = 87.965 / 124.617
  L = 175.930 / 223.577
  IC DLY3 = 0x16

NORMAL_4X / IC_4X  INTERCOPY 3200 (8:3)
  S = 76.969 / 117.286
  L = 157.604 / 179.595
  IC DLY3 = 0x11

TC_2X  TURBOCOPY 1.22
  8253 MODE3
  SHORT_COUNT = 311
  LONG_COUNT  = 622
  S = 141.818 / 140.909
  L = 282.727 / 282.727
  DLY3 = 0x29

TC_3X  TURBOCOPY 1.22
  8253 MODE3
  SHORT_COUNT = 231
  LONG_COUNT  = 462
  S = 105.455 / 104.545
  L = 210.000 / 210.000
  DLY3 = 0x1B
```
