# CRIG node bring-up test

ESP-IDF v5.4.1, target ESP32-S3 (DevKitC-1 N16R8). Safe test of the node I/O:
on-board WS2812 (GPIO48 on a v1.0 board, GPIO38 on v1.1), buzzer (IO15), VBAT_SENSE (IO1),
and the three optocoupler outputs FULL/HALF/PWR (IO4/IO5/IO6, as built: IO4 = FULL via U3, IO5 = HALF via U4), which stay LOW until armed with `a`.

```
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00 flash
esptool -p <same by-id> --after watchdog-reset chip-id     # start the app (see note)
./term.py                                                  # commands: ? i v b l a d h f g
```

Note: on the native USB port a normal `hard-reset` can leave the chip in download mode
("waiting for download"); `--after watchdog-reset` starts the app. Do not use minicom/screen
(they toggle DTR/RTS); use `term.py`.
Never connect USB while the node pack powers the 5V0 pin.
