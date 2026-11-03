# CRIG EN-EL9(a) S-line sniffer

ESP-IDF v5.4.1, ESP32-S3-DevKitC-1 (N16R8). Listen-only capture of the camera's battery "S" line,
to work out the protocol (see `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md`). GPIO2 is never an output.

## Wiring
```
camera S (J2 pin 2) --[10 kΩ]--+-- GPIO2 (ADC1_CH1)
                               +--[96.8 kΩ]-- camera − (BLK) -- ESP32 GND    phase 1: bench supply, no battery
                                                                             phase 2: remove 96.8 kΩ, real battery in
```
Power the ESP32 before the camera. ESP32 GND straight to BLK (not via BLU). Camera USB unplugged.

## Use
```
. ~/esp/esp-idf/export.sh && idf.py build
esptool.py --chip esp32s3 -p <by-id> --after no_reset write_flash 0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin 0x10000 build/crig_ssniff.bin
esptool.py -p <by-id> --after watchdog_reset chip_id      # start the app
./ssniff.py                  # phase 1 (divider 0.906)
./ssniff.py --div 1          # phase 2 (10 kΩ only)
./ssniff.py --decode captures/<run>/burst_001.csv         # re-decode offline
```
Keys: `+`/`-` trigger threshold ±50 mV (default 400 mV at GPIO2), `i` info, `s` idle stats on/off, `q` quit.

The ADC samples continuously (≈80.6 kHz measured, ≈8.4 samples per 9600-baud bit). A burst starts at the
first sample above the threshold and ends after 20 ms below it (5 ms pre-trigger, max 1 s). Output per run in
`captures/<date-time>/`: `burst_NNN.csv` (t_s, v_gpio, v_s), `bursts.txt` (levels, bit string, UART 8N1 decode
in both polarities, `!` = framing error), `log.txt` (everything, incl. `S` idle stats every 5 s: min/max/avg mV,
sample rate, ADC overflows). A burst right after boot can be an ADC start-up artefact.
