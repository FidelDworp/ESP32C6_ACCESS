# Overdracht — ACCESS (buitenverlichting, ESP32C6_ACCESS)

*Stand 7 november 2026 — Filip Delannoy (FiDel), Zarlardinge (BE)*

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

## 2. Uitgangspunten voor het ontwerp

- ESP32-C6 op het Zarlar shield; nooit meer dan 3,3 V op een pin; **buffer 3,3 → 5 V** (74AHCT1G125 of 74HCT125) voor de LED-modules buiten.
- Vast IP, `/json` als contract, logging via het Dashboard, Apple Home via de RPi.
- **Zonlicht (TSL2561, lux)** hoort voortaan op een ACCESS- of OUTSIDE-controller, niet op een room (7/11). Kiezen welke en of de lux gedeeld wordt.

## 3. Open punten (vóór het ontwerp van ACCESS1)

1. Terraslicht (S) gaat niet aan, ook niet via Apple Home: andere oorzaak, te onderzoeken.
2. Koppeling W1 → W2 nagaan (`lw2` bleef 0 bij beweging aan de voordeur).
3. PIR W2 volgens de code defect.
4. Ontwerp ACCESS1: plaats, aansluitingen, zones als instellingen, `/json`-keys (afstemmen op rij 3 van het Dashboard).
