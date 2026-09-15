# Sharp MZ CMT — Intercopy 10.2 & TurboCopy 1.22 Timing Reference

**Účel:** zdrojová reference pro stanovení časových konstant v **QDTool/QDTools** a **MZ-SD2CMT2**.

**Datum analýzy:** 2026-09-14

Tento dokument je určen k používání společně s:

- `CMT_TIMING_REFERENCE.md`
- `LEADER_PULSES_UNIFICATION.md`

Neřeší znovu kompletní Sharp ROM timing ani politiku leader counts. Doplňuje je o přímou analýzu dvou historických kopírek:

- **Intercopy V10.2**
- **Turbo Copy V1.22 / TC1.22**

Hlavní zásada:

> Vždy rozlišovat **writer-generated waveform**, **receiver decision timing**, **project compatibility waveform** a **leader/framing policy**. Tyto čtyři věci nejsou zaměnitelné.

---

# 1. Vstupní binárky

## 1.1 Intercopy V10.2

Soubor:

```text
Incopy102(3).mzf
```

SHA-256:

```text
b7b8669791a12c0212b046defdd37ca1f2687e595589c1cddf07bdb2ef443439
```

Identifikace v MZF filename:

```text
Intercopy V10.2
```

Poznámka: MZ znaková sada není ASCII, ale suffix `V10.2` je v headeru přímo čitelný.

---

## 1.2 Turbo Copy V1.22

Soubor:

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

Filename končí:

```text
V1.22
```

Startovací rutina kopíruje hlavní pracovní obraz z `$1200` do oblasti začínající `$E471`.

Relokační rozdíl použitý níže:

```text
$E471 - $1200 = $D271
```

Tím lze přímo mapovat kód uložený v MZF na runtime adresy TurboCopy.

---

# 2. Evidence labels používané v tomto dokumentu

```text
COPIER-WRITER-EXACT
    instrukce skutečného writeru konkrétní kopírky
    + deterministický výpočet Z80/timer timingů

COPIER-TIMER-EXACT
    přesná hodnota programovaného HW čítače / timer mode;
    převod na µs může stále záviset na přesné fyzické clock frekvenci

RECEIVER-EXACT
    přesný loader/monitor receive path a decision window

HW-NOMINAL
    hodnota z hardware/service dokumentace, např. CKMS = 1.10 MHz

PROJECT-COMPAT
    záměrně zvolený pevný waveform s dobrou receive margin;
    nemusí být totožný s historickým writerem

PROJECT-FRAMING
    kanonická politika leader/tapemark counts projektu;
    nesmí být odvozována z pulse width
```

Pro rozhodování o změně konstant platí:

```text
writer-exact != receiver-exact != compatibility waveform != framing
```

---

# 3. Intercopy V10.2 — rozlišení NORMAL a FAST IPL

## 3.1 Mode selector

Intercopy používá mode byte na `$16F3`.

Analýza programu dává:

```text
0 = NORMAL
1 = SINCLAIR
2 = CPM
3 = TURBO
4 = FAST IPL
```

To je důležité, protože společná FM writer rutina je používána více režimy.

---

## 3.2 NORMAL používá společný FM writer přímo

Pro režim NORMAL vede cesta v Intercopy přibližně:

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

Takže časování odvozené z `$16F7/$1D8E` **není omylem jen FAST IPL timing**.

NORMAL používá tento writer přímo při zvolené rychlosti.

---

## 3.3 FAST IPL používá stejný fyzický FM writer jinak

FAST IPL má zvláštní setup.

Rutina kolem `$201C`:

1. uloží aktuálně zvolenou turbo rychlost,
2. nastaví dočasně `1200 Bd`,
3. zapíše loader/header,
4. původní rychlost obnoví,
5. turbo body zapíše společným FM writerem při vybrané rychlosti.

Tedy:

```text
FAST IPL header
    -> 1200 Bd physical FM timing

FAST IPL turbo body
    -> selected 2400 / 2800 / 3200 Bd physical FM timing
```

Framing FAST IPL je samostatná věc a řídí se `LEADER_PULSES_UNIFICATION`.

---

# 4. Intercopy speed table `$16F7`

Relevantní řádky:

```text
speed   readpoint   t1   t2   t3   t4
--------------------------------------
1200      $4D      $40  $80  $48  $87
2400      $20      $1F  $40  $26  $47
2800      $16      $18  $30  $22  $3D
3200      $11      $15  $2B  $20  $31
```

Význam:

```text
readpoint
    parametr receiveru / DLY3 family

t1
    SHORT první půlvlna

t2
    LONG první půlvlna

t3
    SHORT druhá půlvlna

t4
    LONG druhá půlvlna
```

`$1DF5` vloží čtyři timing bytes přímo do self-modifikované pulse rutiny `$1D8E`.

To je přímý důkaz, že Intercopy waveform může být asymetrický.

---

# 5. Intercopy writer `$1D8E` — výpočet pulse widths

Pro běžný steady data path je možné intervaly vyjádřit:

```text
první půlvlna:
    N = ($F2 + timing_operand) & $FF
    T = 182 + 13*N

druhá půlvlna:
    N = ($F3 + timing_operand) & $FF
    T = 169 + 13*N
```

Použitý PAL MZ-800 CPU clock:

```text
3,546,875 Hz
```

Převod:

```text
time_us = T / 3,546,875 * 1,000,000
```

Poznámka:

- tabulka níže je referenční steady-data waveform;
- některé leader/tape-mark kontexty mohou mít nepatrnou caller-dependent odchylku;
- například u některého opakovaného LONG kontextu může být rozdíl řádu 1 T;
- pro pevný čtyřčíselný profil jsou hodnoty níže správnou code-derived referencí.

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

Tento výsledek je zároveň velmi dobrý sanity check proti standardnímu MZ-800 CMT waveformu.

---

## 6.2 2400 Bd

```text
SHORT HIGH = 403 T = 113.621 us
SHORT LOW  = 494 T = 139.278 us

LONG  HIGH = 832 T = 234.573 us
LONG  LOW  = 923 T = 260.229 us
```

Historický faktor:

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

Historický speed ratio:

```text
7:3 = 2.333333...
```

V současném projektovém názvosloví se tento stupeň často označuje jako:

```text
NORMAL 1:3
IC 1:3
```

ale **nejde matematicky o 3.000×**.

---

## 6.4 3200 Bd

```text
SHORT HIGH = 273 T =  76.969 us
SHORT LOW  = 416 T = 117.286 us

LONG  HIGH = 559 T = 157.604 us
LONG  LOW  = 637 T = 179.595 us
```

Historický speed ratio:

```text
8:3 = 2.666666...
```

V současném projektovém názvosloví se tento stupeň často označuje jako:

```text
NORMAL 1:4
IC 1:4
```

ale **nejde matematicky o 4.000×**.

---

# 7. Kritický závěr pro NORMAL 3 / NORMAL 4

Pro historickou reprodukci Intercopy NORMAL:

```text
NORMAL "3" / 2800 Bd:
    SHORT 87.965 / 124.617 us
    LONG  175.930 / 223.577 us

NORMAL "4" / 3200 Bd:
    SHORT 76.969 / 117.286 us
    LONG  157.604 / 179.595 us
```

Tedy historický Intercopy NORMAL 3200 **není**:

```text
112 / 80 us
176 / 160 us
```

a není ani:

```text
96 / 96 us
192 / 192 us
```

Je záměrně / algoritmicky asymetrický.

Pro NORMAL 3200 je navíc důležitý poměr rozhodující první půlvlny:

```text
LONG_HIGH / SHORT_HIGH
= 157.604 / 76.969
≈ 2.048
```

To dává výrazně větší SHORT/LONG separation než profil:

```text
176 / 112 ≈ 1.571
```

Toto je důležité pro Barbarian-style adaptive receiver.

---

# 8. Intercopy IC loader — receiver timing není writer waveform

`CMT_TIMING_REFERENCE` už stanovuje přesné receiver windows pro IC.

IC loader:

- spouští low-monitor copy z RAM,
- patchuje DLY3,
- přidává EDGE-return hook s netto zpožděním `+86 T`.

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

Současné SD2CMT2 IC waveforms:

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

Tyto hodnoty jsou:

```text
PROJECT-COMPAT / HW-tested compatibility waveform
```

Nejsou to:

```text
COPIER-WRITER-EXACT Intercopy waveform
```

Proto se v QDTool ani SD2CMT2 nesmí jedna tabulka automaticky používat pro:

```text
NORMAL 3200 exact
a
IC 1:4 compatibility
```

---

# 9. Doporučené oddělení Intercopy profilů v kódu

Pro oba projekty je vhodné mít koncepčně dvě vrstvy.

## Historická writer reference

```text
INTERCOPY_NORMAL_1200
INTERCOPY_NORMAL_2400
INTERCOPY_NORMAL_2800
INTERCOPY_NORMAL_3200
```

s code-derived H/L hodnotami z kapitoly 6.

Stejný physical timing engine je relevantní i pro FAST IPL turbo body.

## Receiver-compatible SD2CMT2 profiles

```text
IC_1_2_COMPAT
IC_1_3_COMPAT
IC_1_4_COMPAT
```

s dosavadními HW-proven hodnotami.

Změna jedné vrstvy nesmí automaticky měnit druhou.

---

# 10. QDTool WAV — důsledek sample rate

Při 44.1 kHz má jeden sample:

```text
22.675737 us
```

Intercopy NORMAL 3200 pak odpovídá přibližně:

```text
SHORT HIGH  76.969 us -> 3.394 samples
SHORT LOW  117.286 us -> 5.172 samples

LONG HIGH   157.604 us -> 6.950 samples
LONG LOW    179.595 us -> 7.920 samples
```

To je hrubá mřížka, zejména pro nejkratší 77-us půlvlnu.

Proto pro waveform-fidelity export:

```text
88.2 kHz
```

je výrazně vhodnější než 44.1 kHz.

Pokud QDTool zůstane na 44.1 kHz, musí používat průběžný fractional/error accumulator; jednotlivé pulsy budou stále výrazně kvantizované.

---

# 11. Turbo Copy V1.22 — nová přímá writer analýza

Starší reference měla:

```text
TC loader/readpoint = exact
TC historical writer loop = not yet directly established
```

`Tc122(4).mzf` tuto mezeru pro **Turbo Copy V1.22** zavírá.

Pozor:

> Tato část je source-exact pro TC1.22. Nesmí se bez dalšího tvrdit, že V1.0, V1.2, V1.21 a V1.22 mají bit-identický writer.

---

# 12. TurboCopy V1.22 — runtime relocation

Start na `$2D98` provádí relocaci hlavní pracovní oblasti do high monitor/work RAM prostoru.

Relevantní mapping:

```text
source $1974 -> runtime $EBE5
source $19A0 -> runtime $EC11
source $19A8 -> runtime $EC19
```

Právě `$EBE5` je fyzický CMT writer.

---

# 13. TurboCopy V1.22 — 8253 counter 0

TC1.22 programuje 8253 control register hodnotou:

```text
$36
```

Dekódování 8253 control word:

```text
counter = 0
RW      = LSB then MSB
mode    = MODE 3
format  = binary
```

MODE3 je square-wave generator.

Sharp MZ-800 service manual uvádí pro counter #0:

```text
CLK0 / CKMS = nominal 1.10 MHz
```

a OUT0 je přiveden také na:

```text
Z80 PIO PA4
```

TurboCopy tedy používá HW timer jako časovou základnu writeru.

Evidence:

```text
COPIER-TIMER-EXACT
+
HW-NOMINAL pro převod na µs
```

---

# 14. TurboCopy V1.22 — writer `$EBE5`

Zjednodušený význam runtime rutiny:

```asm
LD HL,(E25E)        ; SHORT timer count
JR NC,short
ADD HL,HL           ; Carry => LONG = 2 * SHORT

wait OUT0 phase
LD A,$03
OUT ($D3),A         ; jedna CMT WRITE hrana

...

wait opposite OUT0 phase
LD A,$02
OUT ($D3),A         ; druhá CMT WRITE hrana

CALL $E83D          ; reload 8253 counter 0 z HL
```

Důležité závěry:

```text
SHORT counter = E25E
LONG  counter = 2 * E25E
```

To je přímý writer kód.

LONG/SHORT ratio v timer count domain je tedy přesně:

```text
2:1
```

---

# 15. TurboCopy V1.22 — výpočet SHORT counteru

Runtime `$E87C/$E89E` používá:

```text
base = 480
ratio numerator   = byte E49C
ratio denominator = byte E49B
offset = 71
```

Výsledkem je:

```text
SHORT_COUNT =
    floor(480 * numerator / denominator) + 71
```

a:

```text
LONG_COUNT = 2 * SHORT_COUNT
```

Toto je:

```text
COPIER-TIMER-EXACT
```

V dodaném TC1.22 je výchozí stav:

```text
E49B = 2
E49C = 1
```

tedy:

```text
SHORT_COUNT
= floor(480 * 1 / 2) + 71
= 240 + 71
= 311

LONG_COUNT = 622
```

To odpovídá režimu 2:1.

Pro ratio 3:1:

```text
SHORT_COUNT
= floor(480 * 1 / 3) + 71
= 160 + 71
= 231

LONG_COUNT = 462
```

---

# 16. TurboCopy V1.22 — MODE3 timer half-periods

Pro 8253 MODE3:

- sudý count: HIGH a LOW mají `N/2` timer clocks,
- lichý count: jedna fáze má `(N+1)/2`, druhá `(N-1)/2`.

## TC 2:1

```text
SHORT count = 311
    MODE3 halves = 156 / 155 CKMS ticks

LONG count = 622
    MODE3 halves = 311 / 311 CKMS ticks
```

Při service-manual nominal:

```text
CKMS = 1.10 MHz
```

vyjde raw OUT0 reference:

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

Při 1.10 MHz:

```text
SHORT:
    116 ticks = 105.455 us
    115 ticks = 104.545 us

LONG:
    231 ticks = 210.000 us
    231 ticks = 210.000 us
```

Tyto hodnoty jsou **timer OUT0 timing**, ne ještě absolutně přesný PC1 cassette-WRITE edge schedule.

---

# 17. TurboCopy writer — proč nelze timer halves slepě vydávat za exact WRITE µs

TurboCopy writer nepřepojuje CMT WRITE přímo HW výstupem OUT0.

Z80:

1. polluje PIO PA4 / OUT0,
2. detekuje změnu timer phase,
3. až potom provede `OUT ($D3),A`.

Relevantní poll smyčka má řádově:

```text
IN + AND + JP = 25 Z80 T
```

Při 3.546875 MHz:

```text
25 T ≈ 7.048 us
```

Obě fyzické WRITE hrany mají podobnou následnou instrukční režii, ale okamžik zachycení timer edge je kvantovaný pollingem.

Proto:

```text
exact historical TC cassette WRITE waveform
```

není ideálně reprezentován jednou dokonale konstantní H/L čtveřicí.

Code-exact historická reprezentace je:

```text
MODE3 counter model
+
counter values
+
Z80 polling
```

Pro pevný čtyřčíselný profil je nutné zvolit aproximaci.

---

# 18. TurboCopy receiver — přesné readpointy

`CMT_TIMING_REFERENCE` stanovuje TurboCopy receiver path.

90B loader je zaveden na:

```text
$D400
```

Speed byte je:

```text
$D44B
```

a loader provede:

```asm
LD A,($D44B)
LD ($0A4B),A
```

bez Intercopy `+86 T` EDGE hooku.

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

Současný SD2CMT2 používá:

```text
TC 1:2:
    SHORT 144 / 144 us
    LONG  288 / 288 us

TC 1:3:
    SHORT 112 / 112 us
    LONG  204 / 204 us
```

Tyto hodnoty mají dobrou receiver margin.

Nejsou však doslovnou reprezentací každého writer edge TC1.22.

Jejich status zůstává:

```text
PROJECT-COMPAT
```

Nová TC1.22 analýza poskytuje vedle nich historickou timer reference:

```text
TC 2:1:
    timer-centered SHORT ≈ 141 us
    timer-centered LONG  ≈ 283 us

TC 3:1:
    timer-centered SHORT ≈ 105 us
    timer-centered LONG  ≈ 210 us
```

s cca ±jedním polling quantum na jednotlivých software-detected edges.

Proto se nedoporučuje bez HW regression pouze přepsat stávající SD2CMT2 TC constants na timer-centered čísla.

---

# 20. TurboCopy V1.22 loader fingerprint

TC1.22 obsahuje 90B loader family zaváděnou na `$D400`.

V dodané binárce má extrahovaný 90B template před runtime metadata patchingem SHA-256:

```text
8f90221c447fe891a84364a437e66fae3d348bcc72d7cc2bc4d70433e7d7a0d1
```

To není bit-identický hash s dříve dokumentovanou V1.21-derived SD2CMT2 šablonou.

Z toho plyne:

> Nezaměňovat „stejná loader family / stejný timing mechanism“ s „bit-identická verze kopírky“.

Pro timing-critical receiver část však TC1.22 přímo obsahuje princip:

```text
speed byte -> $0A4B
```

stejně jako analyzovaná TC family.

---

# 21. Leader/framing policy — používat samostatný dokument

Pulse timing nesmí měnit leader counts.

Pro QDTool/SD2CMT2 používat `LEADER_PULSES_UNIFICATION` jako projektovou politiku.

Kanonicky:

```text
NATIVE NORMAL:
    HEADER = 22000 SHORT
    DATA   = 11000 SHORT

SPECIAL LOADER:
    HEADER       = 11000 SHORT
    loader DATA  =  5500 SHORT
    turbo DATA   =  5500 SHORT
```

Pro IC:

```text
11000 / 5500
```

má navíc silnou historickou a HW podporu.

Pro TC:

```text
11000 / 5500 / 5500
```

je **SD2CMT2 canonical/optimized framing policy**, nikoliv tvrzení o přesném historickém framingu každé TurboCopy verze.

Tato politika nesmí být zaměněna za pulse-width analýzu tohoto dokumentu.

---

# 22. Doporučené constants model pro QDTool

QDTool by měl rozlišit minimálně:

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

Pokud MFI/MTI zachovává historická projektová jména:

```text
NORMAL 1:3 -> Intercopy 2800 / 7:3
NORMAL 1:4 -> Intercopy 3200 / 8:3
```

musí být toto mapování výslovně dokumentováno.

---

# 23. Doporučené constants model pro SD2CMT2

## NATIVE / historical reproduction path

Pokud cílem režimu je reprodukovat Intercopy NORMAL:

```text
použít Intercopy writer-exact reference
```

## IC hardware-compat path

Pokud cílem je současný HW-proven IC loader:

```text
zachovat IC compatibility waveforms
```

dokud nejsou writer-exact hodnoty samostatně otestovány na reálném MZ.

## TC hardware-compat path

Současné:

```text
TC 1:2 = 144/144, 288/288
TC 1:3 = 112/112, 204/204
```

mají dobré receiver margins.

TC1.22 source-exact writer je lépe modelovat pomocí:

```text
8253 MODE3 + counter values + polling
```

než tvrdit, že historický writer měl jednu absolutně konstantní symetrickou čtveřici.

---

# 24. Doporučená tabulka zdrojové autority

| Profil / údaj | Autoritativní hodnota | Evidence |
|---|---|---|
| MZ-800 NORMAL 1× ROM | dle `CMT_TIMING_REFERENCE` | ROM-PATH-HIGH |
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

# 25. Co se nesmí znovu zaměnit

## 25.1 NORMAL 3200 není IC 1:4 waveform

Zakázaná inference:

```text
IC 1:4 speed byte = $11
=> NORMAL 1:4 musí být 112/80,176/160
```

Správně:

```text
$11 je receiver/readpoint speed byte
+
Intercopy 3200 writer používá vlastní t1..t4 row
```

---

## 25.2 Intercopy 3/4 nejsou matematické 3×/4×

```text
"3" -> 2800 Bd -> 7:3 ≈ 2.333×
"4" -> 3200 Bd -> 8:3 ≈ 2.667×
```

---

## 25.3 TurboCopy receiver delay není writer pulse width

```text
$29 / $1B
```

jsou receive DLY3 values.

Writer TC1.22 používá:

```text
8253 MODE3
E25E counter
```

---

## 25.4 TC fixed µs profile není bit-exact TC writer

Historický writer má:

```text
HW timer edge
-> Z80 polling
-> software CMT WRITE edge
```

Proto fixní waveform je nutně aproximace.

---

## 25.5 Leader count není pulse duration

Použít `LEADER_PULSES_UNIFICATION`.

Rychlejší waveform nesmí automaticky měnit počet leader pulzů.

---

# 26. Doporučení pro další implementaci

## QDTool

1. Opravit NORMAL 1:4 tak, aby nebyl kopií IC 1:4 compatibility timing.
2. Pokud názvy 1:3/1:4 reprezentují Intercopy slots:
   - 1:3 -> 2800 Bd code-derived row,
   - 1:4 -> 3200 Bd code-derived row.
3. Pro WAV s 3200 Bd preferovat 88.2 kHz nebo vyšší sample rate.
4. Oddělit writer-fidelity profily od loader-compatibility profilů.
5. Přidat automatické edge-duration testy pro všechny waveform profily.

## SD2CMT2

1. Neměnit HW-proven IC/TC compatibility constants pouze na základě historické fidelity.
2. Pokud se zavede faithful Intercopy NORMAL profil, použít code-derived row.
3. Pokud se zavede faithful TurboCopy profil, ideálně modelovat MODE3 count a polling, ne jen čtyři absolutní µs hodnoty.
4. Všechny změny IC/TC časování provést až s real-MZ regression.
5. Leader counts držet podle `LEADER_PULSES_UNIFICATION`.

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

# 28. Finální rozhodovací pravidlo

Při nastavování nové konstanty v QDTool nebo SD2CMT2 se nejdřív musí odpovědět:

```text
Chci:
A) historicky věrný writer waveform?
B) waveform s nejlepší receiver margin?
C) kanonický project framing?
```

Pak:

```text
A -> použij tento dokument / COPIER-WRITER-EXACT nebo COPIER-TIMER-EXACT

B -> použij CMT_TIMING_REFERENCE receiver windows
     + HW regression

C -> použij LEADER_PULSES_UNIFICATION
```

Nikdy neodvozovat jednu z těchto vrstev automaticky z druhé.
