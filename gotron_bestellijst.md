# CRIG — Bestellijst Gotron.be

Prijzen incl. btw, opgehaald op 2026-10-07 (kortingsprijs waar van toepassing). Controleer voorraad en prijs op de pagina.

| Gotron-ref. | Onderdeel | Aantal | Stukprijs | Totaal | Opmerking |
|---|---|---|---|---|---|
| PC817 | [EL817 optocoupler DIP4](https://www.gotron.be/pc817-opto-coupler-5kv-80v-50ma.html) | 6 | €0,22 | €1,32 | 3 node (HALF, FULL, optioneel groen), 1 remote (TRIG), 2 reserve |
| RB100E R1% | [100 Ω 1 % metaalfilm 0,5 W](https://www.gotron.be/r1-100-ohm-metaalfilmweerstand-0-5watt.html) | 4 | €0,12 | €0,48 | S-lijn camera (NTC-vervanging) + reserve. **Vervallen:** S is ≈100 kΩ (102 kΩ gemeten op 2026-11-03), niet 100 Ω, en draagt data |
| RC120E R1% | [120 Ω 1 % metaalfilm](https://www.gotron.be/componenten/passief/weerstanden/1-metaalfilmweerstand.html) | 1 | €0,12 | €0,12 | Testwaarde voor S (82 Ω en 150 Ω: geen productpagina gevonden) |
| RC1K0 R1% | [1 kΩ 1 % metaalfilm](https://www.gotron.be/componenten/passief/weerstanden/metaalfilm1/rc1k0-r1-1-kohm-metaalfilmweerstand-1-4-watt.html) | 10 | €0,12 | €1,20 | Opto-LED serieweerstand |
| RC10K R1% | [10 kΩ 1 % metaalfilm](https://www.gotron.be/rc10k-r1-10-kohm-metaalfilmweerstand-1-4-watt.html) | 10 | €0,12 | €1,20 | Pull-downs, pull-ups |
| LS18650-3 | [Li-ion 18650, 3350 mAh, 10 A (blote cel)](https://www.gotron.be/oplaadbare-lithium-ion-batterij-3-7-v-3500-mah.html) | 5 | €5,40 | €27,00 | 4 node-pack (2S2P, ~48 Wh), 1 remote. Geen beveiliging in de cel |
| BAT120 | [18650-houder met draden (15 cm)](https://www.gotron.be/batterijhouder-voor-ls18650-lion-cel-met-draden.html) | 1 | €1,07 | €1,07 | Remote. Voor het pack: 4 losse houders = €4,28 extra (optioneel) |
| Z162A | [Zekeringhouder chassis 5 × 20 mm (max 10 A)](https://www.gotron.be/zekeringhouder-voor-chassis-5-x-20mm-vlakke-kop.html) | 1 | €1,24 | €1,24 | Eén zekering achter de BMS |
| Z2T2,5A | [Zekering 5 × 20 mm, 2,5 A traag, 10 st.](https://www.gotron.be/zekering-5x20mm-traag-2-5a-230v-10st.html) | 1 pak | €2,89 | €2,89 | Enige zekering (achter de BMS); valideren met stroommeting (O-11), anders 5 A (Z2T5A, €2,89 per 10) of 3,15 A |
| VMA438 | [OLED 0,96" I²C (Whadda)](https://www.gotron.be/0-96-oled-display-met-i2c-voor-arduino.html) | 1 | €13,14 | €13,14 | Remote |
| ADA1201 | [Trilmotor 10 mm (Adafruit)](https://www.gotron.be/vibrating-mini-motor-disc.html) | 1 | €2,10 | €2,10 | Remote |
| VMA321 | [TP4056-lader WPM321 (2 st.)](https://www.gotron.be/oplaadboard-voor-lithiumbatterij-1-a-2-st.html) | 1 pak | €4,91 | €4,91 | Alleen prototype; geen beveiliging, geen power-path |
| | **Totaal** | | | **€56,67** | €60,95 met 4 extra celhouders |

## Niet bij Gotron gevonden (elders bestellen)

- Buck 2S → **5,0 V, ≥1 A** (MP1584 / mini-360, eerst op 5,0 V instellen) voor de 5V0-pin van het devboard; de Gotron-buck heeft 12–24 V ingang en past niet
- ESP32-S3-DevKitC-1 (zelfde bord als de RLC) in plaats van een losse module; voor u.FL de -1U-variant (O-16)
- 2.54 mm gepolariseerde connectoren (JST-XH-klasse, crimpcontacten): J2 3-pins (voeding + S, stroom ~1–1,5 A, geen losse Dupont) en J3 4-pins (signalen), steker + bus, 1 set voor de node; draad in de kleuren van het schema
- 2S-BMS ≥8 A en 2S-balanslader
- XT30 (alleen de laad-/ontlaadlead van het pack)

## RLC-interface (spec v0.18, nog te bestellen bij Gotron)

| Gotron-ref. | Onderdeel | Aantal | Stukprijs | Totaal | Opmerking |
|---|---|---|---|---|---|
| AD322 | 5-polige DIN 180° vrouwelijk, chassismontage (plastic) | 2 | €1,23 | €2,46 | RLC-basis + CRIG-remote (vervangt GX12-4) |
| AD302 | 5-polige DIN 180° mannelijk, voor op kabel (plastic) | 2 | €1,57 | €3,14 | Beide kabeleinden; kabel 1-op-1 bedraden (1–1, 2–2, 3–3) |
| PC817 | Optocoupler | +1 | €0,22 | €0,22 | Tweede opto (ARM) in de remote; uit reserve als die er nog is |
| | **Totaal** | | | **≈ €5,82** | Prijzen opgehaald 2026-11-03 |
- TPS63020 en BQ24074 (remote)
- Grote gele SHOOT-knop, ARM-toggle met beugel, TEST/MENU-knoppen, schuifschakelaar

## Camera-floatlader (spec v0.23 §11.3, nog te bestellen)

EN-EL9a blijft in de camera; de node houdt hem op max. 8,0 V. Prijzen incl. btw, opgehaald op 2026-11-07. Gotron heeft geen buck-boost met instelbare stroombegrenzing; de stroom wordt passief begrensd met diode + weerstand.

| Gotron-ref. | Onderdeel | Aantal | Stukprijs | Totaal | Opmerking |
|---|---|---|---|---|---|
| BUCKBOOST02 | [DC-DC buck-boost Joy-IT SBC-BuckBoost02, 5–25 V → 3–25 V, 2 A, 30 W](https://www.gotron.be/dc-dc-buck-boost-converter-joy-it-5-25v-naar-3-25v-30w-2a.html) | 2 | €5,90 | €11,80 | 1 + 1 reserve. Op **8,00 V** zetten en meten vóór aansluiten. Chips op de foto's: FP5139 (boost-controller) met één spoel met 4 pinnen = SEPIC (bevestigd), MBR10450S/MBR1045OS (Schottky 10 A / 45 V), BL1512 (vermoedelijk MOSFET). Geen instelbare stroomgrens. Ruststroom meten (T-47). Alternatief met display: [BUCKBOOST01](https://www.gotron.be/voltage-converter-met-step-up-en-step-down-functie-met-display-joy-it.html) €11,91 (LM2577 + LM2596, minder efficiënt) |
| 1N5822 | [Schottky 1N5822, 3 A](https://www.gotron.be/diode-1n5822.html) | 2 | €0,65 | €1,30 | Terugvoedingsblokkade; 1N5819 (1 A) is te krap bij ~0,95 A |
| R3W/1E0 | [1 Ω 3 W metaaloxide 5 %](https://www.gotron.be/3w-metaaloxide-weerstand-5-1-ohm.html) | 2 | €0,48 | €0,96 | Stroombegrenzing (max ≈ 0,95 A, ≈ 0,9 W) |
| R3W/1E5 | [1,5 Ω 3 W metaaloxide 5 %](https://www.gotron.be/3w-metaaloxide-weerstand-5-1-5-ohm.html) | 1 | €0,48 | €0,48 | Optioneel: als 0,95 A te warm blijkt (dan max ≈ 0,65 A) |
| | **Totaal** | | | **€14,54** | Voorraad van de diode en weerstanden niet gecontroleerd |

## Nog niet besteld / wachten op test

- NTC100 ([Gotron](https://www.gotron.be/ntc-thermistor-100ohm-10-tolerance.html)), ref. NTC100: alleen als de vaste 100 Ω op S niet volstaat of je echte celtemperatuur wilt
