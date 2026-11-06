# CRIG (D3000 Rocket Photography) — Changelog

## 2026-11-03 — Remote built: hardware test firmware, all wired parts pass, as-built docs (spec v0.16, schematic v0.2)

### 1. Goal
David soldered up the CRIG remote; validate the hardware with a test firmware and record the build as-built.

### 2. Board identification
- One device on USB: `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CCC062258-if00` (the dev board's CH343 UART-bridge port, `1a86:55d4`, same VID:PID as the TTGOs — the serial tells them apart).
- `esptool read-mac`: **68:EE:8F:47:96:D4** (not the node, …8E:C8). ESP32-S3 QFN56 rev v0.2, 16 MB flash, 8 MB octal PSRAM.
- Added to `~/claudecode/usb-devices.md` (CRIG section).

### 3. Wiring as built (wire colours, Dutch)

| GPIO | Function | Wire |
|---|---|---|
| IO1 | Battery sense | geel/blauw |
| IO4 | SHOOT button | groen/zwart |
| IO5 | TRIG (PC817) | grijs/wit (David first said IO3, corrected) |
| IO8 / IO9 | OLED SDA / SCL | blauw/wit / wit/blauw |
| IO10 | Green LED | groen/zwart (same colours as IO4!) |
| IO11 | Red LED | blauw/paars |
| IO12 | Vibration motor | wit/grijs |
| IO15 | Buzzer | geel/oranje |
| IO48 | On-board RGB LED | (on board) |

IO6 (RLC_DET) and J1 (GX12-4) not wired yet.

### 4. Test firmware `firmware/remote_hwtest/`
- ESP-IDF v5.4.1, console on UART0 115200 (`CONFIG_ESP_CONSOLE_UART_DEFAULT`).
- Boot sequence: info, battery, inputs, I²C line levels (pull-up / pull-down), software (bit-bang) and hardware I²C scans (incl. SDA/SCL-swapped retry), then buzzer, motor, green, red, RGB, OLED pattern; then a live loop (SHOOT held → red LED + motor + RGB red; OLED live page).
- `run_test.py`: opens the port with DTR/RTS released, pulses RTS to restart, prints the console for N s.
- RGB fixed on GPIO48 (RLC `PIN_RGB_LED`), GPIO38 no longer touched.

### 5. Results

| Part | Result |
|---|---|
| MCU, flash, PSRAM | Pass |
| Battery sense IO1 | Pass, 3.78–3.89 V |
| Buzzer, motor, RGB LED, SHOOT button | Pass |
| Green / red LEDs | Fail first (both fitted reversed) → resoldered → pass |
| OLED | Fail first: lines pulled high but no ACK in either pin order (bit-bang and hardware) → David repaired a lifted trace on the module → 0x3C answers, display correct |
| Power-bank module | No light-load auto-off; runs while charging; 5 V starts by itself |
| TRIG / DET | Not tested (J1 / IO6 not wired) |

### 6. Power as built
18650 → 1S protection strip soldered on the cell (AliExpress BMS, variant "S1") → P+ through the on/on toggle switch → B+ of a power-bank charger/5 V boost module (LX-LBCE1, "5V 2.4A Type-C + USB", variant "2.4A Weld USB Blue") → 5V0. P− → module B−. Switch on the battery side so the module draws nothing when off; consequence: charge with the switch on. Product pages under `netwerkdrive/Electronics/Aliexpress/`; controller ICs and thresholds not documented by the sellers.

### 7. Documentation

| File | Change |
|---|---|
| `camera-rig-functional-spec.md` | v0.16: as-built §4, §9.2, FR-R13, §11.2 (power path, modules, switch, checks), §12.2; O-18, O-19 closed |
| `crig_remote_schematic.svg` / `.png`, `tools/remote_schematic.py` | v0.2 as built: protection strip + power-bank module, SW1 in P+, plain button + 2 LEDs, RGB IO48, wire colours, test status |
| `CRIG_session_handoff.md` | D14 runtime, D24 superseded note, D29 tweak, D30 (as built + tested) added, remote summary, O-18/O-19 closed |
| `firmware/remote_hwtest/` | New: `main/main.c`, `run_test.py`, `README.md`, CMake + sdkconfig.defaults |
| `~/claudecode/usb-devices.md` | CRIG remote row (by-id, MAC, port notes) |

### 8. Notes
- The autoresume guard blocked its own `off` command at 97 % (it runs through a tool call); David ran `! python3 ~/.claude/hooks/autoresume.py off`. The guard is still **off** on this machine — turn it back on with `/autoresume on`.
- Next (superseded by §14 below): wire J1 + IO6, then the real remote firmware.

### 9. RLC interface rework: ARM line (spec v0.17)
- Owner wants the RLC's arm state to reach the CRIG, so it can switch the camera on and wake it before TRIG, with the cable kept at 4 wires max. Serial over the TRIG opto was considered (fail-safe, but more RLC code and a cross-project protocol); owner chose a **second optocoupler + IO pin**.
- **ARM heartbeat:** no pulses = not connected / hung, 1 Hz (100/900 ms) = connected, 10 Hz (50/50 ms) = armed. Steady high was rejected (a crashed base would look armed).
- **TRIG gating:** TRIG is accepted only while ARM decodes as armed, so a stuck TRIG stops within about 0.3 s.
- **DET loop dropped:** the heartbeat replaces cable detect.
- **CRIG side:** RLC status NONE / CONNECTED / ARMED / LOST; PREPARE flag in HEARTBEAT/SHOOT (FR-R15); node camera preparation FR-N19 (camera on + HALF wake, keep-alive every 4 s).
- **RLC base pins:** GPIO39 = TRIG, GPIO41 = ARM. Checked against RLC `pin_config.h` (base uses 1, 2, 4–18, 21, 40, 42, 47, 48), the DevKitC-1 N16R8 reserved pins, and RLC spec App. C.1 (spares 38/39/41). GPIO38 stays spare (on-board RGB on v1.1 boards; the base is v1.0, LED on GPIO48).
- **Remote:** IO6 = ARM_IN (second PC817, U6). Schematic v0.3 has two optocouplers.
- **Hand-off for the RLC Claude session:** `docs/RLC_change_request_CRIG.md` (first named `…_v0.17.md`).
- First commit and new public repo: https://github.com/steemandavid/D3000-rocket-photography (`ec64e54`).

### 10. Connector: 5-pin DIN 180° instead of GX12-4 (spec v0.18)
- Gotron options compared:
  - M12-A (AD784 panel + AD781 plug): ≈ €36
  - Hirschmann CA (AD751/AD753): ≈ €24
  - **5-pin DIN 180° plastic: AD322 female chassis socket + AD302 male cable plug, ≈ €5.60** — chosen
  - AD323 (270° socket): rejected, the only 270° cable plug is Lumberg AD382 (€4.87), ≈ €11 in total
- **Pinout:** 1 = TRIG+, 2 = COM− (centre pin), 3 = ARM+, 4/5 n.c. Straight-through cable; ready-made DIN audio cables may swap 1↔3.
- Not waterproof and no lock: accepted (sheltered placement; an unplugged cable fails safe and shows RLC LOST).
- Updated: spec §8.2, §9.2, §12.2, T-12/17/25, App. A H-1/H-3/H-4, O-9, hand-off, schematic v0.4, `gotron_bestellijst.md` (new RLC-interface section ≈ €5.82).

### 11. RLC review answers (spec v0.18 §8.5, App. A)
1. **Pulse completed on link loss:** handled as a normal pulse end. ARM stays 10 Hz while TRIG is HIGH (the RLC session's "drop to 1 Hz" would have made the CRIG ignore TRIG).
2. **Mid-pulse aborts:** owner decided to **keep shooting**. Once FIRING is entered, TRIG always ends via the post timer; new hook `cam_trig_on_firing_enter()` plus a `fired` flag makes `cam_trig_cancel()` safe to call everywhere.
3. **LINK_LOST:** `do_enter_link_lost()` gets `cam_arm_set(false)`.
4. **Physical checks:** O-8 (GPIO39/41 continuity) for David. Also check for leftover wiring on GPIO41 from the RLC v1.7 continuity-MOSFET design, which was later dropped. O-9 is the DIN socket location.
5. **GPIO39/41 boot state** (JTAG pads): harmless. The 10 kΩ pull-down beats an internal weak pull-up (< 0.6 V at the opto). Verified by T-26.

### 12. Radio channels
- **RLC:** verified on channel 11 (`components/rlc_common/include/rlc_config.h:313` `WIFI_CHANNEL 11`, `rlc_espnow.c:170` with `WIFI_SECOND_CHAN_NONE`, no LR). The RLC spec allows changing it after a field survey.
- **CRIG: channel 1** (50 MHz from 11; the remote sits within 1 m of the base), fallback 6. Rule: ≥ 5 channels from the RLC channel (RR-02). Reduce the remote's TX power if T-20 shows an effect on the RLC (RR-05). §7.5 updated.

### 13. Documentation (since the first commit)

| File | Change |
|---|---|
| `camera-rig-functional-spec.md` | v0.17 → v0.18 (§7.1, §7.5, §8, App. A, tests, parameters, O-8/O-9/O-20) |
| `docs/RLC_change_request_CRIG.md` | Renamed; DIN connector; review answers (§8) |
| `crig_remote_schematic.*`, `tools/remote_schematic.py` | v0.3 (two optocouplers), v0.4 (DIN-5 J1) |
| `CRIG_session_handoff.md` | D15 superseded; D31–D34 added; diagram, O-9 |
| `gotron_bestellijst.md` | RLC-interface order section |
| `firmware/remote_hwtest/README.md` | IO6 = ARM (not read yet) |

### 14. Next
- **Remote:** fit U6 (PC817 + 1N4148, 10 kΩ / 10 nF) on IO6, and the DIN socket. Extend `remote_hwtest` to decode the ARM pattern.
- **RLC:** David does the O-8 continuity check, decides O-9, and hands the updated `docs/RLC_change_request_CRIG.md` to the RLC session.
- The autoresume guard is still **off** (`/autoresume on`).

## 2026-11-01 — Remote simplified (no arming), remote hardware defined, spec v0.15, draft remote schematic

### 1. Goal
Start on the CRIG remote: list its components, simplify it, and produce a first schematic.

### 2. Decisions (David, 2026-11-01)
- **No arming.** ARM switch, TEST and MENU buttons removed; the green/amber/red LEDs removed. Reason: the CRIG is not a launch controller; an accidental trigger only wastes a few frames.
- Board: **ESP32-S3-DevKitC-1U N16R8** (same family as the RLC) with external antenna on u.FL.
- Power: 18650 → **TP4056 module with protection** (DW01A + FS8205A) → slide switch → **5 V boost into 5V0** (like the RLC remote and node; option "b", accepting the LDO loss).
- On-board RGB LED = general status indicator, incl. link.
- LEDs (SHOOT ring) driven **directly from GPIOs**, no transistors (high-efficiency LEDs).
- Buzzer: same 3.3 V active part as the node (internal oscillator).
- SHOOT button type: David specifies later (O-18).

### 3. Spec v0.15 (`camera-rig-functional-spec.md`)
Applied as 56 checked replacements (each had to match exactly once).

| Area | Change |
|---|---|
| §3.3 | New principle 8 "No arming" |
| §4.2 / §4.3 | Short SHOOT tap replaces TEST; ARM steps removed |
| §5.1 | FR-N02 IDLE at power-up; FR-N03/04/07/08/10/15 reworded; FR-N07 test via node USB console; FR-N08 link loss = indication only |
| §5.2 | FR-R01/R03/R13 drop ARM; FR-R04, FR-R05 removed; FR-R14 (RGB status LED) added |
| §5.3 | No DISARMED outcomes; "accidental SHOOT" row added |
| §6.1 / §6.3 | Calibration on node console; camera power control follows the link instead of `arm_req` |
| §7.1 / §7.3 | Remote = DevKitC-1U; `arm_req`, TEST, CFG_GET/SET, JOG, CFG_VAL removed (codes reserved) |
| §9.1 | States BOOT / IDLE / SHOOTING / LOCKOUT / TEST / CAL / FAULT |
| §9.2 / §9.3 | Controls = SHOOT + power switch; single OLED page; RGB LED pattern table; node "Solid amber ARMED" removed |
| §11.2 | Rewritten: power path, ~185 mA at the cell, 2.9 Ah/day needed, ~16 h runtime; TPS63020/BQ24074 plan dropped |
| §12.2 | Rewritten remote hardware summary, references the draft schematic |
| §13 | `t_link_lost_disarm` → `t_link_lost` (2 s, indication only); no remote config page |
| §14 | T-03/T-09/T-31 reworded; T-08 removed; T-32 pass ≥ 15 % |
| §15 | O-5 closed; O-17, O-18 updated; O-19 (board revision → RGB LED IO48 v1.0 / IO38 v1.1) added |

### 4. Draft remote schematic v0.1
- `crig_remote_schematic.svg` / `.png`, generated by `tools/remote_schematic.py` (`python3 tools/remote_schematic.py crig_remote_schematic`, needs cairosvg).
- Drawing primitives copied from the RLC project: `tools/schematic_svg.py`.
- Proposed pin map:

| Function | GPIO |
|---|---|
| Battery ADC (100 k/100 k + 100 nF) | IO1 |
| SHOOT button (int. pull-up) | IO4 |
| TRIG (PC817 collector, 10 k pull-up, 10 nF) | IO5 |
| RLC cable detect (1 k series, 10 k pull-up, 100 nF) | IO6 |
| OLED I²C SDA / SCL (VMA438, 0x3C) | IO8 / IO9 |
| Ring green / red (1 k placeholder each) | IO10 / IO11 |
| Vibration motor (BC337 + 1 k / 10 k, 1N4148 flyback, from VBAT_SW) | IO12 |
| Buzzer (direct) | IO15 |
| On-board RGB LED | IO48 or IO38 (O-19) |

- Avoided: IO0/3/45/46 (strapping), IO19/20 (USB), IO35–37 (octal PSRAM), IO43/44 (UART0), IO38/IO48.

### 5. Files changed

| File | Change |
|---|---|
| `camera-rig-functional-spec.md` | v0.14 → v0.15 (see §3) |
| `crig_remote_schematic.svg`, `.png` | New, draft v0.1 |
| `tools/remote_schematic.py` | New, schematic generator |
| `tools/schematic_svg.py` | New, copied from the RLC project |
| `CRIG_session_handoff.md` | D24 updated, D28/D29 added, node/remote/protocol summary, O-5/O-17/O-18/O-19, remote firmware plan |

### 6. Notes / follow-ups
- The schematic is not bench-validated; ring resistor values depend on the SHOOT button choice (O-18). Drawn common-cathode; a common-anode ring needs + to 3V3 and active-LOW GPIOs.
- TP4056 has no power path: switch the remote off while charging. Flash via the dev board USB with SW1 off.
- Set the boost module to 5.0 V **before** connecting it to the dev board.
- Kept FR-R03 "SHOOT/TRIG must be inactive once after boot" as stuck-input protection, even without arming.
- Check the DevKitC-1U revision marking for the RGB LED pin (O-19).

## 2026-10-16 … 2026-10-19 — Stand-by tests (conclusive), S-line sniffer, battery protocol research

### 1. Goal
Repeat the inconclusive stand-by test with a charged battery, test a longer idle time, and prepare reverse-engineering of the EN-EL9a "S" contact with the ESP32.

### 2. Stand-by test, 30 s idle (2026-10-16)
- Same sequence as 2026-10-15 (`firmware/optotest` + scratchpad `standby2.py`), USB left untouched during the run, photos counted and EXIF-matched afterwards (DSC_4694–4703).
- Result: **FULL alone 0/2; HALF+FULL 6/6** (HALF 100+FULL 100, 300+300, 1000+500 ms). 500 ms FULL gave 2 frames (camera in continuous mode).
- Camera clock = PC − 1 h 00 min 15 s.
- Data: `docs/bench/standby-test-2026-10-16.tsv`.

### 3. Stand-by test, 10 min idle (2026-10-16)
- First attempt aborted: **with the camera's USB attached the screen stays on (no stand-by)**. Its shots DSC_4704–4706 are discarded.
- Rerun with camera USB unplugged (`standby10.py`: CAL, 600 s idle, HALF 100+FULL 100; 600 s idle, HALF 1000+FULL 500): 1 frame (DSC_4709) and 2 frames (DSC_4710/4711).
- Conclusion: the camera wakes and shoots from stand-by on the normal HALF→FULL press; a periodic keep-alive (D4, O-3) is not needed for gaps up to at least 10 min. One sample per variant; longer idle and the camera's auto-off setting not tested.
- Data: `docs/bench/standby-test-10min-2026-10-16.tsv`.

### 4. S-line sniffer `firmware/ssniff/` (2026-10-16)
- Listen-only capture of the EN-EL9a S contact: GPIO2 (ADC1_CH1), ADC continuous ≈80.6 kSa/s measured, 12 dB attenuation, curve-fitting calibration; burst = first sample ≥ threshold (400 mV, `+`/`-`) until 20 ms quiet, 5 ms pre-trigger, max 1 s (PSRAM buffer); idle stats every 5 s.
- Host tool `ssniff.py`: per run `captures/<date-time>/` with `burst_NNN.csv` (t_s, v_gpio, v_s), `bursts.txt` (levels, bit string, UART 8N1 decode in both polarities), `log.txt`; `--div` divider correction, `--decode` offline. Decoder verified on a synthetic signal.
- Wiring agreed with David:

```
camera S (J2 pin 2) --[10 kΩ]--+-- GPIO2
                               +--[96.8 kΩ]-- camera − (BLK) -- ESP32 GND
```
  Phase 1: bench supply, 96.8 kΩ kept (camera alone, `--div 0.906`). Phase 2: 96.8 kΩ removed, real EN-EL9a in (`--div 1`). ESP32 powered first; camera USB unplugged.
- Built, flashed to the node board (MAC 68:EE:8F:47:8E:C8, MAC checked first) and started; GPIO2 open reads 0 mV, no ADC overflow. One spurious burst right after boot (ADC start-up). **Not yet tested on the camera.** `~/claudecode/usb-devices.md` updated (board now runs ssniff).

### 5. Battery protocol research (2026-10-19)
- No public documentation of the Nikon battery protocol found; "fully decoded" dummies and clones exist (EN-EL14/15, EP-5 replacements for the EN-EL9(a)/D3000) but makers publish nothing.
- Only technical source: Gough Lui's EN-EL14 clone teardown (5 contacts, middle = data via divider; main IC possibly a Lattice FPGA, unconfirmed).
- D40 firmware 1.12 (2009) improved EN-EL9a charge reporting → the camera reads charge data from the battery; the changing payload might be measurement data rather than crypto (hypothesis, to test with the sniffer).
- Practical alternative: a third-party EP-5 coupler that works in the D3000 without error.

### 6. Files changed

| File | Change |
|---|---|
| `firmware/ssniff/` | New: `main/main.c`, `ssniff.py`, `README.md`, CMake files, `sdkconfig.defaults` |
| `docs/bench/standby-test-2026-10-16.tsv` | New: 30 s idle results |
| `docs/bench/standby-test-10min-2026-10-16.tsv` | New: 10 min idle results |
| `CRIG_session_handoff.md` | §6b: 30 s and 10 min results, USB-keeps-camera-awake rule, sniffer + research summary |
| `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md` | Sniffer follow-up and web research sections |
| `~/claudecode/usb-devices.md` | Node board now runs `firmware/ssniff` |
| `camera-rig-functional-spec.md` | v0.13 → v0.14 (2026-10-23): keep-alive optional and off by default (FR-N03, `t_keepalive_interval` = 0, §4, §6.3, §10, §11.3, T-03, T-31, O-3); §6.2 stand-by results; §10 USB keeps camera awake; J2 pin 2 / PW-01 / O-12 sniffer + research |
| `CRIG_session_handoff.md` | D4 and O-3 updated to match spec v0.14 |

### 7. Notes and next steps
- **Bench rule:** camera USB unplugged for any stand-by test; check that the camera is switched on before a run.
- Schematic still shows the obsolete 100 Ω RS on J2 pin 2 (spec fixed in v0.14).
- Next: solder the sniffer wiring, run phase 1 and 2 captures; optionally a 30–60 min idle test; consider buying an EP-5 coupler.
- Helper scripts `standby2.py` / `standby10.py` live in the session scratchpad only.
- The board now runs ssniff, not optotest: reflash `firmware/optotest` for opto tests.

## 2026-10-15 — First ESP32 trigger test, HALF/FULL mapping corrected, S-pin findings parked

### 1. Goal
Test the camera trigger through the ESP32 node, record what was learned about the EN-EL9a S contact, and bring documentation, schematic and firmware in line with the as-built wiring.

### 2. Results
- Trigger test through `firmware/bringup` and the new `firmware/optotest`: all three optocoupler stages work. A soldering fault on the FULL stage (U3) was repaired by David; before that the camera woke up (HALF) but could not take a picture.
- As built (bench-verified): J3 pin 1 orange = **FULL** = GPIO4/U3 → camera WHT; J3 pin 2 red = **HALF** = GPIO5/U4 → camera YEL; brown = GPIO6/U5 → GRN. Documentation and firmware had HALF/FULL the other way round. Wires were not re-soldered; documentation, schematic and firmware were changed.
- EN-EL9a S contact: ≈100 kΩ NTC-like (86 kΩ warm), 0 V DC, plus 9600 baud data bursts with a non-repeating payload. Not 98 Ω; a fixed 96.8 kΩ does not remove "battery cannot be used". Parked; camera stays on the original battery. See `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md` (incl. analysis tools).

### 3. Changes
- Spec v0.13: §6.2 connector table and as-built note, PW-01, O-12, GPIO assignment, revision table.
- `CRIG_session_handoff.md`: D17, D18, D26, O-12, new §6b bench status.
- `crig_node_schematic.svg/.png` v0.3: U3 = FULL (IO4), U4 = HALF (IO5), J3 labels, S/RS notes.
- `firmware/bringup`: `PIN_HALF` = 5, `PIN_FULL` = 4; command help shows pin numbers and pulse times; new `H` / `F` hold commands (until `d` or the arm window ends); pulse times lengthened (see handoff §6b).
- `firmware/bringup`: timing sweeps `s` (FULL duration 10–800 ms, HALF lead 1000 ms) and `S` (HALF lead 0–800 ms, FULL 500 ms), one attempt every 3 s, `d` aborts. Both firmwares now ignore input bursts (several characters at once): a terminal with echo on had sent the boot log back as commands and started a sweep unintentionally; the filter reported `input burst ignored` on the next boot.
- `firmware/optotest` (new): keys `h` (HALF), `f` (FULL), `p` (PWR) switch an optocoupler ON, Enter switches all OFF, auto-off after 30 s; host tool `optotest.py`.

- Stand-by test (optotest, FULL only vs HALF+FULL after 30 s idle): inconclusive; stale gvfs listing and then the camera dropped off USB (low battery, being charged). Details and a ready-to-repeat sequence in handoff §6b. Scripts: scratchpad `standby.py` / `standby2.py` (not kept in the project).

### 4. Notes
- Port handling: opening the ESP32-S3 native USB port with a normal terminal toggles DTR/RTS; use `term.py` / `optotest.py`. A helper that left echo on once sent the boot log back as commands and briefly armed the outputs.
- `idf.py flash` failed twice with a serial error while another program held the port; direct `esptool write-flash` worked once the port was free.

## 2026-10-07 — D3000 wiring measured, spec v0.5, Gotron order list

### 1. Goal
Review the CRIG handoff and spec, work out what to buy at Gotron.be, and process the measurements of the modified D3000 (wires soldered to focus/shutter switch, on/off switch and battery compartment).

### 2. Measurement results (D3000)

| Wire | Function | Measured |
|---|---|---|
| Black / Red | Battery − / + | |
| Blue | COMMON of focus + shutter; continuity with black | |
| Yellow | HALF | 4.71 V idle, 0 V half and full press; 0.12 mA short-circuit |
| White | FULL | 3.31 V idle and half press, 0 V full press; 0.064 mA short-circuit |
| Green | On/off | OFF: shorted to ground plane (0.5 Ω, 0 V). ON: open, ~4.72 V pull-up, 0.01 mA; 34.5 Ω to blue with battery out (unexplained) |
| EN-EL9a "S" contact | Not wired out | NTC to −: 98 Ω, 83.5 Ω when warmed, same in both polarities (**wrong unit:** it is kΩ; corrected 2026-10-15, re-measured 102 kΩ on 2026-11-03) |

- Camera shows "battery cannot be used" with only + and − connected (S open). Plan: fixed 100 Ω (1 %) between the camera's S terminal and − (**not yet tested**).
- A diode yellow(anode) → blue(cathode) triggers focus: camera low threshold > ~0.6 V; PC817 (Vce,sat ~0.1–0.2 V) will work.
- FULL requires HALF to stay closed (yellow stays 0 V at full press).

### 3. Decisions
- Option B (internal wired release) selected; O-1 and O-13 closed.
- Two PC817 per spec (HALF, FULL); 1 kΩ series, 10 kΩ pull-down per GPIO.
- Camera on/off via green line specified, default **off** (`cam_power_ctrl`). Short green to blue = camera off; open optocoupler = camera follows knob (fail-safe).

### 4. Files changed

| File | Change |
|---|---|
| `camera-rig-functional-spec.md` | v0.4 → v0.5: §6.2 measured wiring, new §6.3 camera power control, FR-N14…N17, STATUS bit, parameters `cam_power_ctrl`/`t_cam_boot`/`t_cam_off_delay`, tests T-40…T-45, PW-01 NTC finding, O-1/O-12/O-13/O-14, revision history |
| `CRIG_session_handoff.md` | D3, D17, D18, open items, new §6.2 measurement results |
| `gotron_bestellijst.md` | New: Gotron order list with prices and Gotron references (PC817, RB100E R1%, RC120E R1%, RC1K0 R1%, RC10K R1%, LS18650-3, BAT120, Z162A, Z2T5A, Z2T2,5A, VMA438, ADA1201, VMA321), total €60,80 (€65,08 with 4 extra cell holders). Charger title says "WPM321", product code is VMA321 |

### 5. Gotron findings
- Available: PC817, metal-film resistors, 18650 cell (3350 mAh, bare, 10 A), single 18650 holder, 5×20 mm chassis fuse holder, 5 A and 2.5 A slow fuses (10-packs, €2,89), OLED 0.96", vibration motor, TP4056 board.
- Not found: ESP32-S3-WROOM-1U, 2S BMS, 2S→3.3 V buck (Gotron's buck is 12–24 V input), GX12-4, XT30, TPS63020/BQ24074, switches and buttons.
- Cell is 3350 mAh, not 3500: pack ≈ 48 Wh (spec needs ≥ 44 Wh incl. margin, tight).
- Fuses: main 5 A slow, camera branch 2.5 A slow (selective coordination for T-16); 3 A does not exist at Gotron (3.15 A only in the 180-piece assortment, €16,49). Validate branch fuse with the camera current measurement (O-11).

### 5b. Node schematic (2026-10-08)
- Added `crig_node_schematic.svg` / `.png`: power (pack, BMS, F1, SW1, buck, F2 + XT30 camera supply, VBAT divider), ESP32-S3 U1, three PC817 stages (HALF, FULL, optional green on/off), J3 camera signal connector, camera-side reference (measured pull-ups, RS 100 Ω on S), status LED.
- Proposed GPIO map: IO1 VBAT sense, IO4 HALF, IO5 FULL, IO6 camera power, IO7 PAIR, IO15/16/17 RGB LED, IO0 BOOT, IO19/20 USB.
- BLU (COMMON) is the shared opto return and deliberately not tied to GND at the node; it meets BLK inside the camera.
- Generated by a script (scratchpad, not kept in the project); edit the SVG or regenerate if the design changes.
- Spec §12.1 references the schematic. Draft, not bench-validated.

### 5c. Dev board on 5 V (2026-10-08)
- Node uses the ESP32-S3-DevKitC-1 (same as the RLC), fed 5.0 V from the buck into 5V0; on-board LDO, EN network, BOOT/RESET and USB-C replace the discrete parts in the schematic (v0.2).
- Spec v0.6: §11.3 node logic row, power budget (+3.5 Wh: ~37.5 Wh worst case, ~49 Wh with margin vs. ~48 Wh pack — tight), RR-06 relaxed, O-16 added.
- Caveats: set and measure the MP1584 at 5.0 V first; no USB while the pack is on; buck input must stay above ~6.0 V.

### 5d. Status LED (2026-10-08)
- Status LED is now the dev board's on-board WS2812 (IO48 on v1.0, IO38 on v1.1): external RGB LED, three resistors and IO15–17 removed from the schematic; spec §9.3/§12.1 updated. Needs a light pipe/window out of the lens view; check the board revision.

### 5e. S wire (2026-10-08)
- S terminal gets a wire to the node; RS 100 Ω moves from the camera battery compartment to the node and returns to BLU. Camera cable = 7 conductors (RED, BLK, S, YEL, WHT, GRN, BLU). The S wire must not be blue (BLU = COMMON). Schematic and spec §6.2/PW-01 updated.

### 5f. Buzzer (2026-10-09)
- Buzzer fitted on the node (+ to GPIO15, − to GND) and planned on the remote. Drawn as BZ1 in the schematic. Overrides D6 / FR-R09 ("no buzzer on the remote"); spec v0.7: FR-R09 changed, FR-N18, O-17 (tone patterns distinct from RLC tones, silent during bursts, buzzer type and current).

### 5g. Remote use cases and active buzzer (2026-10-09)
- Active buzzer: 2 mA at 3.3 V measured, direct GPIO drive is fine; single pitch, patterns by rhythm only.
- Remote has two uses, both already in the architecture: (1) manual remote with an illuminated SHOOT button, (2) TRIG input from the RLC base (§8, GX12-4, opto) so the camera fires at every RLC launch.
- Spec v0.8: FR-R13 (ring green = ready, red otherwise; ready = link OK + node ARMED + no FAULT/LOCKOUT + batteries OK), §9.2/§12.2 updated, field procedures check the ring, O-18 (button/ring type and voltage, ready definition, drop the three LEDs?).

### 5h. PAIR switch removed (2026-10-09)
- SW3 and IO7 removed from the schematic; no pairing procedure. Units are bound at build time by MAC, PMK, LMK and channel in a git-ignored binding file; replacing a unit means re-flashing both. Spec v0.9 §7.4 rewritten; PAIRING state, PAIR_REQ/PAIR_ACK and the node PAIR button removed; handoff D23.

### 5i. Single fuse (2026-10-09)
- Camera-branch fuse F2 dropped: one fuse (2.5 A slow; 5 A if the camera peak current requires it) directly behind the BMS, then SW1, then camera and node buck in parallel. A camera short now also takes the node down. Spec v0.10 §11.3, §5.3, T-16; schematic and order list updated (one holder, one fuse pack, total €56,67).

### 5j. Camera connectors (2026-10-09)
- Two 2.54 mm plug-in connectors: J3 4-pin (orange HALF/IO4 = camera YEL, red FULL/IO5 = WHT, brown ON/OFF/IO6 = GRN, black COMMON = BLU) and J2 3-pin (red camera +, brown S, black −); wires are spliced at the camera. RS 100 Ω now sits on J2 pin 2 to BLU. Schematic redrawn, spec v0.11 §6.2 mapping table, order list updated. Brown and black occur on both connectors with different meanings; J2 contacts must handle ~1–1.5 A (JST-XH class, not plain Dupont).

### 5k. S wire colour (2026-10-09)
- S wire: brown on J2, blue on the camera side, same colour as BLU. Rule: tag both ends and check continuity (BLU to BLK ~0 Ω, S to BLK not). Spec v0.12, handoff D27, schematic note.

### 5l. First flash: bring-up test (2026-10-09)
- Board found: ESP32-S3 (QFN56, rev v0.2), 16 MB flash, 8 MB octal PSRAM (N16R8), MAC 68:ee:8f:47:8e:c8, native USB port, by-id `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_68:EE:8F:47:8E:C8-if00` (MAC-based by-id; changes if the chip is replaced).
- `firmware/bringup/` (ESP-IDF v5.4.1): serial-command test of on-board WS2812 (GPIO48 and GPIO38), buzzer IO15, VBAT_SENSE IO1, and the opto outputs IO4/5/6, which stay LOW until armed with `a` (30 s). Own RMT WS2812 driver (the `led_strip` component 3.x needs a newer IDF, 2.5.5 failed at runtime). Flashed and verified: boot, chip info, ADC, command handling, outputs refused when not armed.
- Gotcha: on the native USB port, `hard-reset` leaves the chip in download mode and normal terminals (minicom/screen/pyserial) toggle DTR/RTS into download mode. Start the app with `esptool --after watchdog-reset chip-id` and use `firmware/bringup/term.py`.
- IO1 floats (0.1–0.2 V, once 2.3 V at boot) because the divider is not connected yet.
- MAC and by-id registered in the shared device list `~/claudecode/usb-devices.md` (new section "CRIG camera rig") next to the RLC boards, so the right board is flashed.
- Open: confirm which LED pin lights (GPIO48 vs 38) and that the buzzer sounds.

### 6. Next steps
1. Test 100 Ω on the camera S terminal (O-12); fallbacks 82–150 Ω; optional Gotron NTC100.
2. Measure camera current (O-11/T-33) and meter-off / keep-alive (O-3).
3. Bench-test optocouplers (T-01, T-02).
4. Order parts (`gotron_bestellijst.md` plus the items from other shops).
5. Optional: set up the `ESP32-CameraRig` repo with `crig_protocol.h` and host tests (handoff §7).
6. RLC change (Appendix A) only after O-8 is resolved.

### 7. Notes
- Project directory is not a git repository; nothing committed.
- The 34.5 Ω reading on the green line in the ON position is unexplained; re-measure with the battery in and out.
- Camera power control must not cut power during card write-out (`t_cam_off_delay`, T-42).

## 2026-11-03 — EN-EL9a S-pin re-measured

David measured the S contact to − on the original EN-EL9a (out of the camera): **102 kΩ**. That confirms the 2026-10-15 correction (≈100 kΩ, NTC-like), not the 98 Ω recorded on 2026-10-07. Added the reading to `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md`, spec PW-01 and handoff D18; annotated the 2026-10-07 table row and marked the 100 Ω item in `gotron_bestellijst.md` as obsolete. Older history entries (spec revision 0.5, changelog sections from 7–9 Oct) are left as written.

## 2026-11-04 — CRIG firmware (node + remote)

1. **Firmware `firmware/crig`** (ESP-IDF 5.4.1, one project, node/remote images via Kconfig): encrypted ESP-NOW on channel 1, binding from `tools/gen_binding.py` (git-ignored `crig_binding.h`), MAC-checked `flash.sh`. Node: shutter FSM, PREPARE, pack monitor, LED, buzzer, console. Remote: SHOOT/TRIG/ARM, ARM decoder, link quality, readiness LEDs, motor, RGB, OLED, battery.
2. **Tests:** 30 host checks (`tests/host/run.sh`); HIL H01–H17 (`tests/hil/hil.py`) pass 3 runs in a row on both boards, driven through console `sim` hooks.
3. **Fixes found on the bench:** node → remote frames unacknowledged at first (cleared after a physical change, RSSI −32 → −8 dBm; `scan`/`bcast` diagnostics added); false remote battery cutoff while the motor ran (`gpio_get_level()` on an output-only pin always reads 0) — battery now read only with the motor off, 3 s cutoff persistence, 10 s to deep sleep.
4. **Spec v0.19:** fault bits NO_PACK/PREPARING, no-pack/no-cell rules, node LED pins, §14.5 HIL table, O-21 (untested/unimplemented).
5. **RLC feedback:** the RLC starts each new ARM pattern in its LOW phase (first 1 Hz pulse ~950 ms after a disarm). Accepted: the CRIG decoder already handles it (ARMED drops 300 ms after the last fast pulse; CONNECTED is held); host test added; T-27 and the hand-off doc updated.

**Notes:** HIL burst tests take real pictures if the camera is attached. The node read 8.2 V with USB connected — don't power it from the pack and USB at the same time.

## 2026-11-06 — RLC interface on the bench

1. **Setup:** owner fitted U4/U6 and the DIN socket on the remote's side and the cable to the RLC base (RLC fw 1.2.22). All four units on USB: RLC base `5B5E042156` (UART bridge), RLC remote native USB `AC:A7:…`, CRIG node UART bridge, **CRIG remote on its native USB port** (`68:EE:8F:47:96:D4`).
2. **Console on native USB:** `crig_console` also reads the USB-Serial/JTAG RX FIFO (LL, no driver); new `reboot` command; `flash.sh` and `hil.py` fall back to the native port (opened raw, never touching DTR/RTS).
3. **Bug found — ARM input bounced:** with the real base, the remote saw falling edges 100 ms apart (periods 100 + 900 ms at 1 Hz; raw interrupt count ~4 per pulse) — the PC817 turn-off edge. At 10 Hz (50/50 ms) the decoder would have seen 50 ms periods and **never decoded ARMED**, so TRIG would never have been accepted. Fix: ARM sampled every 1 ms by an esp_timer with a 3 ms debounce; the interrupt only counts raw edges (`aisr` in `status`).
4. **New HIL tests:** R01 (real idle ARM 1 Hz → CONNECTED, TRIG idle) and R02 (RLC base reset: no pulses during boot, 1 Hz within 2 s; = CRIG T-26 / RLC T-C06). `hil.py rlc` runs them.
5. **Results:** HIL H01–H17 + R01–R02 **19/19 pass**; CRIG host 30/30; RLC host 583 checks, 0 failures (incl. T-CAM01–10). RLC remote linked, IDLE, 0 missed pings after the base resets.
6. **Docs:** spec v0.20 (FR-R10 note, §14.5 R01/R02, O-21), handoff D36, README, `docs/RLC_change_request_CRIG.md` §9 (results for the RLC session), `~/claudecode/usb-devices.md` (RLC base by-id corrected, CRIG remote native port).

**Still needs a person:** arm and fire the RLC into an inert igniter tester (T-21/T-C01, T-22/T-C02, T-27/T-C07, T-C08), power-off with TRIG high (T-23), loading (T-24), shorts (T-25).

### 2026-11-06 (cont.) — arm-and-fire bench session

- Guided session, owner arming and firing the RLC into an inert igniter tester on CH1, all four consoles logged (`captures/2026-11-06-rlc-arm-fire.log`, git-ignored). **All pass:** T-21 ×2, T-22a, T-22b, T-27 (arm, arm timeout, key off), RLC T-C08 ×2. Key numbers: first 10 Hz pulse 22 ms after ARMED; TRIG HIGH at PRE_FIRE + 3000 ms, LOW at pulse end + 3000 ms; abort in PRE_FIRE drops TRIG in the same ms; first 1 Hz pulse 956 ms after disarm. The debounced ARM input showed clean 99–101 ms periods at 10 Hz.
- **Node fix:** an extra 300 ms wake pulse came straight after every burst (PREPARE still 1 while the remote waits ~0.3 s to see ARM drop). `end_burst()` now pushes the next wake pulse out by `t_prepare_keepalive`; H07 checks it. HIL 17/17 after the change.
- Not run: T-23 (power off with TRIG high), T-24, T-25, T-27 ERROR (RLC fault-injection build).
- **T-25 and T-23 (same day):** ARM short (base idle) → CRIG LOST at 2.49 s, recovers, base unaffected; TRIG short during a full fire sequence → base timing unchanged (3000 / 2000 / 1050 ms), CRIG burst pauses while shorted; base switched off mid-countdown with TRIG HIGH → CRIG stops at once, LOST at 2.48 s, no glitches, 1 Hz again after the reboot. All PASS. Log `captures/2026-11-06-rlc-shorts-poweroff.log`. Remaining: T-24 (scope) and T-27 ERROR (RLC fault-injection build).

### 2026-11-06 (cont.) — look-and-listen check

- Owner watched and listened while the consoles were logged. Passed: resting state, real SHOOT button (5 taps + 3 s hold, one burst per press, no bounce), link loss and recovery, RLC cable unplug and replug, remote cutoff and deep sleep.
- **Node buzzer silent:** wired to **GPIO7**, firmware (and the bring-up firmware) drove GPIO15. Firmware moved to GPIO7; spec FR-N18, node schematic SVG and PNG (regenerated with cairosvg) updated. Node boot beep 200 ms, link-up 2 × 100 ms (were 80 / 40 ms).
- **New:** remote beeps 2 × 100 ms when the RLC comes back after LOST (owner request). Console `beep <ms>` on both units, `reboot` on the node (HIL can now drive the node on its native USB port).
- **Deep sleep fix:** the OLED stayed lit in deep sleep (powered from 3V3) and the display task overwrote the shutdown message. Now the display task stops, `REMOTE BATT EMPTY` shows for 2 s, then display off and charge pump off before sleep. Rechecked: OLED dark.
- Readiness LEDs stay green while shooting: as designed (motor, RGB and OLED show shooting).
- Regression: host 30/30; HIL 16/16 with the node on its native port, then with both CRIG units back on their UART bridges the full suite H01–H17 + R01–R02 **19/19 pass**.
