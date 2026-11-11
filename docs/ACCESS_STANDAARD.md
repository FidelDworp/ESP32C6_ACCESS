# ACCESS-standaard (Zarlar buitenverlichting)

*v0.4 — 11 nov 2026 — Filip Delannoy (FiDel). Status: **vastgelegd** (zones, `/json`-keys, gedrag). Wijzigen alleen met akkoord van FiDel. Sketch: **v0.1** (basis), voor het testbord.*

Alle buitenverlichting van Zarlardinge draait op **dezelfde hardware en dezelfde sketch** `ESP32_C6_ACCESS`, op minstens drie controllers: ACCESS1 (.82), ACCESS2 (.83) en ACCESS3 (.84). Wat per controller verschilt (zones, namen, luxsensor), is een **instelling** in `/settings` (NVS), nooit een aparte sketch. Zelfde aanpak als de [ROOM-standaard](https://github.com/FidelDworp/ESP32C6_ROOMS/blob/main/docs/ROOM_STANDAARD.md).

---

## 1. Hardware

| Onderdeel | Standaard |
|---|---|
| Controller | ESP32-C6-DevKitM-1 (16 MB flash) op het **Zarlar shield** |
| Zones | **maximaal 4 per controller**, elk een PIR-ingang en een PWM-uitgang (§1.1) |
| Lampen | per zone **LED-drivers van 300 mA** (constante stroom) met PWM-stuuringang. Per driver twee lampen **12 V / 3 W in serie op 24 V** |
| Luxsensor | **TSL2561** op de I2C-bus (IO13 SDA / IO11 SCL, 3,3 V, adres 0x39), op **één** controller: waar hij het daglicht het best ziet (normaal ACCESS1). Per controller aan/uit in `/settings` |
| Voeding PIR's | HC-SR501 op 5 V (uitgang 3,3 V): shield-jumper Sense pin 8 op **5 V** |

**Gouden regel: een ESP32-pin ziet nooit meer dan 3,3 V.** Elke signaalader meten vóór aansluiten.

### 1.1 Vaste plaatsen per zone ("zoneslots")

Elke controller heeft dezelfde 4 zoneslots op vaste pinnen. Een controller gebruikt alleen de slots die hij nodig heeft. Zo is de hardware van ACCESS1, 2 en 3 identiek.

| Slot | PIR-ingang | PWM-uitgang | ACCESS1 (vandaag op R8: PIR / PWM) |
|---|---|---|---|
| 1 | IO3 (Sense 1) | IO19 (Option 5) | **D** Voordeur (MOV-W1 / PWM-W1) |
| 2 | IO5 (Sense 2) | IO18 (Option 4) | **W4** Veldkant (MOV-W2 / PWM-W2) |
| 3 | IO2 (Option 3) | IO10 (Option 2) | **S** Terras (MOV-S / PWM-S) |
| 4 | IO12 (Sense 4) | IO1 (Sense 6) | vrij |

### 1.1.1 Zones en lampgroepen (beslist 8/11)

De lampgroepen zijn die van de plattegrond op FiDels tekening *ACCESS-WASPL bedrading* (telkens een paar lampen op één module). Een **zone** = één PWM-uitgang + de PIR('s) ervoor; ze kan meerdere lampgroepen sturen. **De zonecode is overal de enige naam** van die zone: `/settings`, `/json`, bediening, Dashboard, RPi.

| Controller | Zone | Lampgroepen | Muur | Toestand 8/11 |
|---|---|---|---|---|
| ACCESS1 (.82) | `S` | S1 + S2 | S (tuinzijde) | werkt (R8) |
| ACCESS1 | `W4` | W4 | W (veldzijde) | werkt (R8) |
| ACCESS1 | `D` (voordeur) | W2 + DN, DS + W3 | W | werkt (R8) |
| ACCESS2 (.83) | `W1` | W1 | W | nog niet in gebruik |
| ACCESS2 | `N1` | N1 | N | nog niet in gebruik |
| ACCESS3 (.84) | `N2` | N2 | N | nog niet in gebruik |
| ACCESS3 | `E` | E1 + E2 | E | nog niet in gebruik |

ACCESS2 en ACCESS3 staan in de B&B; W1 en N1 zitten in de hoek waar ACCESS2 komt. Windrichtingen in het Engels, zoals op de tekening: **W, S, N, E**.

- PIR-ingangen: **1 kΩ in serie**, verder niets. IO3 heeft op het shield een 4,7 kΩ pull-up (T-BUS): daar zeker niet meer dan 1 kΩ.
- IO1 heeft op het shield een 10 kΩ pull-up naar 3V3: bij "0 V = aan" blijft dat licht uit tijdens het opstarten.
- Vrij: IO4 (Option 6, eventueel statuspixel), IO6, IO7. **Niet** gebruikt voor iets anders zonder dit document aan te passen.

### 1.1.2 Aansluiten op de Optionix-plaatjes (11/11)

Elke connector krijgt een **Optionix-plaatje** met 8 soldeercontacten. Zelfde verdeling op elke ACCESS (volgt de zoneslots):

| Pin | **RoomSense** | **Option** |
|---|---|---|
| 1 | IO3 · **PIR slot 1** | +5 V |
| 2 | IO5 · **PIR slot 2** | IO10 · **PWM slot 3** |
| 3 | IO6 · vrij | IO2 · **PIR slot 3** |
| 4 | IO12 · **PIR slot 4** | IO18 · **PWM slot 2** |
| 5 | IO7 · vrij | IO19 · **PWM slot 1** |
| 6 | IO1 · **PWM slot 4** | IO4 · vrij (statuspixel) |
| 7 | GND | GND |
| 8 | voeding sensoren (shield-jumper: 5 V voor HC-SR501, 3V3 voor AM312) | 3V3 |

**Testbord:** de LED-driver op **slot 1**: stuuringang op **Option 5 (IO19)**, massa op **Option 7 (GND)**; een PIR op **RoomSense 1 (IO3)**, voeding RoomSense 8, massa RoomSense 7, 1 kΩ in serie op de signaalader.
- **Gemeenschappelijke massa:** de min van de 24 V-voeding moet aan de GND van de ESP32, tenzij de stuuringang van de driver galvanisch gescheiden is (nakijken op de driver).

### 1.2 Stuuringang van de LED-drivers: 3,3 V

- De Photon R8-ACCESS stuurt dezelfde drivers vandaag met **3,3 V**, en dat werkt. **Beslist:** eerst zonder buffer.
- **Te testen** met de eerste controller: gaat het licht volledig aan én volledig uit, en dimt het netjes over het hele bereik?
- Alleen als dat niet lukt: per uitgang een **buffer 3,3 → 5 V** (74AHCT1G125, of één 74HCT125 voor 4 uitgangen) met 10 kΩ pull-up op de ingang, zodat de lichten uit blijven tijdens het opstarten. Schema: [`ESP32C6_ROOMS/docs/aansluitingen.md` §5](https://github.com/FidelDworp/ESP32C6_ROOMS/blob/main/docs/aansluitingen.md).
- Polariteit: vandaag **0 V = licht aan** (uitgangen actief laag). Per zone omkeerbaar in `/settings`.

## 2. Gedrag

### 2.1 Per zone

| Modus | Wat |
|---|---|
| **AUTO** (standaard) | **Beslist: nacht én beweging** (zoals de ROOM-sketch: donker én beweging). Licht aan op de ingestelde helderheid, uit na de brandtijd sinds de laatste beweging. Overdag gebeurt er niets |
| **MANUEEL** (override) | **Beslist 8/11:** een zone manueel aan, dag of nacht. **Eén modus voor de hele controller** (`au`), zoals R8. Na de override-tijd (standaard **30 min**, instelbaar) of bij manueel uit: terug naar AUTO. Lampen die door een PIR aan gingen, gaan ook in MANUEEL uit na hun brandtijd |

Per zone in `/settings`: zonecode · muur (W, S, N, E) · aan/uit · helderheid (%) · brandtijd (standaard 30 s, zoals R8) · polariteit. Per controller: override-tijd. Geen meeschakelen tussen zones (de koppeling W1 → W2 van R8 vervalt).

### 2.2 Dag of nacht

1. **Eigen luxsensor** (als die op deze controller zit): nacht als lux ≤ drempel-nacht (standaard 5 lux), dag als lux ≥ drempel-dag (standaard 10 lux). De hysterese voorkomt geflikker bij schemering (waarden van de Photon S-OUTSIDE).
2. **Lux van een andere ACCESS:** de controllers zonder sensor lezen de lux uit `/json` van de controller met de sensor (instelling: naam of IP, bv. `access1.local`).
3. **Terugval: berekende zonsopgang en zonsondergang** uit de datum en de ligging van Zarlardinge (instelbaar, met een marge in minuten). Gebruikt als de luxbron langer dan 10 minuten niet antwoordt. Zo blijft elke controller zelfstandig werken.

De bron die in gebruik is, staat in de UI en in `/json`.

**Let op:** de TSL2561 verzadigt boven ±40.000 lux (felle zon). Voor dag/nacht maakt dat niet uit; voor andere toepassingen wel.

## 3. Software

- **Eén sketch** `ESP32_C6_ACCESS`, zelfde basis als de andere ESP32-sketches: vast IP, mDNS (`access1.local` …), `/settings`, `/update` (OTA), Diagnose (herstartreden, geheugen, WiFi), geen Matter.
- **Apple Home** via de RPi-bridge (`server.js`), zoals nu voor R8-ACCESS.
- **Dashboard:** rij 3 (de 4 muren) leest later `/json` van ACCESS1–3 in plaats van de Photon, met de muurletters **W·S·N·E** (nu W·Z·N·O). Eigen Dashboard-versie, eigen GO.
- **De lux in `/json`**, zodat ook het Dashboard, de ECO en anderen hem kunnen lezen. Daarmee is een aparte OUTSIDE-controller (.72) waarschijnlijk overbodig.
- **`/json` is een contract.** Keys en bedieningsadressen worden vastgelegd vóór v0.2 en daarna niet meer gewijzigd zonder akkoord. Een controller stuurt alleen de keys van zijn eigen zones.

### 3.1 `/json`-keys en bediening (beslist 8/11)

Per zone: **`m` + zonecode** en **`l` + zonecode**, in kleine letters.

| Key | Betekenis |
|---|---|
| `a` | uptime (s) |
| `m<zone>` | beweging in de laatste 10 min (1/0): `ms`, `mw4`, `md` (ACCESS1) · `mw1`, `mn1` (ACCESS2) · `mn2`, `me` (ACCESS3) |
| `l<zone>` | licht nu, helderheid 0–100 %: `ls`, `lw4`, `ld` · `lw1`, `ln1` · `ln2`, `le` |
| `au` | modus van de controller: 1 = AUTO, 0 = MANUEEL |
| `lux` | lux (−1 = geen bron) |
| `nt` | nacht (1/0) |
| `lb` | bron dag/nacht: 0 = eigen sensor, 1 = andere ACCESS, 2 = berekend, −1 = nog geen bron (v0.1–v0.2) |
| `ac`, `ad`, `ae`, `af` | RSSI, heap %, grootste blok, laagste heap (zoals ROOM) |

Voorbeeld ACCESS1: `{"a":3600,"ms":0,"ls":0,"mw4":0,"lw4":0,"md":1,"ld":80,"au":1,"lux":2,"nt":1,"lb":0,…}`

Bediening: `/set_zone?z=<zone>&v=1` (manueel aan, controller naar MANUEEL), `v=0` (uit, terug naar AUTO), optioneel `&b=0..100` (helderheid). Bv. `/set_zone?z=D&v=1&b=60`.

Alleen om te testen, **geen contract**: `/set_test?z=1..4&b=0..100` zet een zoneslot tijdelijk op een helderheid (niet bewaard; sinds v0.1).

**Overgang van R8:** de Photon gebruikt `mw1`/`lw1` voor de voordeur en `mw2`/`lw2` voor de veldkant. Die keys worden **niet** behouden (`lw1` betekent straks lampgroep W1 op ACCESS2). Het Dashboard en `server.js` gaan om op het moment dat ACCESS1 de Photon vervangt (v0.4).

## 4. Niet in de standaard

- Geen andere sensoren (temperatuur, CO2…), geen pixels, geen relais. AlarmiX (deuren, sloten, bel) blijft in de planning, buiten deze standaard.
- Spelregels zoals bij de rooms: iets nieuws alleen als het in de standaard past; eerst dit document aanpassen, dan hardware en code; experimenten in `ESP32C6_Experiments`.

## 5. Stappenplan

Telkens één versie, na GO, met versiekop, changelog en een 48 u-test.

| Versie | Wat |
|---|---|
| v0.1 | **Basis:** WiFi, OTA, mDNS, `/settings`, `/json`, Diagnose (hergebruikt uit ROOM). Op het testbord (§6.2) |
| v0.2 | **Zones:** PIR, brandtijd, helderheid (PWM), AUTO/MANUEEL met override-tijd, `/set_zone`. Nog zonder dag/nacht |
| v0.3 | **Dag/nacht:** TSL2561, lux delen tussen de controllers, berekende terugval |
| v0.4 | **Plaatsen als ACCESS1.** R8-ACCESS uit; `server.js` en Dashboard (rij 3) naar ACCESS1. Daarna ACCESS2 en ACCESS3 in de B&B |

## 6. Beslissingen (FiDel)

1. **Zones en controllers** (8/11, vervangt de N1-beslissing van 7/11): zie §1.1.1. ACCESS1 = S, W4, D · ACCESS2 = W1, N1 · ACCESS3 = N2, E.
2. **Testbord** voor v0.1–v0.3: een losse ESP32-C6 op een shield, met **één LED-driver en twee lampen 9,5 V / 300 mA in serie op 24 V**, aangesloten op slot 1 (§1.1.2). In de maak (11/11).
3. **PWM-frequentie** van de drivers: **start op 1 kHz**, daarna testen op geflikker en lawaai; bijstellen indien nodig.

4. **`/json`-keys** (8/11): zonecode als naam, licht in %, één modus per controller (§3.1).
5. **Manuele override** max. 30 min (instelbaar); brandtijd instelbaar (8/11).

## 6.1 Afwijkingen in R8-ACCESS (Photon), opgelost door de nieuwe sketch

- Beweging aan de voordeur schakelde ook de veldkant (W1 → W2, "tijdelijk"): vervalt.
- Geen dag/nacht: de lampen gingen ook overdag aan.
- Geen dimmen: alles aan/uit, ook op PWM-pinnen.
- Na een manueel commando bleven lampen die door een PIR brandden, aan tot iemand terug naar AUTO schakelde.
- Brandtijd 30 s vast in de code.
- `lw2` bleef 0 bij beweging aan de voordeur, terwijl de code W2 mee aanzette: op het testbord de uitgangen apart controleren.

**Nog na te kijken op de tekening:** de nieuwe PIR's MOV-W1, MOV-W3 en MOV-DN gaan via het PIReX-printje samen naar één ingang. Met de nieuwe zones horen W3 en DN bij `D` (ACCESS1) en W1 bij ACCESS2.

## 7. Historiek

| Versie | Datum | Wat |
|---|---|---|
| v0.4 | 11 nov 2026 | Aansluitingen op de Optionix-plaatjes en testbord (§1.1.2), `lb` −1 en testadres `/set_test`; sketch v0.1 |
| v0.3 | 8 nov 2026 | Zones per controller en zonecodes (S, W4, D · W1, N1 · N2, E), `/json`-keys vastgelegd, één modus per controller met override-tijd, afwijkingen R8 |
| v0.2 | 7 nov 2026 | Open vragen beantwoord: N1 blijft bij ACCESS1, testbord met één driver + LED, PWM start op 1 kHz |
| v0.1 | 7 nov 2026 | Eerste voorstel: zoneslots, LED-drivers 300 mA op 3,3 V, TSL2561 op één controller met delen en berekende terugval, AUTO alleen 's nachts, MANUEEL-override, stappenplan |
