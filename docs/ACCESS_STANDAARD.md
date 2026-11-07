# ACCESS-standaard (Zarlar buitenverlichting)

*v0.2 — 7 nov 2026 — Filip Delannoy (FiDel). Status: **voorstel**, te bevestigen (open vragen §6 beantwoord, `/json`-keys nog niet). Wat al beslist is, staat er als "beslist".*

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

| Slot | PIR-ingang | PWM-uitgang | ACCESS1 vandaag (klemmen R8) |
|---|---|---|---|
| 1 | IO3 (Sense 1) | IO19 (Option 5) | **W1** Voordeur (lampen W1 + D) |
| 2 | IO5 (Sense 2) | IO18 (Option 4) | **W2** Veldkant (lampen W3 + W4) |
| 3 | IO2 (Option 3) | IO10 (Option 2) | **S** Terras (lampen S1 + S2) |
| 4 | IO12 (Sense 4) | IO1 (Sense 6) | **N1** (lampen N1 + N2), tussen de voordeur (W-zijde) en de N-muur: **blijft bij ACCESS1** (beslist 7/11, werkt goed). Ander stuk muur dan de noordgevel bij de B&B (ACCESS2) |

- PIR-ingangen: **1 kΩ in serie**, verder niets. IO3 heeft op het shield een 4,7 kΩ pull-up (T-BUS): daar zeker niet meer dan 1 kΩ.
- IO1 heeft op het shield een 10 kΩ pull-up naar 3V3: bij "0 V = aan" blijft dat licht uit tijdens het opstarten.
- Vrij: IO4 (Option 6, eventueel statuspixel), IO6, IO7. **Niet** gebruikt voor iets anders zonder dit document aan te passen.

### 1.2 Stuuringang van de LED-drivers: 3,3 V

- De Photon R8-ACCESS stuurt dezelfde drivers vandaag met **3,3 V**, en dat werkt. **Beslist:** eerst zonder buffer.
- **Te testen** met de eerste controller: gaat het licht volledig aan én volledig uit, en dimt het netjes over het hele bereik?
- Alleen als dat niet lukt: per uitgang een **buffer 3,3 → 5 V** (74AHCT1G125, of één 74HCT125 voor 4 uitgangen) met 10 kΩ pull-up op de ingang, zodat de lichten uit blijven tijdens het opstarten. Schema: [`ESP32C6_ROOMS/docs/aansluitingen.md` §5](https://github.com/FidelDworp/ESP32C6_ROOMS/blob/main/docs/aansluitingen.md).
- Polariteit: vandaag **0 V = licht aan** (uitgangen actief laag). Per zone omkeerbaar in `/settings`.

## 2. Gedrag

### 2.1 Per zone

| Modus | Wat |
|---|---|
| **AUTO** (standaard) | **Beslist: alleen 's nachts.** Beweging → licht aan op de ingestelde helderheid, uit na de brandtijd sinds de laatste beweging (standaard 30 s, zoals R8). Overdag gebeurt er niets |
| **MANUEEL AAN** (override) | **Beslist:** zoals in de ROOM-sketch. Licht aan, dag of nacht, tot je het manueel uitzet; dan terug naar AUTO |

Per zone in `/settings`: naam · muur (W, Z, N, O) · aan/uit · helderheid (%) · brandtijd · polariteit · **meeschakelen met een andere zone** (vandaag schakelt W1 ook W2 mee, zie open punten).

### 2.2 Dag of nacht

1. **Eigen luxsensor** (als die op deze controller zit): nacht als lux ≤ drempel-nacht (standaard 5 lux), dag als lux ≥ drempel-dag (standaard 10 lux). De hysterese voorkomt geflikker bij schemering (waarden van de Photon S-OUTSIDE).
2. **Lux van een andere ACCESS:** de controllers zonder sensor lezen de lux uit `/json` van de controller met de sensor (instelling: naam of IP, bv. `access1.local`).
3. **Terugval: berekende zonsopgang en zonsondergang** uit de datum en de ligging van Zarlardinge (instelbaar, met een marge in minuten). Gebruikt als de luxbron langer dan 10 minuten niet antwoordt. Zo blijft elke controller zelfstandig werken.

De bron die in gebruik is, staat in de UI en in `/json`.

**Let op:** de TSL2561 verzadigt boven ±40.000 lux (felle zon). Voor dag/nacht maakt dat niet uit; voor andere toepassingen wel.

## 3. Software

- **Eén sketch** `ESP32_C6_ACCESS`, zelfde basis als de andere ESP32-sketches: vast IP, mDNS (`access1.local` …), `/settings`, `/update` (OTA), Diagnose (herstartreden, geheugen, WiFi), geen Matter.
- **Apple Home** via de RPi-bridge (`server.js`), zoals nu voor R8-ACCESS.
- **Dashboard:** rij 3 (W·Z·N·O) leest later `/json` van ACCESS1–3 in plaats van de Photon.
- **De lux in `/json`**, zodat ook het Dashboard, de ECO en anderen hem kunnen lezen. Daarmee is een aparte OUTSIDE-controller (.72) waarschijnlijk overbodig.
- **`/json` is een contract.** Keys en bedieningsadressen worden vastgelegd vóór v0.2 en daarna niet meer gewijzigd zonder akkoord. Een uitgeschakelde zone voert geen code uit, maar haar keys blijven bestaan (waarde 0).

### 3.1 Voorstel `/json`-keys en bediening (te bevestigen)

| Key | Betekenis |
|---|---|
| `a` | uptime (s) |
| `z1m` … `z4m` | beweging in zone 1–4 in de laatste 10 min (1/0) |
| `z1l` … `z4l` | licht zone 1–4: helderheid nu (0–100 %) |
| `z1a` … `z4a` | modus zone 1–4: 1 = AUTO, 0 = MANUEEL |
| `lux` | lux (−1 = geen bron) |
| `nt` | nacht (1/0) |
| `lb` | bron dag/nacht: 0 = eigen sensor, 1 = andere ACCESS, 2 = berekend |
| `ac`, `ad`, `ae`, `af` | RSSI, heap %, grootste blok, laagste heap (zoals ROOM) |

Bediening: `/set_zone?z=1..4&v=1` (manueel aan), `v=0` (uit, terug naar AUTO), optioneel `&b=0..100` (helderheid).

## 4. Niet in de standaard

- Geen andere sensoren (temperatuur, CO2…), geen pixels, geen relais. AlarmiX (deuren, sloten, bel) blijft in de planning, buiten deze standaard.
- Spelregels zoals bij de rooms: iets nieuws alleen als het in de standaard past; eerst dit document aanpassen, dan hardware en code; experimenten in `ESP32C6_Experiments`.

## 5. Stappenplan

Telkens één versie, na GO, met versiekop, changelog en een 48 u-test.

| Versie | Wat |
|---|---|
| v0.1 | **Basis:** WiFi, OTA, mDNS, `/settings`, `/json`, Diagnose (hergebruikt uit ROOM). Op het testbord (§6.2) |
| v0.2 | **Zones:** PIR, brandtijd, helderheid (PWM), AUTO/MANUEEL, meeschakelen, `/set_zone`. Nog zonder dag/nacht |
| v0.3 | **Dag/nacht:** TSL2561, lux delen tussen de controllers, berekende terugval |
| v0.4 | **Plaatsen als ACCESS1.** R8-ACCESS uit; `server.js` en Dashboard (rij 3) naar ACCESS1. Daarna ACCESS2 en ACCESS3 in de B&B |

## 6. Beslissingen (FiDel, 7/11)

1. **Zone N1** (lampen tussen de voordeur aan de W-zijde en de N-muur) **blijft bij ACCESS1**: werkt goed. De noordgevel bij de B&B is een ander stuk muur en komt op ACCESS2.
2. **Testbord** voor v0.1–v0.3: een losse ESP32-C6 op een shield, met **één LED-driver en een LED** op een houten bordje. FiDel maakt het in de week van 9/11.
3. **PWM-frequentie** van de drivers: **start op 1 kHz**, daarna testen op geflikker en lawaai; bijstellen indien nodig.

Nog open: bevestiging van de `/json`-keys (§3.1), vóór v0.2.

## 7. Historiek

| Versie | Datum | Wat |
|---|---|---|
| v0.2 | 7 nov 2026 | Open vragen beantwoord: N1 blijft bij ACCESS1, testbord met één driver + LED, PWM start op 1 kHz |
| v0.1 | 7 nov 2026 | Eerste voorstel: zoneslots, LED-drivers 300 mA op 3,3 V, TSL2561 op één controller met delen en berekende terugval, AUTO alleen 's nachts, MANUEEL-override, stappenplan |
