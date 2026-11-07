# ESP32C6_ACCESS — buitenverlichting Zarlardinge

*Filip Delannoy (FiDel) — aangemaakt 6 november 2026*

> **Status:** de ESP32-controllers ACCESS1–3 moeten nog ontworpen worden. Vandaag bedient de **Photon R8-ACCESS** de buitenlichten zuid en west. Zijn laatste sketch staat in [`old/R8-ACCESS.ino`](old/R8-ACCESS.ino).

Overzicht van het hele systeem: [`Zarlardinge-Home-Automation`](https://github.com/FidelDworp/Zarlardinge-Home-Automation) (README en Master-document).

---

## 1. Drie controllers, één sketch

ACCESS1, 2 en 3 horen samen: ze krijgen **dezelfde sketch** `ESP32_C6_ACCESS` en **drie opeenvolgende IP-adressen**. Wat per controller verschilt (naam, zones, PIR's), wordt een instelling, geen aparte sketch.

| Controller | IP | MAC | Muren | Plaats | Stand |
|---|---|---|---|---|---|
| **ACCESS1** | 192.168.0.82 | 58:8C:81:32:26:9C | **Zuid** en **West** | nog te bepalen | 📋 nog te ontwerpen; neemt het werk over van de Photon R8-ACCESS |
| **ACCESS2** | 192.168.0.83 | — | **Noord** | B&B | 📋 nog te ontwerpen |
| **ACCESS3** | 192.168.0.84 | — | **Oost** | B&B | 📋 nog te ontwerpen |

## 2. Vandaag: de Photon R8-ACCESS

Plaats: **westmuur in de slaapkamer**. Bestuurt de buitenlichten met PIR-sensoren, met manuele bediening.

| Zone | Licht | Uitgang | PIR |
|---|---|---|---|
| **W1** | Voordeur | A5 | D3 |
| **W2** | Veldkant | A4 | D5 |
| **S** | Terras | A6 (DAC-pin, alleen aan/uit) | A7 |
| E | — (gepland, verhuist naar ACCESS3) | A2 | D6 |
| N | — (gepland, verhuist naar ACCESS2) | A3 (DAC-pin) | D7 |

**Werking**
- **Automatisch (PIR):** beweging → licht aan, 30 s na de laatste beweging weer uit.
- **Manueel:** Particle-functie `Manual` met `W1on`, `W2on`, `Son`… zet het licht aan en schakelt over naar manueel. `W1off`, `W2off`, `Soff`… zet het uit en terug op automatisch. `reset` herstart de Photon.
- De uitgangen zijn **actief laag** (LOW = licht aan).

**Apple Home:** de knoppen *Voordeur* (W1), *Veldkant* (W2) en *Terras* (S) lopen via de RPi-bridge (`server.js` **v2.12**, sinds 6/11/2026; daarvoor stuurden ze ten onrechte naar de Photon S-OUTSIDE).

**Toestand voor het Dashboard (sinds 6/11/2026):** Particle-variabele `JSON_status`, opgehaald via de Cloudflare Worker:
`https://controllers-diagnose.filip-delannoy.workers.dev/sensor?id=30002d000747353138383138`

| Key | Betekenis |
|---|---|
| `mw1`, `mw2`, `ms` | beweging aan W1 / W2 / S in de laatste 10 minuten (1/0) |
| `lw1`, `lw2`, `ls` | licht W1 / W2 / S aan (1/0) |
| `au` | 1 = automatisch (PIR), 0 = manueel |

Voorbeeld (beweging aan de voordeur): `{"mw1":1,"mw2":0,"ms":0,"lw1":1,"lw2":0,"ls":0,"au":1}`

## 3. Op het Dashboard: matrixrij 3

Sinds Dashboard **v6.4** toont rij 3 de 4 muren rondom het huis, van links naar rechts W · Z · N · O. Bron: `JSON_status` van R8-ACCESS, later de ACCESS-controllers. Bijgewerkt om de ±9 min (rondgang van de Photons).

| Pixel | Label | Kleur → betekenis |
|---|---|---|
| 1 | W | groen = online, automatisch · oranje = online, manueel · rood = offline · geel = wacht op de eerste poll |
| 2 | PIR W | paars = beweging aan W1 of W2 in de laatste 10 min · zacht grijs = geen beweging |
| 3 | Voordeur | warm geel = licht W1 aan · zacht grijs = uit |
| 4 | Veldkant | warm geel = licht W2 aan · zacht grijs = uit |
| 5 | Z | zoals pixel 1 (zelfde controller) |
| 6 | PIR Z | paars = beweging aan S in de laatste 10 min · zacht grijs = geen beweging |
| 7 | Terras | warm geel = licht S aan · zacht grijs = uit |
| 8 | — | uit (vrij) |
| 9 | N | heel dim grijs = nog geen controller (ACCESS2) |
| 10–12 | — | uit (later PIR en licht noord) |
| 13 | O | heel dim grijs = nog geen controller (ACCESS3) |
| 14–16 | — | uit (later PIR en licht oost) |

Zolang de bron niet online is (pixel 1 en 5 geel of rood), blijven pixels 2–4 en 6–7 uit.

## 4. Open checkpunten (voor het ontwerp van ACCESS1)

1. **Terraslicht (S) gaat niet aan**, ook niet met de knop in Apple Home (die sinds `server.js` v2.12 wel naar R8-ACCESS stuurt). Andere oorzaak, nog te onderzoeken.
2. **Koppeling W1 → W2:** volgens de code zet beweging aan W1 ook W2 aan ("tot de W2-PIR hersteld is"), maar het W2-blok zet W2 meteen weer uit zonder eigen beweging. Bij een test op 6/11 bleef `lw2` op 0, terwijl er 4 lampen links en rechts van de voordeur branden. Nagaan welke lampen W1 en welke W2 zijn, en of de koppeling werkt.
3. **PIR W2:** volgens het commentaar in de code is die defect.

## 5. De ACCESS-standaard (ontwerp)

Hardware, gedrag en stappenplan staan in [`docs/ACCESS_STANDAARD.md`](docs/ACCESS_STANDAARD.md) (v0.1, voorstel van 7/11/2026). Kort:

- **4 zoneslots** op vaste pinnen, identiek op elke controller: PIR-ingang + PWM-uitgang naar een **LED-driver van 300 mA** (2 × 12 V / 3 W in serie op 24 V). Sturing op **3,3 V**, zoals de Photon; buffer naar 5 V alleen als een test dat nodig maakt.
- **AUTO = alleen 's nachts** (beslist), **MANUEEL AAN** als override zoals in de ROOM-sketch (beslist).
- **Lux:** één **TSL2561** op de controller met het beste zicht op het daglicht (normaal ACCESS1). De andere lezen de lux via `/json`; valt die weg, dan berekenen ze zelf zonsopgang en -ondergang.
- Stappen: v0.1 basis → v0.2 zones → v0.3 dag/nacht → v0.4 plaatsen als ACCESS1.

## 6. Mapindeling

| Map | Inhoud |
|---|---|
| `README.md` | dit document |
| `ESP32_C6_ACCESS/` | de sketch (komt na het ontwerp) |
| `docs/` | `ACCESS_STANDAARD.md` (hardware, gedrag, stappenplan) · `ACCESS_OVERDRACHT.md` (voor een nieuw gesprek met Claude) |
| `old/` | `R8-ACCESS.ino`: laatste Photon-sketch (6/11/2026, met `JSON_status`). Het origineel blijft in [`PHOTON_ROOMS`](https://github.com/FidelDworp/PHOTON_ROOMS) |
