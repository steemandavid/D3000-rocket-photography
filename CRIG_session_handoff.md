# CRIG — Session Handoff for Claude Code

**Project:** Rocket Liftoff Camera Rig (CRIG)
**Owner:** David Steeman (GitHub: `steemandavid`)
**Handoff date:** 2026-10-06 (last updated 2026-11-03: remote built and hardware-tested)
**Companion document:** `camera-rig-functional-spec.md` (v0.4) — the authoritative functional specification. This handoff explains *how we got there*, what was decided and rejected, what was learned, and what to do next. Where the two disagree, the spec wins, except for items in §6 below that are newer than v0.4.

---

## 1. Goal in one paragraph

Repurpose an old **Nikon D3000** DSLR (18–55 mm kit lens) into a remotely triggered camera rig that shoots **continuous bursts (~3 fps) of rocket liftoffs**. A **camera node** (ESP32) sits with the camera 5–10 m from the pad and presses the shutter; a **dedicated CRIG remote** (ESP32) controls it over ESP-NOW. When the owner's own **Remote Launch Controller (RLC)** is used for ignition, the RLC base supplies a wired, optically isolated trigger signal to the CRIG remote so bursts start automatically at T‑2 s. With third-party launch systems (e.g. Wilson F/X), the operator holds SHOOT on the CRIG remote manually.

## 2. Key facts established in the session

### 2.1 Nikon D3000

| Fact | Consequence |
|---|---|
| No wired remote terminal (no MC-DC2 / accessory port); USB/AV only | Wired triggering requires opening the camera (or a mechanical presser) |
| Only remote option is the ML-L3 infrared remote, which fires **single frames** in remote mode (no burst) and has a remote-mode timeout | **IR triggering rejected** |
| Tethered USB capture not supported (Nikon software / gphoto2) as far as known | USB triggering not pursued |
| Continuous mode ~3 fps; JPEG buffer long, RAW buffer short | Shoot JPEG Fine |
| Shutter button is two-stage (HALF = wake/meter, FULL = release) | Node drives REST / HALF / FULL |
| EN-EL9a battery: 2S Li-ion, 7.2–7.4 V nominal, 8.4 V full, 1080 mAh | Camera can be fed directly from a 2S Li-ion pack |
| iFixit guide shows the shutter button + on/off switch assembly connects via a **6-pin flex cable** | Candidate tap point for the wired release (option B); **no public pinout found** — must be measured (§6.1) |

Reference: <https://www.ifixit.com/Guide/Nikon+D3000+On-Off+Switch+and+Shutter+Button+Replacement/52160>

### 2.2 RLC (repo `steemandavid/ESP32-RocketLaunchController`)

Findings from reading the RLC source (firmware ~1.2.19, FSD v1.7x):

| Fact | Where |
|---|---|
| Hardware: ESP32-S3-DevKitC-1, ESP32-S3-WROOM-1 N16R8, both units; ESP-IDF v5.4.1 | `README.md`, FSD |
| ESP-NOW on **fixed Wi-Fi channel 11**, no LR mode, encrypted peers (PMK + LMK) | `components/rlc_common/include/rlc_config.h` (`WIFI_CHANNEL 11`), `rlc_espnow.c` |
| The **base** runs the countdown: `PRE_FIRE_DELAY_MS 5000`, then `FIRE_PULSE_DURATION_MS 1000`, `POST_FIRE_COOLDOWN_MS 2000` | `rlc_config.h` |
| Base FSM: BOOT → IDLE → ARMED → PRE_FIRE → FIRING → POST_FIRE → IDLE (+ ERROR); aborts from PRE_FIRE on dead-man timeout, stale heartbeat, comm degraded, key off, arm sense lost, continuity open | `components/rlc_base/src/rlc_base_fsm.c` |
| Fire pulse end event: `EVT_FIRE_PULSE_DONE` | `rlc_fsm_events.h`, `rlc_fire_timer.c` |
| The remote only mirrors the countdown locally; the base is authoritative | `rlc_remote_fsm.c` |
| Base spare GPIOs per FSD: **38, 39, 41** (digital only; all ADC1 pins used) | FSD pin table, `pin_config.h` |
| Caveats: FSD spare-pin lists have been wrong before (remote GPIO 2); on DevKitC-1 **v1.1** GPIO 38 drives the on-board RGB LED (RLC uses GPIO 48 → probably v1.0 board) | FSD v1.59 history |
| RLC arms only if link quality ≥ 70 % (≤ 30 % ping failures) — CRIG must never degrade the RLC link | FSD guard 10 |
| Base panel has no spare connector; base battery 3S 5000 mAh | FSD / README |

## 3. Decision log (chronological)

| # | Decision | Rationale / notes |
|---|---|---|
| D1 | Target **liftoff stills** with burst mode | 3 fps gives ~1–3 good liftoff frames per flight; start early, accept pre-ignition frames |
| D2 | ~~IR (ML-L3) trigger~~ → **rejected** | Remote mode = single frames, timeout, sunlight issues |
| D3 | Actuator: **internal wired release (option B) selected 2026-10-07** (was open vs. servo option A) | Wires soldered and measured (§6.2); common `shutter_set(REST/HALF/FULL)` abstraction stays in firmware; servo driver is now optional/stub |
| D4 | Manual focus; ~~HALF used as keep-alive~~ → **updated 2026-10-23: no keep-alive by default** | Bench 2026-10-16: the HALF lead of a SHOOT wakes the camera from standby (verified after 10 min); keep-alive kept as an option (`t_keepalive_interval` = 0 = off), spec v0.14 FR-N03 |
| D5 | **Hold-to-shoot**: remote streams SHOOT packets while active; node releases on timeout | Every failure mode (release, link loss, crash) ends the burst |
| D6 | Dedicated CRIG remote, physically/audibly distinct from the RLC remote; haptic, no buzzer | Avoid grabbing the wrong remote; avoid confusing tones |
| D7 | Explored: RLC sends ESP-NOW trigger packets / RLC GPIO wired to node | Superseded by D9 |
| D8 | **No pad matching** — camera may cover several pads | Any RLC launch triggers the burst |
| D9 | **CRIG remote is the node's only controller.** In RLC scenario, the CRIG remote sits next to the RLC base and gets a wired **TRIG** signal from a spare base GPIO | Minimal RLC change (GPIO + timers only), no RF coupling, systems stay separate |
| D10 | CRIG radio on **channel 1** (RLC fixed on 11) | No airtime interaction |
| D11 | No WAKE line from the RLC; the **node alone** handles camera wake | Saves a base pin; keep-alive is sufficient |
| D12 | ~~Power CRIG remote from RLC base~~ → **reverted**: remote has its **own battery** | Full galvanic isolation via optocoupler; RLC supplies no power |
| D13 | Node has a **2S2P 18650 pack (~50 Wh)** that powers **node + camera**; camera battery removed, leads soldered to camera battery terminals | Sized for a 12 h launch day, ~1 000 frames |
| D14 | Remote battery: **1 × 18650 ≥ 3 000 mAh**, USB-C charging | ~16 h at ~185 mA (spec v0.15 §11.2; the earlier ~24 h estimate assumed a buck-boost to 3.3 V) |
| D15 | *(Superseded by D31/D32.)* RLC interface connector: **GX12-4**: TRIG+, TRIG−, DET, DET_RET (DET pins bridged inside the base socket for cable detect) | Connector type not used for igniters |
| D16 | Trigger window in base: assert at PRE_FIRE + 3000 ms (T‑2), deassert at fire pulse end + 3000 ms; immediate deassert on any abort/ERROR/disarm; hard cap 10 s | See spec §8.4 and Appendix A |
| D17 | Camera interface (bench-verified 2026-10-15: HALF + FULL released the shutter; HALF = U4/GPIO5, FULL = U3/GPIO4; a soldering fault on the FULL stage was repaired): 2 × PC817 (HALF, FULL) on the D3000's measured low-side switch lines; FULL asserts both; optional 3rd on the green on/off line | Spec §6.2/§6.3; measurements in §6.2 below |
| D18 | EN-EL9a "S" contact: **corrected 2026-10-15** — it is not ~98 Ω but ≈100 kΩ at room temperature (NTC-like, 86 kΩ when warmed) and it also carries a 9600 baud data exchange; a fixed resistor (tried 96.8 kΩ) does NOT remove "battery cannot be used". Parked, see `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md`. Camera keeps running on the original Nikon battery for now. S wire must not be blue (BLU = COMMON) | The earlier 98 Ω / RS 100 Ω plan is invalid Re-measured 2026-11-03: 102 kΩ S to − (battery out of the camera). |
| D19 | Node uses the ESP32-S3-DevKitC-1 (same as the RLC) fed 5.0 V by the buck on its 5V0 pin | On-board LDO, EN network, BOOT/RESET, USB-C; budget +3.5 Wh (spec v0.6 §11.3); no USB while the pack is on; antenna O-16 |
| D20 | Status LED = on-board WS2812 (IO48 on v1.0, IO38 on v1.1) | No external LED; light pipe out of the lens view |
| D24 | *(Hardware superseded by D30: plain button + two LEDs; the green/red readiness meaning stays.)* Remote SHOOT button is an illuminated button with a bi-colour ring: green = rig ready (link OK, no FAULT/LOCKOUT, batteries OK; "node ARMED" dropped in v0.15), red otherwise (spec FR-R13); both use cases kept: manual remote and RLC TRIG | Spec v0.8; O-18 |
| D30 | **Remote as built and tested (2026-11-03):** plain SHOOT push button + separate green (IO10) / red (IO11) 3 mm LEDs; RGB LED fixed on GPIO48 like the RLC; power = 18650 → 1S protection strip on the cell → toggle switch in P+ → power-bank charger/5 V boost module (LX-LBCE1) → 5V0; charges only with the switch on, keeps running while charging, no light-load auto-off, 5 V starts by itself. Test firmware `firmware/remote_hwtest`: everything wired passes; J1 (TRIG) and IO6 (DET) not wired yet | Spec v0.16 §9.2, §11.2, §12.2; `crig_remote_schematic.svg` v0.2; board in `~/claudecode/usb-devices.md` (UART port `usb-1a86_USB_Single_Serial_5CCC062258-if00`, MAC 68:EE:8F:47:96:D4) |
| D31 | **RLC interface v0.17 (2026-11-03):** second optocoupler and signal **ARM** next to TRIG, still a 4-wire maximum (3 used: TRIG+ / ARM+ / COM−; DET loop dropped; connector see D32). Base GPIO39 = TRIG, GPIO41 = ARM (GPIO38 kept spare: on-board RGB on v1.1 boards). ARM heartbeat: none = not connected, 1 Hz = connected, 10 Hz = armed. TRIG accepted only while ARM = armed. RLC armed → remote sends PREPARE → node switches the camera on and wakes it (≥ 5 s before ignition) | Owner: an ARM signal from the RLC is very beneficial for camera wake/power-up; keep the cable at 4 wires. Spec v0.17 §8, App. A; hand-off `docs/RLC_change_request_CRIG.md`; O-8, O-20 |
| D32 | **Interface connector (2026-11-03): 5-pin DIN 180°** from Gotron instead of GX12-4: AD322 female chassis socket on RLC base and CRIG remote, AD302 male plug on both cable ends (≈ €5.60). Pinout 1 = TRIG+, 2 = COM− (centre), 3 = ARM+, 4/5 n.c.; straight-through cable | Cost (M12 ≈ €36, Hirschmann CA ≈ €24 rejected); not waterproof/no lock accepted: sheltered placement, unplug is a safe failure. AD323 (270°) rejected: needs a €4.87 Lumberg 270° plug. Spec v0.18 §8.2 |
| D33 | **TRIG after FIRING / LINK_LOST (2026-11-03, RLC review):** before FIRING every abort cuts TRIG at once; once FIRING is entered TRIG always ends via the 3 s post timer (pulse completed on link loss, cease-fire, key off, disarm, arm sense lost, ERROR, emergency safe); ARM stays 10 Hz while TRIG is HIGH, 1 Hz in IDLE/LINK_LOST/ERROR; new RLC hook `cam_trig_on_firing_enter()` + `fired` flag | Owner: keep shooting — the igniter may already have lit. Spec v0.18 §8.5, App. A; hand-off §8 |
| D34 | **Radio channels (2026-11-03):** RLC verified on channel 11 (`rlc_config.h` `WIFI_CHANNEL 11`, HT20, no LR); CRIG on **channel 1** (max. spacing, remote sits next to the base), fallback 6; rule: ≥ 5 channels from the RLC channel, re-check if the RLC channel changes | RLC link is safety-critical; CRIG tolerates congestion (hold-to-shoot). Spec v0.18 RR-02, RR-05, §7.5 |
| D35 | **Firmware v0.19 (2026-11-03):** `firmware/crig`, one ESP-IDF 5.4.1 project with node and remote images (Kconfig), encrypted ESP-NOW on channel 1, binding generated by `tools/gen_binding.py` (git-ignored), MAC-checked `flash.sh`. Tested automatically on both boards: 30 host checks plus HIL H01–H17, 3 runs all pass. Console test hooks (`sim …`) replace the button, TRIG, ARM and battery | Spec v0.19 §14.5, O-21; `firmware/crig/README.md` |
| D36 | **RLC interface on the bench (2026-11-06):** U4/U6 and the DIN socket fitted, RLC fw 1.2.22. The ARM input bounced (about 4 falling edges per pulse at the optocoupler turn-off), which would have kept the 10 Hz armed pattern from ever decoding; the remote now samples ARM every 1 ms with a 3 ms debounce. HIL R01–R02 pass against the real base; console input added on the native USB port | Spec v0.20 FR-R10, §14.5 |
| D37 | **Look-and-listen check (2026-11-06):** node buzzer is wired to GPIO7 (draft said GPIO15): firmware follows the wiring. Node beeps lengthened (boot 200 ms, link-up 2 × 100 ms); remote beeps 2 × 100 ms when the RLC comes back after LOST; OLED display + charge pump off before deep sleep. Readiness LEDs stay green while shooting (by design; shooting is shown by motor, RGB, OLED). Console `beep <ms>` on both units, `reboot` on the node | Spec v0.20 FR-N18, §14.5 |
| D38 | **Camera end to end (2026-11-06):** D3000 on its own battery, release cable J3 only (J2 stays disconnected while the camera battery is in: it is wired straight to the battery terminals, unregulated). 3.0 fps confirmed; an RLC launch gives 19 frames, ~6 before ignition; wakes from standby. Frames can be read from the card over USB (gvfs gphoto2). Open: node VBAT sense stuck at the ADC ceiling (O-22) | Spec v0.20 §14.5, O-22 |
| D39 | **Battery sense fixed (2026-11-06):** node R1/R2 were swapped (O-22 closed); the remote divider was on IO13, not IO1, so the 2026-11-03 battery pass was a floating pin. Both now read within ~1.5 % of the meter. USB alone back-feeds ~3.7 V onto the node +VBAT through the buck, so `v_no_supply` is 4.5 V (was 2.0 V). Lesson: check an ADC reading against a meter and with the input switched off before calling it a pass. Spec v0.21 |
| D28 | **No arming (2026-11-01):** ARM switch, TEST and MENU buttons and the three discrete LEDs removed from the remote; node has no DISARMED/ARMED states (IDLE instead); a short SHOOT tap is the test frame; config/calibration/test via the node's USB console | "It is not a launch controller; accidental triggering wastes a few shots." Spec v0.15 principle 8, §9.1, §9.2, §7.3 |
| D29 | Remote hardware (2026-11-01): ESP32-S3-DevKitC-1U N16R8 with external antenna; 18650 → protection → switch → 5 V boost → 5V0 (~16 h; parts as built in D30); on-board RGB LED = status; ring LEDs and buzzer (same part as node) direct from GPIOs; NPN only for the vibration motor | Spec v0.15 §11.2, §12.2; draft `crig_remote_schematic.svg` (generator `tools/remote_schematic.py`); supersedes D14's ~24 h / TPS63020+BQ24074 plan |
| D27 | The camera-side S wire is blue, the same colour as BLU (no other colour available): tag both ends and verify BLU has continuity to BLK, S does not | Spec v0.12 §6.2 |
| D26 | Camera connectors: J2 3-pin 2.54 mm (red +, brown S, black −) and J3 4-pin 2.54 mm (**corrected 2026-10-15 as built:** orange = FULL = GPIO4/U3 → camera WHT, red = HALF = GPIO5/U4 → camera YEL, brown ON/OFF = GPIO6/U5 → GRN, black COMMON → BLU), spliced to the camera wires; RS 100 Ω on J2 pin 2 to BLU (**obsolete**, S is ≈100 kΩ + data, see D18) | Spec v0.11 §6.2; colours repeat across connectors (different nets) |
| D25 | One pack fuse (2.5 A slow, raise to 5 A if O-11 needs it) directly behind the BMS, then main switch, then camera and node buck in parallel; camera-branch fuse dropped | A camera short now takes the node down too; spec v0.10 §11.3, T-16 |
| D23 | No PAIR switch and no pairing procedure: node and remote are bound at compile time by the other unit's MAC, PMK, LMK and channel (git-ignored binding file); a swapped unit means re-flashing both | Spec v0.9 §7.4; PAIR_REQ/PAIR_ACK and PAIRING state removed |
| D22 | Buzzer on node (GPIO15) and remote, overriding D6 ("no buzzer"); tones must stay distinct from RLC tones and silent during bursts | Spec v0.7 FR-R09, FR-N18, O-17 |
| D21 | Camera cable = 7 conductors: RED, BLK, S, YEL, WHT, GRN, BLU | GRN only if camera power control is used; BLU kept separate from BLK |

## 4. Current architecture (summary)

```
Scenario A (RLC):   RLC remote ──ch11──▶ RLC base ──TRIG+ARM (opto, DIN-5)──▶ CRIG remote ──ch1──▶ CRIG node ──▶ D3000
Scenario B (other): operator holds SHOOT on CRIG remote ───────────────────ch1──▶ CRIG node ──▶ D3000
```

- **Node:** ESP32-S3-DevKitC-1, state machine BOOT / IDLE / SHOOTING / LOCKOUT / TEST / CAL / FAULT (no arming since spec v0.15); actuator driver; pack monitoring; status LED; powers the camera through a 3 A fused output.
- **Remote (built 2026-11-03, spec v0.16):** ESP32-S3-DevKitC-1U N16R8, SHOOT push button (only control) + power toggle switch, green/red readiness LEDs, 0.96" OLED, on-board RGB status LED (IO48), vibration motor, buzzer, TRIG opto input + cable detect (not wired yet), 1S 18650 with protection strip and power-bank charger/boost module. Schematic `crig_remote_schematic.svg` v0.2 (as built).
- **Protocol:** ESP-NOW unicast, encrypted (PMK + LMK fixed at build time, peers bound by MAC), ≤ 32-byte frames with magic `0x47495243` ("CRIG"), version, type, seq, ack_seq, CRC-16/CCITT-FALSE. Messages (spec v0.15): HEARTBEAT, SHOOT / STATUS; TEST, CFG_*, JOG and PAIR_* removed. Details in spec §7.
- **Key timings:** heartbeat 5 Hz, SHOOT 20 Hz, `t_shoot_timeout` 300 ms, `t_max_burst` 15 s, `t_link_lost_disarm` 10 s, keep-alive every 60 s. Full parameter table in spec §13.

## 5. Open items (from spec v0.4, plus new)

| # | Item | Owner |
|---|---|---|
| O-1 | ~~Actuator choice~~ — closed: wired release (D3) | — |
| O-3 | Largely answered 2026-10-16 (wake from standby after 10 min works, keep-alive off by default); open: 30–60 min idle, auto-off setting | David |
| O-5 | ~~ESP32 variant~~ — closed v0.15: S3 on both units | — |
| O-6 | No frame-count feedback from the camera; remote shows an estimate | accepted |
| O-8 | Verify chosen RLC base spare GPIO physically + DevKitC-1 revision | David |
| O-9 | Location of the 5-pin DIN socket on the RLC base panel | David |
| O-11 | Measure D3000 current draw (standby, meters active, burst) and update the power budget | David |
| O-12 | EN-EL9a S contact re-measured 2026-10-15: ≈100 kΩ NTC-like plus 9600 baud data (not 98 Ω). 96.8 kΩ on S did not remove the error. **Parked**; findings in `docs/en-el9-s-pin/`. Powering the camera from the node is on hold; the camera runs on the original battery | David |
| O-13 | ~~Pinout of shutter/power flex~~ — closed: see §6.2 | — |
| O-18 | ~~SHOOT ring type~~ — closed 2026-11-03: plain button + two 3 mm LEDs | — |
| O-19 | ~~Remote board revision / RGB pin~~ — closed 2026-11-03: GPIO48, verified | — |
| O-17 (new) | Buzzer tone patterns (part settled: remote uses the node's 3.3 V active buzzer on IO15) | David / Claude |
| O-16 (new) | Node dev board antenna (PCB vs. u.FL -1U) and dev board current on 5 V | David |
| O-14 (new) | Remote camera on/off via the green line (spec §6.3): decide and specify | David / Claude |
| O-15 (new) | Measure camera current draw (T-33) and keep-alive vs. meter-off (O-3) on the bench with the now working supply | David |

Closed: O-2 (RLC channel = 11), O-4 (no club camera rules), O-7 (node pack powers camera), O-10 (no isolated DC-DC needed).

## 6. Items newer than spec v0.4 (to be merged into the spec)

### 6.1 Shutter flex pinout measurement (for option B)

No public pinout exists for the D3000's 6-pin shutter/on-off flex; the service manual is not public (sold by repair vendors). Procedure:

1. **Continuity, battery out, flex disconnected, on the button-assembly side:**
   - Find **COMMON**: the pin that connects to another pin for every switch action.
   - Half-press → one pin connects to COMMON = **HALF (SW1)**.
   - Full press → a second pin also connects = **FULL (SW2)**; HALF stays connected.
   - Toggle the on/off switch → one or two pins change = **POWER switch sense**.
   - Remaining pin(s): unused, extra ground or the other power-switch contact.
2. **Voltages, flex reconnected, camera powered:**
   - COMMON = 0 V and continuous with battery − (confirms ground).
   - HALF and FULL idle at ~3 V (pulled up), 0 V when pressed → optocoupler collector on the switch line, emitter on COMMON (as in spec §6.2).
3. **Soldering:** board-side connector is likely 0.5 mm pitch. Prefer larger pads where the flex meets the switch contacts, or board test points; otherwise 0.1–0.2 mm enamelled wire on the flex traces, anchored with UV glue or Kapton.
4. **Optional extension:** with the POWER switch lines identified, the node could switch the camera on/off remotely to save power between flights (not yet specified).

Add to spec: §6.2 (option B) and open item O-13. **Done in spec v0.5.**

### 6.2 Measurement results (2026-10-07)

Later in the same project: bring-up firmware `firmware/bringup/` (flashed to the first node board, MAC 68:ee:8f:47:8e:c8, registered in `~/claudecode/usb-devices.md`; see changelog 5l for the native-USB reset gotcha), draft node schematic `crig_node_schematic.svg`/`.png`, order list `gotron_bestellijst.md`, and decisions D19–D21 above (spec now v0.6).

Coloured wires were soldered to the shutter/focus switch, the on/off switch and the battery compartment:

| Wire | Function | Measured |
|---|---|---|
| Blue | COMMON of focus and shutter; continuity with black | |
| Yellow | HALF (focus) | 4.71 V idle, 0 V half and full press; 0.12 mA short-circuit |
| White | FULL (shutter) | 3.31 V idle and half press, 0 V full press; 0.064 mA short-circuit |
| Green | On/off | OFF: shorted to the ground plane (0.5 Ω to blue, 0 V). ON: open, ~4.72 V pulled up by the camera, 0.01 mA short-circuit, 34.5 Ω to blue with battery out (cause unknown) |
| Black / Red | Battery − / + | |

- A diode yellow(anode) → blue(cathode) triggers focus, so the camera low threshold is above ~0.6 V; a PC817 will work with a large margin (1 kΩ series, 10 kΩ pull-down on the GPIO).
- **Corrected 2026-10-15:** the EN-EL9a centre contact "S" measures ≈100 kΩ to − at room temperature (86 kΩ after warming; NTC-like), not 98 Ω. It also carries a 9600 baud exchange with the camera whose payload differs on every capture. With only + and − connected, or with a fixed 96.8 kΩ on S, the camera shows "battery cannot be used". Parked; see `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md`. (The earlier 98 Ω figure and the 100 Ω RS plan are superseded.)
- Green wire: shorting it to blue switches the camera off; an open optocoupler lets the knob decide. Third optocoupler and firmware (not yet specified): O-14.

## 6b. Bench status 2026-10-15

- ESP32-S3 (bring-up and `firmware/optotest`) drives the three optocouplers: HALF (GPIO5/U4 → camera yellow), FULL (GPIO4/U3 → camera white), PWR (GPIO6/U5 → camera green). The camera takes a picture with HALF + FULL asserted and releases the shutter when both end.
- HALF and FULL were documented the other way round in spec ≤ v0.12; fixed in spec v0.13, schematic v0.3 and the firmware. The wires at the connector were **not** changed.
- Powering the camera from the node is parked: the S contact of the EN-EL9a is ≈100 kΩ + a 9600 baud exchange, not 98 Ω (see D18 and `docs/en-el9-s-pin/`). The camera runs on the original battery; the node's ESP32 is not powered from the camera supply.
- `firmware/bringup` after the mapping fix: `a`, then `f` (HALF 1000 ms, then HALF+FULL 500 ms) takes one picture (verified 2026-10-15).
- Timing sweeps 2026-10-15 (`s`, `S`; manual focus, single frame, D3000): every attempt took a picture, down to FULL = 10 ms (one 10 ms RTOS tick, so 10–20 ms in practice) with HALF 1000 ms before it, and with HALF lead 0 ms (HALF and FULL asserted together) at FULL 500 ms. The camera is very tolerant of short pulses, so the real timings can be chosen with a wide margin; not yet tested: continuous release (burst) rate with FULL held, autofocus, camera waking from standby.
- Burst test 2026-10-15 (continuous release, FULL held with `F`, manual focus, 1/200 s, 3872x2592 JPEG): EXIF times (0.5 s resolution, `docs/bench/exif-times-2026-10-15.tsv`) show DSC_4590–4660 = 71 frames from 15:42:09.5 to 15:42:37.5, i.e. ≈2.5 frames/s on average; the first 11 frames took ≈3.5 s (≈3 fps), the last 11 ≈4 s (≈2.5 fps): a slight slow-down once the buffer is full, no stall. (Camera clock; EXIF resolution is only 0.5 s.)
- Stand-by test 2026-10-15 (`firmware/optotest`, 30 s idle, then FULL only / HALF 100 ms + FULL 100 ms / HALF 1000 + FULL 500 / HALF 300 + FULL 300, two rounds, pc-times logged): **inconclusive**. The camera's gvfs listing was stale (it showed 0 new photos), and after the run the camera lost USB (low-battery warning; battery being charged), so the EXIF times could not be matched to the log. Run to repeat once the battery is charged: log (pc time) 19:19:20 CAL, then A1 19:19:55, B1 19:20:28, F1 19:21:01, G1 19:21:36, A2 19:22:10, B2 19:22:43, F2 19:23:16, G2 19:23:51; camera clock is 1 h behind the pc. Tip: remount the camera (`gio mount -u gphoto2://<id>/` then `gio mount`) before every count, or read the SD card in a reader. Preliminary reading of the first run: FULL alone seems to work after standby, short HALF 100 + FULL 100 maybe not; **unconfirmed**.
- **Stand-by test repeated 2026-10-16 (new battery, conclusive):** same sequence, 30 s idle before every attempt, USB left untouched during the run, EXIF matched afterwards (camera clock = PC − 1 h 00 min 15 s; data in `docs/bench/standby-test-2026-10-16.tsv`). **FULL alone (500 ms): 0/2 pictures. Every HALF+FULL variant: 6/6**, including HALF 100 + FULL 100 ms. HALF+FULL 500 ms gave 2 frames each time (camera was in continuous mode), 100 and 300 ms gave 1 frame. Conclusion: HALF is required (FULL needs the HALF contact closed, cf. §6.2); a 100 ms HALF lead is enough even after 30 s idle; no separate wake pulse needed at this idle time. Not yet tested: idle times longer than the camera's auto meter-off/standby setting, and auto-off of the camera itself.
- **Long-idle test 2026-10-16 (10 min idle, conclusive for 10 min):** with camera USB attached the screen stays on (no stand-by), so the run was repeated with the camera's USB unplugged. After 600 s idle, HALF 100 + FULL 100 ms took 1 frame and HALF 1000 + FULL 500 ms took 2 frames (`docs/bench/standby-test-10min-2026-10-16.tsv`). The camera wakes and shoots from stand-by on the HALF lead alone: a periodic keep-alive (D4/O-3) is not needed for idle periods up to at least 10 min. One sample per variant; longer idle and the camera's auto-off timer setting not yet tested. **Bench rule: camera USB unplugged for any stand-by test.**
- **S-line sniffer 2026-10-16 (`firmware/ssniff/`, flashed to the node board, not yet tested on the camera):** listen-only capture of the EN-EL9a S contact on GPIO2 (ADC1, ≈80.6 kSa/s) via 10 kΩ; phase 1 with the 96.8 kΩ on J2 kept and the bench supply (camera alone), phase 2 with the 96.8 kΩ removed and the real EN-EL9a in. Web research 2026-10-19: no public protocol documentation exists (only commercial "decoded" clones); a third-party EP-5 coupler that works without error in the D3000 is the practical alternative. See `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md`.
- Timing is not the issue: the earlier "no picture" was the FULL-stage soldering fault. The bring-up pulse times were nevertheless lengthened (HALF 1000 ms, HALF 1000 + FULL 500 ms for `f`, PWR 3000 ms); the real timings (`t_prefire`, burst) are still to be tuned (§12 of the spec).

## 7. Suggested next steps for Claude Code

1. **Repo setup:** create a new repo (e.g. `ESP32-CameraRig`), mirroring the RLC's structure and tooling so both projects feel alike:
   - ESP-IDF **v5.4.1**, CMake project with two build targets (`build_node.sh`, `build_remote.sh`), selected by Kconfig (`CONFIG_CRIG_UNIT_NODE` / `CONFIG_CRIG_UNIT_REMOTE`), like `CONFIG_RLC_UNIT_BASE/REMOTE` in the RLC.
   - Components: `crig_common` (protocol, ESP-NOW, compile-time peer binding, NVS, CRC, config), `crig_node` (FSM, actuator, power monitor, LED), `crig_remote` (FSM, inputs incl. TRIG/DET, OLED UI, haptics, battery).
   - Put `camera-rig-functional-spec.md` and this handoff in the repo root (or `docs/`).
2. **Protocol first:** implement `crig_protocol.h` (packet struct, message types, CRC-16) with host-side unit tests.
3. **Node firmware:** FSM per spec §9.1 with the failsafes of §5.3; actuator abstraction with a stub driver; keep-alive; pack-voltage ADC.
4. **Remote firmware:** SHOOT = button OR TRIG (debounced, opto active-low with pull-up); FR-R03 "must be inactive once" gating; single OLED page with warnings; RGB status LED (FR-R14). No ARM/TEST/MENU (v0.15).
5. **Bench tests** T-01 … T-16 (spec §14.1), then T-31/T-32 endurance.
6. **RLC change** (Appendix A of the spec) — in the RLC repo, as a separate, reviewed change: `rlc_cam_trig.c/.h`, FSM hooks, pin + config constants, RLC-side tests T-21 … T-25 plus a fire-path regression run. Do **not** start this before O-8 is resolved.

### Conventions to keep

- Metric units only.
- Documentation in Markdown.
- Safety-first style as in the RLC: fail-safe defaults (pull-downs, inactive-on-open inputs), no blocking calls in safety paths, every failure ends in REST.
- The CRIG must never be able to influence ignition; the RLC interface stays output-only from the base, optically isolated, no power drawn from the RLC.

## 8. Sources consulted

- iFixit — Nikon D3000 On/Off Switch and Shutter Button Replacement: <https://www.ifixit.com/Guide/Nikon+D3000+On-Off+Switch+and+Shutter+Button+Replacement/52160>
- Nikon support — remote release accessory compatibility: <https://www.nikonimgsupport.com/eu/BV_article?lang=en_GB&articleNo=000046677>
- The Photography Forum — D3000 shutter release cable: <https://www.thephotoforum.com/threads/nikon-d3000-shutter-release-cable.187596/>
- Nikon D3000 repair manual (commercial): <https://uscamera.com/product/nikon-d3000-repair-manual/>
- RLC repository: <https://github.com/steemandavid/ESP32-RocketLaunchController>
