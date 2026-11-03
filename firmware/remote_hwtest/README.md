# CRIG remote hardware test

ESP-IDF v5.4.1, target ESP32-S3 (DevKitC-1U, N16R8). Exercises every part of the remote
once at boot, then runs a live loop. Pins and wire colours: `main/main.c` header,
`crig_remote_schematic.svg` v0.2, spec v0.16 §12.2.

| Pin | Function | Test |
|---|---|---|
| IO1 | Battery sense (100 k / 100 k) | Voltage printed at boot (`v`) |
| IO4 | SHOOT button (int. pull-up) | Live: held → red LED + motor + RGB red, logged as `[EVT]` |
| IO5 | TRIG (PC817) | Level at boot and `[EVT]` on change (J1 not wired yet) |
| IO6 | ARM (second PC817, spec v0.17) | Not read by this firmware yet; the opto is not fitted yet |
| IO8 / IO9 | OLED SDA / SCL (0x3C) | Line levels, software + hardware I²C scan, test pattern, live page |
| IO10 / IO11 | Green / red LED | 1.5 s each at boot (`g`, `r`) |
| IO12 | Vibration motor (via Q1) | 0.6 s at boot (`m`) |
| IO15 | Buzzer | 2 beeps at boot (`b`) |
| IO48 | On-board RGB LED | R, G, B at boot (`l`) |

```
. ~/esp/esp-idf/export.sh
idf.py build
P=/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CCC062258-if00   # remote, UART-bridge port
esptool.py -p $P read_mac          # must be 68:ee:8f:47:96:d4 before flashing
idf.py -p $P flash
python3 run_test.py 45             # restart the app and print the console for 45 s
```

Console is UART0 at 115200. `run_test.py` opens the port with DTR/RTS released and pulses
RTS (EN) to restart. One-letter commands: `? i v n s p o b m g r l A` (see `?`).

Result 2026-11-03: everything wired passes (after resoldering the two LEDs, which were
reversed, and repairing a lifted trace on the OLED module). TRIG and DET wait for J1 / IO6.
