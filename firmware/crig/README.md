# CRIG firmware (node + remote)

ESP-IDF **v5.4.1**, ESP32-S3 (DevKitC-1 / -1U, N16R8). One project with two images, selected by Kconfig
(`CONFIG_CRIG_UNIT_NODE` / `CONFIG_CRIG_UNIT_REMOTE`). Implements spec `camera-rig-functional-spec.md`
v0.21, which records the firmware's choices in §5, §7.3, §11.2, §13 and §14.5.

```
components/crig_common   protocol + CRC (crig_proto), ARM decoder (crig_armdec), ESP-NOW link (crig_link),
                         WS2812, console, buzzer, parameters (crig_config.h), peer binding (crig_binding.h)
components/crig_node     camera node: shutter FSM, PREPARE, pack monitor, status LED, console
components/crig_remote   remote: inputs, TRIG/ARM, link quality, readiness LEDs, motor, RGB LED, OLED, battery
tests/host               unit tests (gcc): CRC, packet encode/decode, ARM decoder
tests/hil                hardware-in-the-loop tests driving both boards over their consoles
tools/gen_binding.py     generates the git-ignored crig_binding.h (MACs, PMK, LMK, channel)
tools/cap2.py            capture both consoles (optionally reset both, send timed commands)
```

## Build and flash

```
python3 tools/gen_binding.py --node 68:ee:8f:47:8e:c8 --remote 68:ee:8f:47:96:d4   # once; --force regenerates keys
./build.sh node   && ./flash.sh node      # flash.sh checks the board MAC first and refuses a mismatch
./build.sh remote && ./flash.sh remote
```

Ports: node `usb-1a86_USB_Single_Serial_5CCC062602-if00`, remote `usb-1a86_USB_Single_Serial_5CCC062258-if00`
(UART-bridge ports, see `~/claudecode/usb-devices.md`). Console: UART0, 115200. Open the port with DTR/RTS released;
pulsing RTS resets the board (the HIL tests use this).

The console also works on the **native USB port** (`usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`): output is
mirrored there, and input is read from its RX FIFO. Open it raw without touching DTR/RTS (they select download mode);
`reboot` restarts the board. `flash.sh` and `hil.py` fall back to the native port when the UART bridge isn't plugged in
(a board on its native port can't be held in reset, so H03 and H12 need the **node** on its UART bridge).
Both units take `reboot`, which the harness uses to restart a board on its native port.

## Tests

```
tests/host/run.sh                 # 30 checks
python3 tests/hil/hil.py [H05 …]  # H01–H17, about 3 minutes; prints both boards' event logs on a failure
python3 tests/hil/hil.py rlc      # R01–R02: real RLC base on the DIN cable (R02 resets the RLC base)
```

The remote's console **test hooks** (`CONFIG_CRIG_TEST_HOOKS`, on by default) stand in for hardware that a script
can't operate: `sim shoot|trig 0|1|real`, `sim arm none|1|10|real` (synthetic ARM edges), `sim batt <mv>|real`,
`sim badcrc` (one corrupt SHOOT). The node has `sim vbat <mv>|real`. Every hook falls back to the real input with `real`,
and the remote resets them all on reboot.

**The HIL burst tests take real pictures if the camera is connected to the node.**

## Consoles

| Unit | Commands |
|---|---|
| node | `status` (incl. pack ADC `vraw`/`vpin`), `test` (one HALF → FULL → REST cycle, FR-N07), `beep <ms>`, `reboot`, `scan`, `bcast`, `sim vbat` |
| remote | `status` (incl. ARM line stats `aedges`/`aper`/`aage`/`aisr`, battery ADC `braw`/`bpin`), `screen` (current OLED text), `scan`, `bcast`, `beep <ms>`, `reboot`, `sim …` |

Both log state changes as `EVT <ms> <what>`.

## Not covered by the automated tests

These need a person or hardware that isn't fitted yet:
- **Physical inputs:** the real SHOOT button debounce on IO4 (the earlier hardware test confirmed the button itself).
- **The RLC armed and firing:** R01–R02 cover the real idle ARM pattern, the idle TRIG line and a base reset; the
  10 Hz armed pattern and an active TRIG need a person to arm and fire the RLC (T-21, T-22, T-27).
- **What you see and hear** (LEDs, RGB, buzzer, motor, deep sleep): checked by hand on 2026-11-06 (spec §14.5); the
  automated tests see their logic through `ready` events and `screen`.
- **The camera itself** (frames, standby wake).
