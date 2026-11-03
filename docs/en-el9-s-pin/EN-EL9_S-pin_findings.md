# EN-EL9(a) S-pin: bevindingen (parkeerstand)

Datum: 2026-10-15. Status: **geparkeerd** — niet verder uitgezocht. De camerabesturing gaat voor.
Besluit: de ESP32-print wordt **niet** meer vanuit een dummy-batterij gevoed; de camera werkt verder met de originele Nikon-batterij.

## Doel
Een dummy-batterij (of voeding via het batterijcompartiment) voor de Nikon D3000 maken. Met onze eigen voeding (7,7 V op de +pin) en een vaste weerstand (96,8 kΩ) tussen S en min meldt de camera dat de batterij niet gebruikt kan worden.

## Metingen
| Meting | Resultaat |
|---|---|
| Weerstand S–min, originele batterij, uit de camera | ≈100 kΩ bij kamertemperatuur; daalde naar 86 kΩ na opwarmen in de oksel en liep daarna langzaam terug op (NTC-gedrag) |
| Weerstand S–min, originele batterij, uit de camera (2026-11-03) | **102 kΩ** |
| Gelijkspanning op S, originele batterij los | 0 V |
| Gelijkspanning op S, originele batterij in de camera | 0 V (multimeter) — geen statische bias |
| Oscilloscoop op S bij plaatsen van de batterij | digitale bursts, zie hieronder |
| Voedingsspanning aangeboden op + | 7,7 V (past bij 7,2–7,4 V nominaal, 8,4 V vol) |
| Dummy met 96,8 kΩ S–min | camera weigert de batterij |

De 100 kΩ-NTC zit dus waarschijnlijk **naast** (of gedeeld op) S, maar de weerstand alleen is niet genoeg: er is ook communicatie.

## Protocol op S (scope-captures)
Bronbestanden: `/storage/fileshare/d3000-scope-captures/NewFile0e..j.csv/.txt` (100 kSa/s, 1 V/div, 2 ms/div, DC, 1 MΩ, trigger stijgend 1,04 V; tijdstempel scope staat op 2005, negeren) en eerdere `NewFile0.csv` / `bewFile0.csv` in `/storage/fileshare/` (50 kSa/s, identiek aan elkaar).

- Digitaal signaal van ~1–1,5 V, **9600 baud** (bittijd 104 µs), op een hoogohmige lijn met veel 50 Hz-instraling (±0,3–0,9 V).
- Bij het plaatsen van de batterij twee bursts, ~1 ms uit elkaar:
  - **B1** (≈93 bits): geen vaste kop, inhoud verschilt per opname.
  - **B2** (≈142 bits): vaste kop, in alle 5 opnames identiek: 10 bits laag (break), `1010101010` (0x55-sync), `0x25 55 FF`; daarna ≈80 bits payload die **per opname verschilt**.
- Voorbeelden payload B2 (UART 8N1, LSB eerst, na de kop): f `C4 6B 5C 18`, g `37 47 D2 B9`, h `03 4A 55 6B`, i `CB 4A 54 34`, j `38 10 28 D4`.
- Bitgrenzen en bytes zijn een schatting uit 10 samples per bit; start/stop-uitlijning is een aanname.

## Conclusie en onzekerheden
- Een vaste weerstand volstaat niet; opnemen en terugspelen is zinloos als de payload een uitdaging-antwoord of teller is.
- **Niet vastgesteld:** welke burst van de camera komt en welke van de batterij; of er cryptografie in zit; wat de andere contacten doen; de pinout van de EN-EL9a (geen bron gevonden bij zoeken op het web).
- Handige feiten: de Nikon EP-5 (+ EH-5/EH-5A) is een bestaande DC-koppeling voor de EN-EL9 en werkt in de D3000.

## Als we hier op terugkomen
1. Meet de gelijkspanning en weerstand van **alle** batterijcontacten ten opzichte van min (originele batterij).
2. Neem op met de camera aan en **zonder batterij**: wat stuurt de camera alleen? Dat identificeert de camera-burst.
3. Opnemen met een betere massa (korte massaveer op batterij-min) en bij 1 MSa/s of hoger; meerdere opnames met dezelfde en andere batterijen.
4. Overweeg een **EP-5/EH-5A** te kopen en zijn S-lijn te meten, of een derde-partij-EN-EL9 te onderzoeken die in de D3000 werkt.
5. Tools: `tools/edges.py` en `tools/uart_decode.py` (pad naar de captures staat erin).

## Vervolg 2026-10-16: ESP32-sniffer
`firmware/ssniff/` (firmware + `ssniff.py`): S via 10 kΩ naar GPIO2 (ADC1, ≈80 kSa/s, alleen luisteren). Fase 1: bench-voeding, 96,8 kΩ blijft op J2 (camera alleen); fase 2: 96,8 kΩ eraf, echte batterij erin (gesprek camera ↔ batterij). Zie de README daar.

## Webonderzoek 2026-10-19: is het protocol publiek?
Nee, niets gevonden. "Fully decoded" dummy's en kloonbatterijen bestaan (vooral EN-EL14/EN-EL15, ook EP-5-vervangers voor de EN-EL9(a) / D3000; de D3000 gebruikt de EN-EL9a), maar de makers publiceren niets. Enige technische bron: teardown van een EN-EL14-kloon (goughlui.com, 2013): 5 contacten, middelste = data via spanningsdeler, IC volgens een (onbevestigde) commentator een Lattice iCE40/iCE65-FPGA. Geen GitHub-project of protocolbeschrijving voor Nikon gevonden. D40-firmware 1.12 (2009) verbeterde de restladingsmeting van de EN-EL9a: de camera leest dus ladingsgegevens uit de batterij, wat (vermoeden) de wisselende payload kan verklaren zonder dat het cryptografie hoeft te zijn.
