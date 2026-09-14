# QDTool – aktuální časování CMT exportu

Tento dokument popisuje **skutečné hodnoty aktuálně použité v QDTool** pro export
do WAV, LEP a L16. Je určen jako přenosná reference pro sjednocení s jinými
projekty. Hodnoty vycházejí přímo z implementace v `SharpTapeProfileEncoder.cs`
a `SharpTapeExporter.cs`.

Stav dokumentu: 2026-09-14.

## 1. Význam jednoho pulzu

Jeden pulz se skládá ze dvou intervalů:

- běžná polarita: `HIGH` a potom `LOW`,
- invertovaná polarita: `LOW` a potom `HIGH`.

V tabulkách jsou proto samostatně uvedeny délky HIGH a LOW části. `SHORT` kóduje
bit 0 a `LONG` kóduje bit 1. Polarita nemění délky, pouze pořadí obou částí.

## 2. Pevné profily pulzů

Všechny hodnoty v následující tabulce jsou v mikrosekundách.

| Profil časování | SHORT HIGH | SHORT LOW | LONG HIGH | LONG LOW | Použití |
|---|---:|---:|---:|---:|---|
| Intercopy 1200 | 234.573 | 263.894 | 469.145 | 494.802 | syntetická hlavička IC |
| Intercopy 2400 | 113.621 | 139.278 | 234.573 | 260.229 | NORMAL 1:2, turbo data IC 1:2 |
| Intercopy 2800 | 87.965 | 124.617 | 175.930 | 223.577 | NORMAL 1:3, turbo data IC 1:3 |
| Intercopy 3200 | 76.969 | 117.286 | 157.604 | 179.595 | NORMAL 1:4, turbo data IC 1:4 |
| MZ-700 normal | 240.000 | 264.000 | 464.000 | 494.000 | MZ700 1:1 |
| MZ-700 FAST3 | 80.000 | 80.000 | 160.000 | 160.000 | datová etapa MZ700 1:3 |
| TurboCopy 1:2 | 141.818181818 | 140.909090909 | 282.727272727 | 282.727272727 | turbo data TC 1:2 |
| TurboCopy 1:3 | 105.454545455 | 104.545454545 | 210.000000000 | 210.000000000 | turbo data TC 1:3 |

Poznámky:

- Označení NORMAL/IC 1:3 odpovídá analyzované řádce Intercopy 2800 Bd
  (historicky poměr 7:3).
- Označení NORMAL/IC 1:4 odpovídá analyzované řádce Intercopy 3200 Bd
  (historicky poměr 8:3).
- TC podporuje pouze 1:2 a 1:3. Profil TC 1:4 v projektu není.
- TurboCopy hodnoty jsou odvozeny z 8253 MODE 3 při nominálním taktu
  `CKMS = 1.10 MHz`:
  - TC 1:2: SHORT `156/155` ticků, LONG `311/311` ticků,
  - TC 1:3: SHORT `116/115` ticků, LONG `231/231` ticků.

## 3. Nativní MZ-800 monitor 1Z-013B

Profil NORMAL 1:1 nepoužívá jednu pevnou LOW hodnotu pro všechny situace.
Vychází z taktu `3 546 875 Hz`; délka je počítána přesně jako
`ticks * 1 000 000 / 3 546 875` mikrosekund.

HIGH část je nezávislá na kontextu:

| Typ pulzu | Ticky | Délka [µs] |
|---|---:|---:|
| SHORT HIGH | 844 T | 237.955947137 |
| LONG HIGH | 1664 T | 469.145374449 |

LOW část se volí podle oblasti a u dat také podle následujícího pulzu:

| Oblast | Aktuální pulz | Následující pulz | Ticky LOW | Délka LOW [µs] |
|---|---|---|---:|---:|
| leader | SHORT | bez vlivu | 918 T | 258.819383260 |
| tape mark | SHORT | bez vlivu | 908 T | 256.000000000 |
| tape mark | LONG | bez vlivu | 1728 T | 487.189427313 |
| data | SHORT | SHORT | 906 T | 255.436123348 |
| data | SHORT | LONG | 896 T | 252.616740088 |
| data | LONG | SHORT | 1726 T | 486.625550661 |
| data | LONG | LONG | 1716 T | 483.806167401 |

„Následující pulz“ znamená následující datový, synchronizační nebo trailing pulz
ve zarámovaném proudu. Toto rozlišení je důležité zachovat; nahrazení jednou
průměrnou LOW délkou není ekvivalentní současnému QDTool exportu.

## 4. Leadery, tape mark a zakončení bloku

Počet leader pulzů je sjednocen pro všechny profily:

| Typ etapy | Leader | Tape mark | Zakončení za daty |
|---|---:|---|---|
| hlavičková etapa | 11000 × SHORT | 40 × LONG, 40 × SHORT, 2 × LONG | 2 × LONG |
| datová/loader/turbo etapa | 5500 × SHORT | 20 × LONG, 20 × SHORT, 2 × LONG | standardně 2 × LONG |
| TC loader a TC turbo data | 5500 × SHORT | 20 × LONG, 20 × SHORT, 2 × LONG | 98 × SHORT |

Počet znamená počet **celých pulzů**; každý obsahuje HIGH+LOW nebo při inverzi
LOW+HIGH. Délka leaderu v čase tedy závisí na zvoleném profilu pulzů.

## 5. Etapy jednotlivých režimů

| Režim | Hlavičková etapa | Další etapa/etapy | Polarita | Pauza před etapou |
|---|---|---|---|---:|
| NORMAL 1:1 | MZ-800 1Z-013B, leader 11000 | tělo MZ-800 1Z-013B, leader 5500 | běžná | 0 ms |
| NORMAL 1:2 | Intercopy 2400, leader 11000 | tělo Intercopy 2400, leader 5500 | běžná | 0 ms |
| NORMAL 1:3 | Intercopy 2800, leader 11000 | tělo Intercopy 2800, leader 5500 | běžná | 0 ms |
| NORMAL 1:4 | Intercopy 3200, leader 11000 | tělo Intercopy 3200, leader 5500 | běžná | 0 ms |
| MZ700 1:1 | MZ-700 normal, leader 11000 | tělo MZ-700 normal, leader 5500 | běžná | 0 ms |
| MZ700 1:3 | MZ-800 1Z-013B, leader 11000 | tělo MZ-700 FAST3, leader 5500 | běžná | 400 ms před tělem |
| IC 1:2 | syntetická hlavička Intercopy 1200, leader 11000 | tělo Intercopy 2400, leader 5500 | hlavička běžná, tělo invertované | 345 ms před tělem |
| IC 1:3 | syntetická hlavička Intercopy 1200, leader 11000 | tělo Intercopy 2800, leader 5500 | hlavička běžná, tělo invertované | 345 ms před tělem |
| IC 1:4 | syntetická hlavička Intercopy 1200, leader 11000 | tělo Intercopy 3200, leader 5500 | hlavička běžná, tělo invertované | 345 ms před tělem |
| TC 1:2 | syntetická hlavička MZ-800 1Z-013B, leader 11000 | loader MZ-800 1Z-013B + tělo TC 1:2; oba leader 5500 | všechny etapy invertované | 0 ms před loaderem, 110 ms před tělem |
| TC 1:3 | syntetická hlavička MZ-800 1Z-013B, leader 11000 | loader MZ-800 1Z-013B + tělo TC 1:3; oba leader 5500 | všechny etapy invertované | 0 ms před loaderem, 110 ms před tělem |

Profily `UL`, `UL_MZ800` a `UL_MZ700` se do statického WAV/LEP/L16 neexportují.
Vyžadují živý WRITE/SENSE handshake a exportér skončí explicitní chybou.

Pokud je profil položky implicitní NORMAL 1:1, ale celý export je zvolen pro
MZ-700, QDTool jej při sestavení exportu přemapuje na MZ700 1:1.

## 6. Kódování dat uvnitř bloku

Po leaderu a tape mark následují:

1. datové bajty,
2. dvoubajtový checksum v pořadí high byte, low byte,
3. trailing pulzy podle tabulky výše.

Každý datový i checksumový bajt se zapisuje jako:

- 8 bitů od MSB k LSB (`SHORT = 0`, `LONG = 1`),
- za nimi jeden synchronizační `LONG` pulz.

Checksum je 16bitový součet počtu jedničkových bitů ve všech datových bajtech
(při přetečení se ponechá spodních 16 bitů).

## 7. Výstupní formáty a kvantování

| Formát | Reprezentace |
|---|---|
| WAV | PCM, mono, 8 bitů, **44 100 Hz**; vzorky HIGH = 80, LOW = 176 |
| LEP | signed edge duration po 50 µs; HIGH kladně, LOW záporně |
| L16 | signed edge duration po 16 µs; HIGH kladně, LOW záporně |

WAV, LEP i L16 používají průběžný akumulátor kvantizační chyby. Jednotlivý
interval je zaokrouhlen na nejbližší počet vzorků/jednotek, ale zbytek se přenese
do dalšího intervalu. Proto se přesné desetinné délky musí používat jako zdrojové
hodnoty a nesmějí se předem zaokrouhlit na celé vzorky.

Pauza mezi etapami se zapisuje po 1 ms intervalech. Její úroveň odpovídá klidové
úrovni dané polarity: u běžné polarity LOW, u invertované polarity HIGH.

## 8. Autoritativní místa v kódu

- Profily, mapování režimů, počty leaderů a sestavení etap:
  `SharpTapeProfileEncoder.cs`
- Kontextové LOW délky MZ-800, rámování, polarita a výstupní kvantování:
  `SharpTapeExporter.cs`
- Povolené kombinace loaderu a rychlosti:
  `TapeModel.cs`

Při budoucí změně těchto souborů je nutné tento dokument znovu synchronizovat;
nejde o konfigurační vstup programu, ale o přesný popis současné implementace.
