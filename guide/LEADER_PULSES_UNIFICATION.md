# SD2CMT2 — Leader Pulses Unification
## Zadání pro člověka i AI/Codex

**Projekt:** `bales0/MZ-SD2CMT2`  
**Oblast:** MZF/M12/MZT CMT generátor, Sharp MZ-700 / MZ-800  
**Téma:** sjednocení počtu SHORT pulzů v zaváděcích (leader/GAP) tónech  
**TAP:** mimo rozsah tohoto zadání

---

# 1. Cíl

Sjednotit počet leader pulzů v MZF generátoru tak, aby:

- počet pulzů vycházel z jasného pravidla formátu,
- nebyl odvozován z délky WAVu nebo z požadované časové délky leaderu,
- nebyl škálován podle rychlosti přenosu,
- byl čitelný a předvídatelný pro člověka i AI,
- zachoval spolehlivost na reálném MZ hardware,
- oddělil **věrnou reprodukci nahrávky** od **kanonického MZF generátoru**.

Základní filozofie:

```text
WAV / LEP playback
    = věrná reprodukce konkrétní nahrávky

MZF playback
    = kanonický, deterministický a spolehlivý CMT generátor
```

Proto není cílem MZF generátoru napodobovat přesnou délku leaderu konkrétního
PC emulátoru, WAV převodu nebo konkrétní kopírky.

---

# 2. Základní pravidlo

## 2.1 Native / NORMAL formát

Pro běžný Sharp MZ CMT formát používat plné standardní počty:

```text
HEADER leader = 22000 SHORT pulzů
DATA   leader = 11000 SHORT pulzů
```

Tyto hodnoty odpovídají standardnímu Sharp framingu z monitor ROM.

Platí pro native NORMAL přenos.

Počet pulzů se **nemění podle rychlosti**. Pokud existuje rychlejší NORMAL
profil, zrychlení vzniká pouze změnou délky SHORT/LONG pulzu.

---

## 2.2 SD2CMT2 special loader formát

Pro všechny speciální loadery SD2CMT2 používat jednotné poloviční framing
počty:

```text
LOADER HEADER leader = 11000 SHORT pulzů
LOADER DATA   leader =  5500 SHORT pulzů
TURBO PWM DATA leader = 5500 SHORT pulzů
```

Pokud loader nemá samostatný loader DATA blok, položka `5500` se nepoužije.

Pokud po loaderu následuje handshake přenos, handshake payload **nemá CMT
SHORT/LONG leader**.

---

# 3. Důležité pravidlo: leader count není čas

Počet leader pulzů je parametr framingu.

Čas leaderu je pouze důsledek:

```text
leader_time = leader_pulse_count × SHORT_period
```

Proto se počet pulzů nesmí zvětšovat pouze proto, že se SHORT pulz zkrátil.

Zakázaný princip:

```text
rychlejší režim
→ kratší SHORT
→ automaticky přidat více leader pulzů
→ udržovat podobnou dobu leaderu
```

Požadovaný princip:

```text
formát určí počet pulzů
+
profil určí délku SHORT
=
výsledný čas leaderu
```

---

# 4. Kanonická tabulka

| Režim | HEADER leader | Loader DATA leader | Turbo/PWM DATA leader | Následující payload |
|---|---:|---:|---:|---|
| MZ-800 NORMAL | **22000 S** | — | **11000 S** | NORMAL PWM |
| MZ-700 NORMAL | **22000 S** | — | **11000 S** | NORMAL PWM |
| IC 1:2 | **11000 S** | — | **5500 S** | IC turbo PWM |
| IC 1:3 | **11000 S** | — | **5500 S** | IC turbo PWM |
| IC 1:4 | **11000 S** | — | **5500 S** | IC turbo PWM |
| MZ-700 FAST3 LOW | **11000 S** | — | **5500 S** | FAST3 PWM |
| MZ-700 FAST3 HIGH | **11000 S** | — | **5500 S** | FAST3 PWM |
| TC 1:2 | **11000 S** | **5500 S** | **5500 S** | TC turbo PWM |
| TC 1:3 | **11000 S** | **5500 S** | **5500 S** | TC turbo PWM |
| MZ-800 UL classic LOW | **11000 S** | **5500 S** | — | handshake |
| MZ-800 UL classic HIGH | **11000 S** | **5500 S** | — | handshake |
| MZ-800 UL header-only | **11000 S** | — | — | handshake |
| MZ-700 UL LOW | **11000 S** | — | — | handshake |
| MZ-700 UL HIGH | **11000 S** | — | — | handshake |

`S` = jeden kompletní SHORT pulz.

---

# 5. Význam jednotlivých skupin

## 5.1 NORMAL

Native NORMAL musí používat:

```text
22000 / 11000
```

To je referenční Sharp struktura.

Nesmí se zachovat historické hodnoty typu:

```text
6344 / 6344
```

jen proto, že odpovídaly konkrétnímu syntetickému WAVu.

Pokud je potřeba přesně přehrát konkrétní nahrávku, použije se WAV nebo LEP,
nikoliv MZF generátor.

---

## 5.2 Intercopy (IC)

IC je zvlášť důležitý, protože:

```text
HEADER = 11000
DATA   =  5500
```

je současně:

- odvozeno z Intercopy chování,
- historicky použito v projektu,
- ověřeno na skutečném hardware.

Pokus:

```text
5500 -> 2750
```

vedl k problémům při zachycení datového leaderu.

Na reálném stroji byly pozorovány synchronizované border loading pruhy,
ale leader trval neobvykle krátce a data se nenačetla.

Proto:

```text
IC turbo DATA leader = 5500
```

je minimálně **HW-PROVEN + LOADER-DERIVED** projektová reference.

Nesnižovat bez explicitního nového hardware testu.

---

## 5.3 MZ-700 FAST3

FAST3 je speciální MZ-700 loader.

Použít:

```text
HEADER = 11000
FAST3 DATA = 5500
```

FAST3 header se čte ještě normálním monitorovým CMT příjmem.

Vlastní FAST3 timing se aktivuje až pro turbo payload.

Zachovat existující:

```text
400 ms LOW inter-stage delay
```

Tato prodleva není leader a nesmí se s leader pulzy slučovat.

---

## 5.4 TurboCopy (TC)

TC má tři části:

```text
1. loader HEADER
2. loader BODY
3. TC turbo payload
```

Požadované sjednocení:

```text
TC HEADER        = 11000 S
TC loader BODY   =  5500 S
TC turbo payload =  5500 S
```

Toto je záměrná **SD2CMT2 canonical/optimized framing policy**.

Není nutné tvrdit, že `5500` je přesná historická hodnota každé TurboCopy
nahrávky.

Přesnou emulaci konkrétní pásky zajišťují WAV/LEP režimy.

Odstranit logiku, kde TC 1:2 a TC 1:3 mají rozdílný počet leader pulzů pouze
kvůli rozdílné rychlosti pulzů.

Tedy zrušit koncept typu:

```text
TC 1:2 -> 11239
TC 1:3 -> 15130
```

a nahradit:

```text
TC 1:2 -> 5500
TC 1:3 -> 5500
```

pro turbo payload.

---

## 5.5 UL — classic loader

Classic UL obsahuje CMT část a potom handshake.

Požadovaná struktura:

```text
11000 S
LTM
loader HEADER

5500 S
STM
loader BODY

HANDSHAKE
user payload
```

Po spuštění UL receiveru se CMT PWM již nepoužívá.

Handshake payload tedy:

```text
nemá leader
nemá SHORT/LONG PWM framing
```

---

## 5.6 UL — header-only

Header-only UL má celý receiver připravený v headeru.

Struktura:

```text
11000 S
LTM
HEADER s loaderem

HANDSHAKE
user payload
```

Žádný druhý `5500` leader se nevkládá.

---

# 6. Co se NESMÍ změnit v rámci tohoto zadání

Leader Pulses Unification **není** audit všech CMT parametrů.

Bez samostatného důvodu neměnit:

- délky SHORT/LONG pulzů,
- HIGH/LOW poměr pulzu,
- polaritu signálu,
- LOW-first / HIGH-first chování,
- TC invertování,
- tapemark počty:
  - LTM 40 LONG + 40 SHORT,
  - STM 20 LONG + 20 SHORT,
- final 2 LONG,
- TC trailing 98 SHORT,
- checksum algoritmus,
- pořadí bitů,
- MOTOR chování,
- boundary auto-continue,
- IC start delay,
- TC start delay,
- FAST3 400 ms LOW delay,
- UL handshake timing,
- TAP playback,
- WAV playback,
- LEP/L16 playback.

Toto zadání má měnit **pouze počet SHORT pulzů v leader/GAP sekcích MZF
generátoru** a kód, který jejich použití vypočítává.

---

# 7. Doporučená implementace

## 7.1 Centrální konstanty

Preferovat několik sémantických konstant místo rychlostně specifických
čísel:

```cpp
#define MZF_NATIVE_HEADER_LEADER_SHORT_PULSES 22000U
#define MZF_NATIVE_DATA_LEADER_SHORT_PULSES   11000U

#define MZF_LOADER_HEADER_LEADER_SHORT_PULSES 11000U
#define MZF_LOADER_DATA_LEADER_SHORT_PULSES    5500U
#define MZF_LOADER_TURBO_LEADER_SHORT_PULSES   5500U
```

Je možné `LOADER_DATA` a `LOADER_TURBO` sloučit na jednu konstantu, pokud
zůstane význam v komentáři jednoznačný.

Například:

```cpp
#define MZF_SPECIAL_DATA_LEADER_SHORT_PULSES 5500U
```

---

## 7.2 Zakázané konstanty

Po změně by neměly být potřeba rychlostně odvozené leader constants typu:

```cpp
MZF_TC_1_2_TURBO_GAP_SHORT_PULSES
MZF_TC_1_3_TURBO_GAP_SHORT_PULSES

MZF_NORMAL_1_2_LONG_GAP_SHORT_PULSES
MZF_NORMAL_1_2_SHORT_GAP_SHORT_PULSES
MZF_NORMAL_1_3_LONG_GAP_SHORT_PULSES
MZF_NORMAL_1_3_SHORT_GAP_SHORT_PULSES
```

pokud jejich jediným účelem je kompenzace časové délky leaderu.

Rychlost nesmí určovat počet pulzů.

---

# 8. Doporučená rozhodovací logika

Leader count musí být určen podle:

```text
stage + loader type
```

nikoliv podle:

```text
pulse duration / speed divisor
```

Pseudokód:

```cpp
uint16_t mzf_gap_short_pulses()
{
    if (native_normal)
    {
        if (stage == HEADER)
            return 22000;
        return 11000;
    }

    if (special_loader)
    {
        if (stage == HEADER)
            return 11000;

        if (stage == DATA)
            return 5500;

        if (stage == TAPE_TURBO_DATA)
            return 5500;
    }

    return appropriate_explicit_value;
}
```

Pro header-only UL:

```text
HEADER -> 11000
potom handshake
```

Pro TC:

```text
HEADER          -> 11000
DATA loader     -> 5500
TAPE_TURBO_DATA -> 5500
```

---

# 9. Duration calculation

Výpočet celkové délky playbacku musí používat **stejnou leader decision
funkci nebo stejnou centrální tabulku jako samotný generátor**.

Nesmí existovat dvě nezávislé implementace:

```text
generator leader count
duration estimator leader count
```

s rozdílnými hodnotami.

Po změně ověřit:

```text
estimated duration == generated duration
```

v rámci existující přesnosti počítadla.

---

# 10. Testy, které je nutné aktualizovat

Statické/automatické testy mají explicitně ověřit:

```text
NORMAL header = 22000
NORMAL data   = 11000

IC header     = 11000
IC turbo data = 5500

FAST3 header  = 11000
FAST3 data    = 5500

TC header      = 11000
TC loader body = 5500
TC turbo data  = 5500

UL classic header      = 11000
UL classic loader body = 5500
UL handshake           = no PWM leader

UL header-only header = 11000
UL header-only        = no second PWM leader
```

Dále testovat, že:

```text
TC 1:2 leader count == TC 1:3 leader count
IC 1:2 leader count == IC 1:3 leader count == IC 1:4 leader count
```

a že změna speed profile nemění leader count.

---

# 11. Hardware regression test

Po implementaci otestovat minimálně:

```text
MZ-800 NORMAL
MZ-700 NORMAL
IC 1:2
IC 1:3
IC 1:4
TC 1:2
TC 1:3
MZ-700 FAST3
MZ-800 UL classic
MZ-800 UL header-only
MZ-700 UL
```

Zvláštní pozornost:

```text
IC turbo data leader = 5500
```

U IC sledovat border loading pruhy.

Správný průběh:

```text
synchronizované leader pruhy
→ dostatečně dlouhá stabilní fáze
→ tapemark
→ data
```

Podezřelý průběh:

```text
leader pruhy pouze krátce probliknou
→ data nezačnou / LOAD ERROR
```

V takovém případě nejprve ověřit leader count a stage timing, až potom
podezírat polaritu.

---

# 12. Evidence classification

Budoucí dokumentace/AI musí rozlišovat původ hodnot:

```text
NORMAL 22000/11000
    ROM-DERIVED / SHARP STANDARD

IC 11000/5500
    LOADER-DERIVED + HW-PROVEN

FAST3 11000/5500
    SD2CMT2 CANONICAL + HW-COMPATIBLE

TC 11000/5500/5500
    SD2CMT2 CANONICAL / OPTIMIZED FRAMING

UL 11000/5500 before handshake
    SD2CMT2 CANONICAL / OPTIMIZED FRAMING
```

Nesmí se tvrdit, že všechny hodnoty jsou přesnou historickou reprodukcí
konkrétní kopírky.

---

# 13. AI/Codex maintenance contract

Při budoucí změně leaderů musí AI:

1. určit, zda jde o native NORMAL nebo special loader;
2. určit stage:
   - HEADER,
   - DATA,
   - TAPE_TURBO_DATA,
   - HANDSHAKE;
3. nikdy neodvozovat počet pulzů z rychlosti;
4. nikdy neodvozovat počet pulzů z WAV délky bez explicitního požadavku;
5. zachovat `22000/11000` pro native NORMAL;
6. zachovat `11000/5500` pro special loader CMT části;
7. nevkládat PWM leader před handshake payload;
8. neměnit polarity a pulse widths jako vedlejší efekt;
9. použít stejné hodnoty v generátoru i duration kalkulaci;
10. aktualizovat automatické testy;
11. upozornit, pokud navrhovaná změna ruší HW-proven IC `5500`;
12. před změnou HW-proven hodnot požadovat reálný hardware regression test.

---

# 14. Machine-readable policy

```yaml
schema: sd2cmt2-leader-pulses-unification-v1

native_normal:
  header_short_pulses: 22000
  data_short_pulses: 11000
  scale_count_with_speed: false

special_loader:
  header_short_pulses: 11000
  data_short_pulses: 5500
  turbo_pwm_short_pulses: 5500
  scale_count_with_speed: false

profiles:
  mz800_normal:
    header: 22000
    data: 11000

  mz700_normal:
    header: 22000
    data: 11000

  ic_1_2:
    header: 11000
    turbo_data: 5500

  ic_1_3:
    header: 11000
    turbo_data: 5500

  ic_1_4:
    header: 11000
    turbo_data: 5500

  mz700_fast3_low:
    header: 11000
    turbo_data: 5500
    interstage_low_ms: 400

  mz700_fast3_high:
    header: 11000
    turbo_data: 5500
    interstage_low_ms: 400

  tc_1_2:
    header: 11000
    loader_body: 5500
    turbo_data: 5500

  tc_1_3:
    header: 11000
    loader_body: 5500
    turbo_data: 5500

  mz800_ul_classic_low:
    header: 11000
    loader_body: 5500
    payload: handshake
    payload_pwm_leader: none

  mz800_ul_classic_high:
    header: 11000
    loader_body: 5500
    payload: handshake
    payload_pwm_leader: none

  mz800_ul_header_only:
    header: 11000
    payload: handshake
    second_pwm_leader: none

  mz700_ul_low:
    header: 11000
    payload: handshake
    second_pwm_leader: none

  mz700_ul_high:
    header: 11000
    payload: handshake
    second_pwm_leader: none

forbidden:
  derive_count_from_speed: true
  preserve_wav_duration_by_increasing_count: true
  add_pwm_leader_before_handshake_payload: true
```

---

# 15. Definition of Done

Zadání je splněno, pokud:

```text
[ ] native NORMAL používá 22000/11000
[ ] všechny special loader headery používají 11000
[ ] každý special loader DATA/PWM stage, pokud existuje, používá 5500
[ ] TC 1:2 a TC 1:3 mají stejné leader counts
[ ] IC 1:2, 1:3, 1:4 mají stejné leader counts
[ ] FAST3 používá 11000/5500
[ ] UL handshake payload nemá PWM leader
[ ] duration kalkulace používá stejné hodnoty jako generátor
[ ] staré speed-specific leader constants jsou odstraněny nebo nepoužívány
[ ] testy explicitně hlídají novou politiku
[ ] WAV/LEP playback není změněn
[ ] pulse widths/polarity/tapemarks nejsou tímto patchem změněny
[ ] hardware regression projde
```

---

# 16. Krátké shrnutí

Cílová politika SD2CMT2:

```text
NATIVE NORMAL
    HEADER 22000
    DATA   11000

SPECIAL LOADER CMT
    HEADER 11000
    DATA    5500
    TURBO   5500

HANDSHAKE
    no PWM leader
```

Počet pulzů je framing parametr.

Rychlost přenosu mění délku pulzu, nikoliv počet leader pulzů.

Přesnou reprodukci historického nebo emulátorového záznamu řeší WAV/LEP.
MZF generátor má používat jednotný, čitelný a hardware-spolehlivý kanonický
framing.
