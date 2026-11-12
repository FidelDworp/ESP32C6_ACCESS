# Overdracht — ACCESS (buitenverlichting, ESP32C6_ACCESS)

*Stand 12 november 2026, 19:45 — Filip Delannoy (FiDel), Zarlardinge (BE)*

Dit document start een nieuw gesprek met Claude over de buitenverlichting: de Photon R8-ACCESS van vandaag en de ESP32-controllers ACCESS1–3 die nog ontworpen moeten worden. Alle details: [`README.md`](../README.md).

**Werkwijze en afspraken:** zie de [overkoepelende overdracht](https://github.com/FidelDworp/Zarlardinge-Home-Automation/blob/main/docs/ZARLAR_OVERDRACHT.md). Kort: Nederlands · nooit een sketch of script zonder GO · eerst een plan, één stap per versie (versiekop + changelog) en daarna een 48 u-test · alles via OTA · `/json`-keys en bedieningsadressen niet wijzigen zonder akkoord · README en deze overdracht mee bijwerken · na GO compileert Claude en zet de `.bin` in `bin/`.

---

## 1. Wat en waar

| Controller | IP | Muren | Stand |
|---|---|---|---|
| ACCESS1 | 192.168.0.82 | Zuid + West | nog te ontwerpen; neemt R8-ACCESS over |
| ACCESS2 | 192.168.0.83 | Noord (B&B) | nog te ontwerpen |
| ACCESS3 | 192.168.0.84 | Oost (B&B) | nog te ontwerpen |

- **Eén sketch** `ESP32_C6_ACCESS` voor de drie; verschillen worden instellingen.
- **Vandaag:** Photon **R8-ACCESS** (westmuur slaapkamer): W1 Voordeur, W2 Veldkant, S Terras, met PIR (30 s) en manuele bediening; uitgangen actief laag. Laatste sketch: `old/R8-ACCESS.ino` (origineel in `PHOTON_ROOMS`).
- **Dashboard rij 3** (W·Z·N·O) leest `JSON_status` van R8-ACCESS via de Cloudflare Worker (`mw1`, `mw2`, `ms`, `lw1`, `lw2`, `ls`, `au`).
- **Apple Home:** Voordeur, Veldkant en Terras via de RPi-bridge (`server.js` v2.12).

## 2. Ontwerp: de ACCESS-standaard

Zie [`ACCESS_STANDAARD.md`](ACCESS_STANDAARD.md) (v0.3, vastgelegd 8/11). **Beslist door FiDel (7–8/11):**
- Uitgangen: **PWM naar LED-drivers van 300 mA** (standaard 2 LED-lampen 9,5 V / 300 mA in serie op 24 V); de drivers werken op de Photon met 3,3 V → eerst zonder buffer, buffer naar 5 V alleen als nodig.
- **Luxsensor (TSL2561)** normaal op ACCESS1, mogelijk op ACCESS2 of 3: waar hij het daglicht het best permanent ziet.
- **AUTO alleen 's nachts**, tenzij manueel aangezet (**override zoals in de ROOM-sketch**).

- **Zones (8/11):** ACCESS1 = `S` (S1+S2), `W4`, `D` voordeur (W2+DN, DS+W3) · ACCESS2 = `W1`, `N1` · ACCESS3 = `N2`, `E` (E1+E2). Zonecode = enige naam overal.
- **`/json`:** `m<zone>` / `l<zone>` (licht 0–100 %), één modus `au` per controller, manuele override max. 30 min (instelbaar). De R8-keys `mw1`/`lw1`/`mw2`/`lw2` vervallen bij de overstap (v0.4: Dashboard + `server.js` mee om).
- Testbord = losse ESP32-C6 op shield met één driver + LED op een houten bordje (FiDel maakt het week van 9/11); **PWM start op 1 kHz**.

Afwijkingen van de Photon-sketch: zie §6.1 van de standaard.

**Deurcontacten (gepland v0.5, 11/11):** reed-relais op IO6/IO7: ACCESS1 schuifdeur (`SD`) en voordeur (`VD`), ACCESS3 achterdeur (`AD`) en bovendeur (`BD`); keys `c<code>` (voorstel, standaard §4.1). **Sloten** blijven op hun eigen controllers met een geheim (beslist).

## 3. Open punten (vóór het ontwerp van ACCESS1)

1. Terraslicht (S) gaat niet aan, ook niet via Apple Home: andere oorzaak, te onderzoeken.
2. Koppeling W1 → W2 nagaan (`lw2` bleef 0 bij beweging aan de voordeur).
3. PIR W2 volgens de code defect.
4. **Sketch v0.1.1 op het testbord** (12/11): `bin/ESP32_C6_ACCESS_v0.1.1.ino.bin`.
   - **Test 12/11 (v0.1):** PWM op 3,3 V zonder buffer werkt (0 V = 100 % aan, 3,3 V = uit, 1 kHz). Een AM312 op IO3 werkte niet (pull-up van de DS18B20-bus) → **nieuwe pinverdeling** (standaard §1.1, §6.2): PIR's op IO5, IO12, IO2, IO7; deurcontacten op IO3 en IO6.
   - v0.1.1: nieuwe pinnen, deurcontacten tonen (codes in `/settings`), statuspagina ververst om de 2 s, testschuifjes op de statuspagina.
   - Getest 12/11: PIR D op IO5 en de reed-ingangen werken. Niet-aangesloten PIR-ingangen zweefden (duizenden valse bewegingen) → opgelost in v0.2 (pull-down).
5. **Sketch v0.2.2** (12/11): `bin/ESP32_C6_ACCESS_v0.2.2.ino.bin`. Nieuw t.o.v. v0.2.1: `m<zone>` = triggers per minuut (zoals ROOM, beslist 12/11). Daarvoor v0.2.1: v0.2 had een interne pull-down op de PIR's: de AM312 haalde dan maar 1,63 V → in v0.2.1 weer zonder; hardware 470 kΩ–1 MΩ pull-down per PIR-lijn (standaard §1.1). Pagina's tonen de plaats op de plaatjes ("Sense 2 · IO5", "Option 5 · IO19"). AUTO = beweging → licht op helderheid, uit na brandtijd; dag/nacht tot v0.3 vast (standaard altijd nacht). MANUEEL via `/set_zone?z=<zone>&v=1[&b=..]` of de knoppen op de statuspagina; `v=0` of na de override-tijd (30 min) → hele controller terug naar AUTO (beslist 12/11). Te testen: beweging voor PIR D → lamp aan, uit 30 s na de laatste beweging; Aan/Uit-knoppen; daarna 48 u-test en plan + GO voor v0.3 (lux, dag/nacht).
5. PIReX-combinatie MOV-W1 + MOV-W3 + MOV-DN nakijken (W1 gaat naar ACCESS2).
