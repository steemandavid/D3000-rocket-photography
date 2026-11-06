# Rocket Liftoff Camera Rig — Functional Specification

| | |
|---|---|
| **Document** | Camera Rig (CRIG) — Functional Specification |
| **Version** | 0.20 (draft) |
| **Date** | 2026-11-03 |
| **Author** | David Steeman |
| **Status** | Draft — wired release (option B) selected; camera power validation open (O-12); remote simplified (no arming, SHOOT only) |

---

## 1. Purpose and scope

This document specifies a wireless trigger system for a Nikon D3000 DSLR used to photograph rocket liftoffs. The system consists of a **camera node** mounted with the camera near the launch pad and a **dedicated CRIG remote**. The CRIG remote is the only unit that ever controls the camera node.

Two operating scenarios are supported:

- **Scenario A — RLC launch (automatic):** motors are ignited with the Remote Launch Controller (RLC). The CRIG remote sits next to the RLC base unit at the pad and receives an optically isolated wired trigger signal from the base during the countdown. No operator action is needed to take the pictures.
- **Scenario B — Third-party launch system (manual):** motors are ignited with another launch system (e.g. Wilson F/X). The operator holds the CRIG remote and presses SHOOT at "2" in the countdown, releasing it after liftoff.

Both CRIG units are battery powered from their own packs, sized for a full launch day (§11). The camera node's pack also powers the camera, whose own battery is removed.

In scope:

- Camera node hardware and firmware (radio, state machine, actuator interface, power for node and camera)
- CRIG remote hardware and firmware (controls, display, radio, wired trigger input, power)
- Radio protocol and the compile-time peer binding
- The wired trigger interface between the RLC base and the CRIG remote, and the changes required in the RLC (Appendix A)
- Safety behaviour, failsafes and test plan

Out of scope:

- The final choice of shutter actuator (servo/linear actuator vs. internal wired release) — both are supported through a common actuator interface (§6)
- Any modification of third-party or club-owned launch equipment

## 2. Definitions

| Term | Meaning |
|---|---|
| **CRIG** | Camera Rig system (node + remote) |
| **Node** | Camera-side ESP32 unit that operates the shutter and powers the camera |
| **Remote** | CRIG handheld ESP32 unit; controls the node in both scenarios |
| **RLC** | Existing Remote Launch Controller (base unit at the pad + handheld RLC remote) |
| **RLC base** | RLC pad unit; runs the pre-fire countdown and fires the igniters |
| **RLC interface** | Wired, optically isolated 4-wire connection RLC base → CRIG remote carrying TRIG and ARM (§8) |
| **TRIG** | Trigger signal from the RLC base; while active and accepted (§8.6), the remote behaves as if SHOOT were held |
| **ARM** | Heartbeat pulse pattern from the RLC base: none = not connected, 1 Hz = connected, 10 Hz = armed (§8.3) |
| **PREPARE** | Flag from remote to node while the RLC is armed: switch the camera on and wake it (FR-N19) |
| **HALF** | Shutter button half-pressed (camera wakes, meters) |
| **FULL** | Shutter button fully pressed (exposure; continuous in burst mode) |
| **REST** | Shutter button released |
| **Burst** | Continuous shooting while FULL is held (D3000: ~3 fps) |
| **Keep-alive** | Optional short periodic HALF pulse that keeps the camera out of standby; **off by default** since the HALF lead of a SHOOT wakes the camera from standby (FR-N03) |
| **Launch day** | Design basis for battery sizing: 12 h powered on, 40 recorded flights (§11.1) |

## 3. System overview

### 3.1 Scenario A — RLC launch

```
 ┌── Flight line ──┐         ┌───────────────────────── Pad area ─────────────────────────┐
 │                 │         │                                                            │
 │  [RLC remote] ──┼─ ch 11 ─┼─▶ [RLC base] ── igniters                                   │
 │                 │ ESP-NOW │       │                                                    │
 └─────────────────┘         │       │ RLC interface cable (TRIG, ARM, COM), < 1 m        │
                             │       ▼   optocoupler — no galvanic connection             │
                             │  [CRIG remote] ── ESP-NOW ch 1 ──▶ [CRIG node] ── D3000    │
                             │   (own battery)                     (pack powers both)     │
                             └────────────────────────────────────────────────────────────┘
```

### 3.2 Scenario B — Third-party launch system

```
 ┌───── Flight line ─────┐                    ┌────────── Pad area ──────────┐
 │                       │                    │                              │
 │  [3rd-party launcher] ┼── (own system) ────┼──▶ igniter                   │
 │                       │                    │                              │
 │  [CRIG remote] ───────┼── ESP-NOW ch 1 ────┼──▶ [CRIG node] ── D3000      │
 │   (handheld)          │                    │                              │
 └───────────────────────┘                    └──────────────────────────────┘
```

### 3.3 Design principles

1. **One controller** — the node only ever accepts commands from its bound CRIG remote (§7.4). The node is unaware of which scenario is in use.
2. **Launch system integrity** — the CRIG can never influence ignition. The RLC interface is strictly one-way (RLC → CRIG), uses no radio coupling, draws no power from the RLC, and is galvanically isolated by an optocoupler in the CRIG remote.
3. **Separate radio channels** — the CRIG operates on a different ESP-NOW channel from the RLC (default 1 vs. RLC fixed channel 11).
4. **Hold-to-shoot** — the burst lasts only while SHOOT is held (button or TRIG) *and* the link is alive. Every failure mode results in the shutter being released.
5. **Distinct by design** — the CRIG remote must not be confusable with the RLC remote, physically or audibly.
6. **Actuator-agnostic** — firmware drives an abstract REST/HALF/FULL interface; the physical actuator is a build option.
7. **Self-powered for a full day** — each CRIG unit runs a full launch day on its own pack without recharging.
8. **No arming** — the CRIG is not a launch controller; an accidental trigger only costs a few frames. There is no ARM switch and no ARMED state (v0.15); the RLC's ARM signal (§8.3, v0.17) only prepares the camera and validates TRIG. Safety for the launch rests entirely on the one-way, isolated RLC interface (principle 2); hold-to-shoot (principle 4) protects the frames and the batteries.

## 4. Operational concept

### 4.1 Common camera setup

1. Mount camera and node 5–10 m from the pad(s), upwind, portrait orientation. The camera may cover several pads at once.
2. Connect the camera power lead to the node. Configure the camera (§10). Prefocus manually on the launch rail(s).
3. Switch on the node (this also powers the camera), then switch on the camera.

### 4.2 Scenario A — RLC launch

1. Switch on the CRIG remote and verify the link to the node (status LED slow green, §9.2).
2. Connect the CRIG remote to the RLC base with the RLC interface cable. The remote shows "RLC" once the base heartbeat is seen (§8.3).
3. **Tap SHOOT** briefly on the CRIG remote (1–2 frames); check framing and exposure on the camera LCD.
4. Check that the green status LED at the SHOOT button is on (rig ready, FR-R13); do not leave while the red one is on.
5. Place the CRIG remote next to the base, protected from exhaust and weather. Retreat to the flight line.
6. Launch normally with the RLC. When the RLC is armed, the CRIG switches the camera on (if camera power control is enabled) and wakes it. The RLC base activates TRIG from T‑2 s until 3 s after the fire pulse; the camera bursts automatically. Any RLC abort ends the burst immediately.
7. Between flights nothing needs to be done at the CRIG.
8. At the end of the session: switch off the CRIG remote and node. Switching off the RLC base does not switch off the CRIG.

### 4.3 Scenario B — Third-party launch system

1. Switch on the CRIG remote. Verify link.
2. **Tap SHOOT** briefly (1–2 frames); check framing and exposure on the camera LCD.
3. Retreat to the flight line and check that the green status LED is on.
4. During the launch control officer's countdown: at "2", **press and hold SHOOT**.
5. Once liftoff has been observed and the rocket has left the frame, **release SHOOT**.
6. At the end of the session: switch off the CRIG remote and node.

### 4.4 Timing budget

| Item | Value |
|---|---|
| RLC armed → camera prepared | ARM decode ≈ 0.3 s, + `t_cam_boot` if the camera was off; ≥ 5 s available before ignition |
| TRIG edge or button press → remote SHOOT packet | ≤ 50 ms (20 Hz send rate) |
| Remote → node receive | < 20 ms |
| Node actuator REST/HALF → FULL | Servo ~20–30 ms, wired < 1 ms |
| D3000 shutter lag (MF, camera awake) | ~100 ms |
| Frame interval (continuous) | ~330 ms (~3 fps) |
| Burst from T‑2 s | ~6 frames before ignition, ~3 frames of liftoff |
| Scenario A total burst (T‑2 s → fire pulse end + 3 s) | ~6 s, ~18 frames |

## 5. Functional requirements

### 5.1 Node

| ID | Requirement |
|---|---|
| FR-N01 | The node shall drive the shutter through three states: REST, HALF, FULL. |
| FR-N02 | The node shall power up in IDLE with the actuator in REST. There is no arming step (v0.15): a valid SHOOT packet from the bound remote is the only condition for shooting. |
| FR-N03 | The camera is woken from standby by the HALF phase (`t_prefire`) of every SHOOT or test cycle; no separate wake mechanism is required (bench-verified 2026-10-16: HALF 100 ms + FULL 100 ms took a picture after 10 min of standby). Optionally, if `t_keepalive_interval` > 0, the node shall issue a keep-alive HALF pulse of `t_keepalive_pulse` every `t_keepalive_interval` while the link is up (FR-N08), and one immediately when the link comes up. Default: keep-alive off. |
| FR-N04 | On receipt of a valid SHOOT packet while IDLE, the node shall go to HALF for `t_prefire`, then to FULL, and hold FULL while SHOOT packets keep arriving. |
| FR-N05 | The node shall return to REST if no SHOOT packet has been received for `t_shoot_timeout`. |
| FR-N06 | The node shall return to REST after `t_max_burst` of continuous FULL, and enter LOCKOUT until a non-SHOOT packet has been received. |
| FR-N07 | On a `test` command on the node's USB console while IDLE, the node shall perform one HALF → FULL → REST cycle with FULL held for `t_test_full`. (Since v0.15 the remote has no TEST button and the TEST packet is removed; a short tap on SHOOT serves as the field test.) |
| FR-N08 | The node shall consider the link lost if no valid packet has been received for `t_link_lost`, and show this on its status LED (§9.3). Link state does not gate shooting: a valid SHOOT packet is itself proof of a link. |
| FR-N09 | The node shall reply to every valid received packet with a STATUS packet. |
| FR-N10 | The node shall measure the pack voltage and report it in STATUS. Below `v_node_warn` it shall set the low-battery warning flag; below `v_node_cutoff` it shall enter FAULT (REST; SHOOT and test cycles refused). A reading below `v_no_supply` (2.0 V) means no pack is connected (bench / USB power): the node then sets the NO_PACK flag and applies no cutoff (v0.19). |
| FR-N11 | The node shall ignore any packet that fails MAC, magic, version or CRC checks, without reply. |
| FR-N12 | The node shall indicate its state on a local status LED (§9.3). |
| FR-N13 | The node shall supply the camera from its pack through the single pack fuse and main switch (§11.3). |
| FR-N14 | If `cam_power_ctrl` is on, the node shall switch the camera on and off through the green line per §6.3. If it is off (default), the green optocoupler shall never be driven. |
| FR-N15 | After switching the camera on, the node shall not act on SHOOT nor perform a test cycle until `t_cam_boot` has elapsed. |
| FR-N16 | The node shall not switch the camera off within `t_cam_off_delay` of the last FULL, nor while the shutter is not in REST. |
| FR-N17 | The node shall report the camera power state in STATUS. |
| FR-N19 | When `prepare` changes from 0 to 1, the node shall prepare the camera: if `cam_power_ctrl` is on and the camera is off, switch it on (FR-N14, FR-N15); then, once the camera is ready, give one HALF pulse of `t_prepare_wake`. While `prepare` stays 1 and the node is IDLE, it shall repeat a HALF pulse every `t_prepare_keepalive`. `prepare` does not gate SHOOT. When `prepare` returns to 0 or the link is lost, the node stops preparing; camera power then follows §6.3. |
| FR-N18 | The node has a buzzer (GPIO7, + to GPIO7, − to GND; as wired, found 2026-11-06 — the draft schematic said GPIO15) for local state and fault indication. Tone patterns are to be defined (O-17) and shall differ from RLC and other launch system tones. The buzzer shall never sound while the shutter is not in REST. |

### 5.2 Remote

| ID | Requirement |
|---|---|
| FR-R01 | The remote shall send HEARTBEAT packets at `f_heartbeat` while SHOOT is not active. |
| FR-R02 | SHOOT is active when the SHOOT button is pressed **or** TRIG is accepted (asserted while ARM decodes as armed, §8.6). While SHOOT is active, the remote shall send SHOOT packets at `f_shoot` instead of HEARTBEAT. |
| FR-R03 | SHOOT packets shall only be sent if SHOOT became active *after* the link was established. A button held or TRIG asserted during boot or link recovery shall be ignored until it has been inactive once, so a stuck input cannot start a burst on its own. |
| FR-R04 | *Removed in v0.15 (no ARM switch).* |
| FR-R05 | *Removed in v0.15 (no TEST button; a short tap on SHOOT takes 1–2 test frames).* |
| FR-R06 | The remote shall display link status, node state, node pack voltage and warning, remote battery, RSSI and burst elapsed time (§9.2). |
| FR-R07 | The remote shall compute link quality as the percentage of packets acknowledged by STATUS over the last 2 s. |
| FR-R08 | The remote shall show LOCKOUT and require SHOOT to become inactive when the node reports it. |
| FR-R09 | The remote shall provide haptic feedback (vibration) while shooting. A buzzer is fitted (2026-10-09, supersedes the v0.6 "no buzzer" rule); its tones shall be clearly distinct from RLC and other launch system tones, and shall not sound during a burst (O-17). |
| FR-R10 | The TRIG and ARM inputs shall each be read through an optocoupler (TRIG → IO5, ARM → IO6). TRIG is accepted only while ARM decodes as armed (§8.6). The TRIG input shall be read active when the optocoupler conducts, with a pull-up so that an open or disconnected input reads **inactive**. It shall be debounced with `t_trig_debounce`. *Implementation (v0.20):* the ARM input is sampled every 1 ms and a pulse start is a high → low change that holds for 3 ms; an edge interrupt is not used, because the optocoupler's slow turn-off produced extra falling edges at each pulse end (bench, 2026-11-06). |
| FR-R11 | The remote shall run from its own battery through its power switch. It shall measure the battery voltage, warn below `v_remote_warn` and shut down cleanly below `v_remote_cutoff`. As implemented (v0.19): below `v_remote_no_cell` (2.5 V) no cell is connected (switch off / USB power), with no warning or cutoff; a change between cell and no cell is accepted only after 2 s; the battery is measured only while the vibration motor is off (and 300 ms after); cutoff needs `t_batt_cutoff` (3 s) continuously below `v_remote_cutoff` and blocks SHOOT; deep sleep follows `t_batt_shutdown` (10 s) later. |
| FR-R12 | The remote shall decode the ARM line (§8.3) and show the RLC status NONE / CONNECTED / ARMED / LOST (§8.6), together with the active trigger source (BUTTON / TRIG). LOST shall be signalled as a warning. |
| FR-R13 | The remote has a readiness indicator at the SHOOT button: **green** when the rig is ready and **red** in every other case (as built: two separate 3 mm LEDs, green IO10 / red IO11; originally an LED ring in the button). Ready = link to the node alive (link quality above a threshold), no FAULT or LOCKOUT, and node and remote batteries above their cutoff. The indicator shall be red at power-up and whenever status is unknown or stale (no STATUS within `t_status_stale`, default 1 s). It shows readiness for both trigger sources (button and TRIG). The LEDs are driven directly from two GPIOs through series resistors (§12.2). Once the remote is in RLC mode (RLC status ≠ NONE, §8.6), ready additionally requires RLC status CONNECTED or ARMED. |
| FR-R15 | While ARM decodes as armed, the remote shall set `prepare` = 1 in every HEARTBEAT and SHOOT packet; otherwise 0. |
| FR-R14 | The remote shall show its overall status (link, shooting, warnings) on the on-board RGB LED of its dev board (§9.2). There are no discrete status LEDs (v0.15). |

### 5.3 Safety and failsafe summary

| Event | Resulting behaviour |
|---|---|
| Operator releases SHOOT / TRIG deasserted | REST within `t_shoot_timeout` (normally the next packet: HEARTBEAT) |
| RLC aborts the countdown | TRIG deasserted immediately by the base → REST as above |
| RLC interface cable pulled | ARM heartbeat lost → TRIG ignored within ≈ 0.3 s, "RLC LOST" warning → REST within `t_shoot_timeout` |
| RLC base switched off or crashes | ARM pattern stops → TRIG ignored within ≈ 0.3 s (even if stuck asserted) → REST within `t_shoot_timeout`; "RLC LOST"; CRIG stays powered |
| Remote powered off / dropped / brownout / battery empty | REST within `t_shoot_timeout`; node shows NO LINK after `t_link_lost` |
| Radio link lost | REST within `t_shoot_timeout`; NO LINK shown after `t_link_lost` |
| SHOOT held / TRIG stuck too long | REST after `t_max_burst`, LOCKOUT until inactive |
| Node reset / brownout / watchdog | Boots to IDLE, REST; shoots again only on new SHOOT packets |
| Node pack low | Warning at `v_node_warn`; FAULT and REST at `v_node_cutoff`; BMS disconnects the pack before cell damage |
| Short in the camera lead | The single pack fuse opens; the whole node and the camera lose power (no selectivity) |
| Accidental SHOOT (button bumped, wrong moment) | A few wasted frames; burst ends on release or at `t_max_burst`. Accepted (principle 8) |
| Foreign or corrupt packet | Dropped silently |
| Any CRIG fault | No path back into the RLC: one-way, optically isolated interface, no power drawn from the RLC (§8) |

## 6. Actuator interface

Firmware exposes a single abstraction:

```c
typedef enum { SHUTTER_REST, SHUTTER_HALF, SHUTTER_FULL } shutter_t;
void shutter_set(shutter_t s);
```

The actuator type is selected by the `actuator_type` configuration parameter.

### 6.1 Option A — Servo or micro linear actuator (non-invasive)

- 50 Hz PWM from the ESP32 LEDC peripheral; three calibrated pulse widths `servo_us_rest`, `servo_us_half`, `servo_us_full` stored in NVS.
- A compliant element (spring, dense foam or silicone pad) between actuator and button, so positions translate into pressing force rather than exact travel.
- PWM detached in REST if `servo_detach_at_rest` is set; always driven in HALF and FULL.
- Dedicated 5 V buck regulator for the servo, shared ground with the ESP32, ≥ 470 µF bulk capacitor at the servo (§11.3).
- Mounted on a bracket fixed to the camera's tripod screw, so the camera can be removed without losing alignment.

**Calibration procedure** (via the node's USB serial console):

1. Jog the pulse width in 5 µs steps from REST until the camera wakes → `us_half_threshold`.
2. Continue until the shutter fires → `us_full_threshold`.
3. Set `servo_us_half` midway between REST and `us_full_threshold`.
4. Set `servo_us_full` 20–30 % beyond `us_full_threshold`.
5. Verify with 20 test cycles and 5 × 5 s bursts. Re-check after every remount.

### 6.2 Option B — Internal wired release (invasive) — **selected**

- Wires soldered inside the camera (done, 2026-10-07), routed out with the camera power lead (§11.3) in a single cable to the node.
- Two optocouplers (e.g. PC817), one across HALF–COMMON, one across FULL–COMMON.
- FULL asserts both optocouplers (confirmed: HALF stays at 0 V during a full press). The node sequences HALF first, then FULL after `t_prefire`. FULL alone (without HALF) never takes a picture (bench 2026-10-16, 0/2 after 30 s idle); every HALF+FULL combination did, also from standby: HALF 100 + FULL 100 ms, 300 + 300 ms and 1000 + 500 ms after 30 s idle (6/6), HALF 100 + FULL 100 ms and 1000 + 500 ms after 10 min idle (2/2). With continuous release mode, FULL held for 500 ms gives 2 frames. Data: `docs/bench/standby-test-2026-10-16.tsv`, `docs/bench/standby-test-10min-2026-10-16.tsv`.
- Optocoupler inputs pulled to off by default, so the shutter is released while the ESP32 is in reset.
- Warning: the flash capacitor may hold a hazardous charge with the battery removed; discharge before working in the top cover.

**Measured wiring (D3000, 2026-10-07):**

| Wire | Function | Measured behaviour |
|---|---|---|
| Black | Battery − | Ground |
| Red | Battery + | |
| Blue | COMMON of focus and shutter switch | Continuity with black (battery −) |
| Yellow | HALF (focus, half press) | 4.71 V idle, 0 V when half or fully pressed; short-circuit current 0.12 mA |
| White | FULL (shutter, full press) | 3.31 V idle and when half pressed, 0 V when fully pressed; short-circuit current 0.064 mA |
| Green | Camera on/off switch | See §6.3 |
| S wire (camera side: **blue**, the same colour as BLU/COMMON; see the identification rule below) | EN-EL9a "S" terminal in the battery compartment | ≈100 kΩ to − (NTC-like) in the original battery, plus a 9600 baud data exchange with the camera; corrected 2026-10-15 (§11.3 PW-01) |

Camera cable: **7 conductors on two polarised 2.54 mm plug-in connectors**; the camera-side wires are spliced to the connector wires (the camera-side colours differ from the connector colours):

| Connector | Pin | Connector wire | Camera-side wire | Function / node signal |
|---|---|---|---|---|
| J3 (4-pin, signals) | 1 | orange | WHT | FULL (shutter) — GPIO4 via U3 |
| J3 | 2 | red | YEL | HALF (focus) — GPIO5 via U4 |
| J3 | 3 | brown | GRN | Camera on/off — GPIO6 via U5 (optional, §6.3) |
| J3 | 4 | black | BLU | COMMON of focus/shutter (opto return, **not** the pack ground) |
| J2 (3-pin, power) | 1 | red | RED | Camera supply + (switched +VBAT) |
| J2 | 2 | brown | S wire (blue, same colour as BLU) | S terminal; **not used by the node**. Bench only: S-line sniffer `firmware/ssniff` (S → 10 kΩ → GPIO2, listen only, §11.3 PW-01). The RS 100 Ω plan is obsolete |
| J2 | 3 | black | BLK | Pack ground / camera − |

Colours repeat across the connectors (brown: J3 pin 3 = ON/OFF vs. J2 pin 2 = S; black: J3 pin 4 = COMMON vs. J2 pin 3 = ground) but the nets differ; the two connectors must not be interchangeable (different pin count, keyed housings). J2 carries the camera supply current (about 1–1.5 A peak, to be measured, O-11); the contacts must be rated for it (e.g. JST-XH 3 A per pin, not plain Dupont). GRN/J3 pin 3 is only needed if §6.3 is used.

**Blue wires:** two camera-side wires are blue (BLU = COMMON and the S wire) because no other colour was available. Tag both ends of each (heat-shrink label or tape) and verify before connecting: with the node end disconnected, BLU has continuity (~0 Ω) to BLK (black), the S wire does not. J3 pin 4 and J2 pin 2 must never be swapped; a swap would connect the S line to the optocoupler return (COMMON).

- All switch lines are low-side contacts to COMMON behind high-impedance pull-ups (~40 kΩ, ~50 kΩ). A PC817 (collector on the line, emitter on blue) can sink these loads with a large margin; ~2 mA LED current (1 kΩ from a 3.3 V GPIO, 10 kΩ pull-down) is sufficient.
- **As built, bench-verified 2026-10-15:** J3 pin 1 (orange) ↔ camera WHT (FULL) is driven by GPIO4 / U3; J3 pin 2 (red) ↔ camera YEL (HALF) by GPIO5 / U4; J3 pin 3 (brown) ↔ GRN by GPIO6 / U5. Earlier drafts (≤ v0.12) had HALF and FULL the other way round; the splice at the camera was not changed, the documentation and firmware were. A soldering fault on the U3 stage (FULL) was repaired on 2026-10-15; before that, FULL never reached the camera and no picture could be taken. Test with the `firmware/optotest` tool: HALF (U4) and FULL (U3) both asserted releases the shutter, releasing both ends the exposure. The PWR stage (U5) pulls the green line low as intended.
- Bench check: a diode from yellow (anode) to blue (cathode) triggers focus, so the camera's low threshold is above ~0.6 V; a PC817 (Vce,sat ~0.1–0.2 V) pulls lower.

### 6.3 Camera on/off line (green, optional)

The camera's on/off knob is a sliding contact on a ground plane:

- **OFF position:** green is shorted to ground (0.5 Ω to blue, 0 V).
- **ON position:** green is open and pulled up by the camera to ~4.72 V (0.01 mA short-circuit current; 34.5 Ω to blue measured with the battery out, cause unknown).

In short, the remote being switched on and linked keeps the camera on (v0.15; previously the ARM switch did).

Consequence: shorting green to blue switches the camera **off**; releasing it lets the camera follow the knob. A third optocoupler (collector on green, emitter on blue, same drive circuit as §6.2: 1 kΩ series, 10 kΩ pull-down) switches the camera off from the node, with the knob left in ON.

**Fail-safe:** an optocoupler that is off (ESP32 in reset, crash, brownout) does not affect the camera, which then simply follows the knob (ON). The node can never switch the camera off by accident through a fault.

**Feature switch:** remote camera power control is controlled by `cam_power_ctrl` (default **off**). With it off, the third optocoupler is never driven and behaviour is as in v0.4. It shall only be enabled once tests T-40 to T-43 pass.

**Behaviour with `cam_power_ctrl` = on:**

| Node state | Camera power (green) |
|---|---|
| BOOT, link lost | OFF (optocoupler conducting), once the camera has been idle for `t_cam_off_delay` |
| Link comes up (first valid packet) | ON (optocoupler released); SHOOT is acted on only after `t_cam_boot` (FR-N15; keep-alive only if enabled, FR-N03) |
| Console test while camera OFF | ON, wait `t_cam_boot`, run the test cycle (FR-N07) |
| `prepare` = 1 while camera OFF (RLC armed) | ON at once; wake pulse after `t_cam_boot` (FR-N19) |
| After any burst or test | Stay ON for at least `t_cam_off_delay` (card write-out), then OFF only if the link is lost |
| SHOOTING, LOCKOUT | ON, never switched off |
| FAULT (pack cutoff) | OFF after `t_cam_off_delay` |

The node never switches the camera off while the shutter is not in REST or within `t_cam_off_delay` of the last FULL.

## 7. Radio and protocol

### 7.1 Radio

| ID | Requirement |
|---|---|
| RR-01 | ESP-NOW in unicast mode between one remote and one node, bound to each other at build time (§7.4). |
| RR-02 | Operating channel `radio_channel`, a build-time setting for both CRIG units (§7.4), **default and specified value: channel 1** (2412 MHz, 20 MHz wide). The CRIG channel shall be **at least 5 channels away from the RLC channel** (no overlap of the 20 MHz channels), and should be as far away as possible, because the CRIG remote sits within 1 m of the RLC base in scenario A. With the RLC on channel 11, channel 1 gives the maximum spacing (50 MHz); channel 6 (25 MHz) is the fallback if channel 1 is unusable on the field. If the RLC's `WIFI_CHANNEL` is ever changed after a field survey, re-check this rule and re-flash both CRIG units if needed. |
| RR-03 | ESP-NOW encryption enabled with a PMK and a per-pair local master key (LMK) fixed at build time (§7.4). |
| RR-04 | Optional Espressif long-range mode (`radio_lr_mode`), enabled on both ends or neither. The RLC does not use LR mode. |
| RR-05 | TX power configurable (`radio_tx_power`). In scenario A the remote is ~1 m from the RLC base; if T-20 shows any effect on the RLC link, reduce the remote's TX power (the node is only 5–10 m away). |
| RR-06 | Both units use an ESP32-S3 with an external antenna connector (u.FL) where range requires it. The node uses the same ESP32-S3-DevKitC-1 dev board as the RLC (WROOM-1, PCB antenna); a -1U variant with u.FL is to be chosen if the range test (T-30) needs it (O-16). The remote uses an **ESP32-S3-DevKitC-1U** (WROOM-1U, u.FL) with an external antenna (v0.15). |

### 7.2 Packet format

All multi-byte fields little-endian. Total size ≤ 32 bytes.

| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `magic` | uint32 | `0x47495243` ("CRIG") |
| 4 | `version` | uint8 | Protocol version, currently `1` |
| 5 | `type` | uint8 | Message type (§7.3) |
| 6 | `seq` | uint16 | Sender sequence number, wraps |
| 8 | `ack_seq` | uint16 | Last sequence number received from the peer |
| 10 | `payload` | 0–18 bytes | Type-specific |
| n | `crc16` | uint16 | CRC-16/CCITT-FALSE over bytes 0…n−1 |

### 7.3 Message types

| Code | Name | Direction | Payload | Rate |
|---|---|---|---|---|
| `0x01` | HEARTBEAT | R → N | `prepare` (uint8: 0/1) | `f_heartbeat` (5 Hz) |
| `0x02` | SHOOT | R → N | `source` (uint8: 1 = button, 2 = TRIG), `prepare` (uint8: 0/1) | `f_shoot` (20 Hz) while active |
| `0x81` | STATUS | N → R | See below | Reply to every valid packet |

STATUS payload:

| Field | Type | Description |
|---|---|---|
| `state` | uint8 | Node state (§9.1) |
| `shutter` | uint8 | Current shutter_t |
| `fault` | uint8 | Bit flags: 0 pack low warning, 1 pack cutoff, 2 actuator fault, 3 config invalid, 4 camera powered (1 = camera on or power control disabled), 5 NO_PACK (no pack sensed, bench / USB; v0.19), 6 PREPARING (`prepare` = 1, FR-N19; v0.19) |
| `vbat_mv` | uint16 | Node pack voltage |
| `rssi` | int8 | RSSI of the last received remote packet |
| `burst_ms` | uint32 | Elapsed FULL time of the current/last burst |

Codes `0x03` (TEST), `0x10`–`0x12` (CFG_GET, CFG_SET, JOG) and `0x91` (CFG_VAL) were removed in v0.15 together with the remote's TEST and MENU buttons and the `arm_req` field; they stay reserved. Configuration, calibration and test cycles are done on the node's USB console.

The `source` field in SHOOT is informational (node logging); the node treats both sources identically.

### 7.4 Peer binding (compile time; no pairing procedure)

There is no pairing step and no PAIR button. The two units are bound to each other when the firmware is built:

1. Each build contains the **MAC address of the other unit** (node firmware: remote MAC; remote firmware: node MAC), the **PMK** and the **LMK**, and the operating channel.
2. These values live in a local, git-ignored configuration file (e.g. `crig_binding.h` or `sdkconfig.local`), never in the repository. One binding file is shared by both builds.
3. The unit's own MAC is read with `esptool read-mac` (or from the boot log) before building.
4. At start-up each unit registers only the other unit as an encrypted ESP-NOW peer. Frames from any other MAC, or with a wrong key, magic, version or CRC, are dropped silently (FR-N11).
5. Replacing either unit (or its chip) means reading its new MAC, updating the binding file and re-flashing **both** units.
6. The operating channel is a build-time setting as well (RR-02); the remote no longer sends it to the node. `radio_channel` is therefore not changed over the air.

Consequences: no pairing logic, no PAIRING state and no radio exchange of keys; but the LMK is static and sits in the firmware images (flash encryption is out of scope), and every hardware swap needs a re-flash.

### 7.5 Coexistence with the RLC and other systems

- The RLC uses ESP-NOW on **channel 11** (2462 MHz), 20 MHz wide (`WIFI_SECOND_CHAN_NONE`), without LR mode: `WIFI_CHANNEL 11` in the RLC's `components/rlc_common/include/rlc_config.h`, applied in `rlc_espnow.c` (checked 2026-11-03). The RLC spec treats 11 as a default that may be changed after a field survey (it avoids channel 1, which is often congested at events).
- The CRIG uses **channel 1** (RR-02): the non-overlapping channel furthest from 11, chosen because the RLC link is safety-critical and the CRIG remote sits next to the RLC base. Possible congestion on channel 1 is acceptable for the CRIG: hold-to-shoot tolerates lost packets (`t_shoot_timeout` 300 ms = 6 SHOOT packets at 20 Hz), and a lost burst only costs photos. Use channel 6 if a survey shows channel 1 unusable.
- Different peer MACs, encryption keys and magic number additionally prevent any cross-acceptance, even if channels were ever set equal.
- CRIG air time is negligible (≤ 20 packets/s of ≤ 32 bytes each way).
- For third-party launch systems (scenario B), check on site whether they use 2.4 GHz and choose a free channel if needed.

## 8. RLC interface (scenario A)

### 8.1 Function

The RLC base drives two optocoupler LEDs in the CRIG remote over a short 4-wire cable (v0.17):

- **TRIG** — a steady level during the shooting window around ignition. While TRIG is *accepted* (§8.6), the remote behaves exactly as if SHOOT were held.
- **ARM** — a pulse pattern (§8.3) that tells the remote whether the base is connected and running, and whether it is armed. While the base is armed, the remote has the node **prepare** the camera: switch it on if needed and wake it (FR-N19), so it is ready well before the burst starts at T−2 s.

ARM is not an operator arming step for the CRIG (principle 8 stands): it only prepares the camera and validates TRIG. The CRIG remote draws no power from the RLC base, and the two units have no galvanic connection. The cable-detect loop of v0.4–v0.16 (DET, pins 3–4) is replaced by the ARM heartbeat, which also tells whether the base firmware is running.

### 8.2 Connector and pinout

A **5-pin DIN 180°** connector pair (v0.18, chosen for cost: Gotron AD322 female chassis socket, €1.23, on the RLC base panel **and** on the CRIG remote; AD302 male plastic cable plug, €1.57, on **both** ends of the cable). It is keyed and fits one way only; it is not waterproof and has no lock, which is acceptable because the remote sits next to the base sheltered from exhaust and weather (§4.2), and an unplugged cable is a safe failure: ARM lost → TRIG ignored → RLC LOST shown before the launch (§8.6). Add strain relief (cable tie) at both ends. The connector type shall **not** be used for any other purpose on the RLC base (in particular not for igniter outputs), so the CRIG cable and igniter leads cannot be confused.

DIN pin numbering along the arc is 1 – 4 – 2 – 5 – 3, so COM− sits on the centre pin and the two signals are separated by unused pins.

| Pin | Signal | Side | Description |
|---|---|---|---|
| 1 | TRIG+ | RLC base | Base **GPIO39** via 470 Ω → TRIG optocoupler (U4) LED anode in the remote |
| 2 | COM− | RLC base | Base ground → both LED cathodes (shared return); centre pin |
| 3 | ARM+ | RLC base | Base **GPIO41** via 470 Ω → ARM optocoupler (U6) LED anode in the remote |
| 4, 5 | — | — | Not connected (spare) |

Both optocoupler LEDs and their reverse diodes are on the RLC side of the isolation barrier, so the shared return does not compromise the isolation. Cable: ≤ 1 m, 3 cores used (≥ 3-core shielded or unshielded audio cable is fine), wired **straight through** (1–1, 2–2, 3–3). Do not use a ready-made DIN audio cable without checking it: some swap pins 1↔3 and 4↔5 (recording leads), which would swap TRIG and ARM.

### 8.3 ARM line patterns

| ARM line (LED drive) | Meaning | Remote decoder |
|---|---|---|
| No pulses (steady off **or** steady on) | RLC not connected, base off, base hung, or wiring fault | No rising edge for `t_arm_lost` (2.5 s) |
| **1 Hz**: 100 ms on / 900 ms off | Connected, **not armed** (IDLE, LINK_LOST, ERROR, …) | Rising edges 0.7–1.3 s apart |
| **10 Hz**: 50 ms on / 50 ms off | **Armed**: base ARMED, PRE_FIRE, FIRING, POST_FIRE, and for as long as TRIG is asserted | Three consecutive periods of 70–130 ms (≈ 0.3 s); armed is left after 300 ms without such a period |

A stuck level in either direction decodes as "not connected", so a hung base or a shorted line can never be read as armed. On the base side the pattern is `armed = (state ∈ {ARMED, PRE_FIRE, FIRING, POST_FIRE}) OR TRIG asserted`; the OR keeps the line armed for the last second of the TRIG window, because POST_FIRE (2 s) ends before TRIG does (fire pulse end + 3 s).

### 8.4 Electrical requirements

| ID | Requirement |
|---|---|
| IF-01 | TRIG+ and ARM+ are driven by base **GPIO39** and **GPIO41** through ~470 Ω each, giving ~4–5 mA into each PC817 LED. Both pins are spare on the RLC base: not in its `pin_config.h`, not strapping (0, 3, 45, 46), USB (19, 20), UART0 (43, 44), flash (26–32) or octal PSRAM (35–37) pins of the DevKitC-1 N16R8. GPIO38 stays spare on purpose: on a DevKitC-1 **v1.1** it drives the on-board RGB LED (the base is a v1.0 board, LED on GPIO48). Physical check of the base wiring: O-8. |
| IF-02 | Each base GPIO shall have a 10 kΩ pull-down so TRIG and ARM are inactive (no pulses) during base boot and reset, before firmware configures the pins. |
| IF-03 | In the remote, each optocoupler output pulls its input LOW when the LED conducts (TRIG → IO5, ARM → IO6), with a 10 kΩ pull-up to 3V3 and 10 nF to ground, so an open input reads inactive. |
| IF-04 | Each optocoupler LED in the remote shall have a reverse-protection diode in anti-parallel (D1 for TRIG, D3 for ARM). |
| IF-05 | No conductor shall connect RLC base circuitry to CRIG remote circuitry other than through the optocouplers. Isolation between pins 1–3 and remote ground shall measure ≥ 10 MΩ with a multimeter (test T-15). |
| IF-06 | The RLC base supplies no power to the CRIG remote. |

### 8.5 Trigger and arm window

Based on the RLC base state machine (IDLE → ARMED → PRE_FIRE → FIRING → POST_FIRE → IDLE) with `ARM_TIMEOUT_MS` = 10 000 ms, `PRE_FIRE_DELAY_MS` = 5000 ms, `FIRE_PULSE_DURATION_MS` = 1000 ms and `POST_FIRE_COOLDOWN_MS` = 2000 ms:

| Event in the RLC base | ARM line | TRIG |
|---|---|---|
| Power-on / reset, before init | No pulses (pull-down) | Inactive (pull-down) |
| Init done, IDLE | 1 Hz | Inactive |
| IDLE → ARMED | **10 Hz** | Inactive |
| ARMED → IDLE (disarm, arm timeout, key off, link lost) | 1 Hz | Inactive |
| PRE_FIRE entered | 10 Hz | Inactive; start a one-shot timer of `PRE_FIRE_DELAY_MS − cam_lead_ms` (5000 − 2000 = 3000 ms) |
| Timer expires while still in PRE_FIRE | 10 Hz | **Assert** |
| PRE_FIRE → FIRING | 10 Hz | Remain asserted |
| Fire pulse ends | 10 Hz | Start a one-shot timer of `cam_post_ms` (3000 ms), then **deassert** |
| POST_FIRE → IDLE | 10 Hz while TRIG is still asserted, then 1 Hz | As above |
| Any exit from PRE_FIRE other than to FIRING (abort, link lost, key off, continuity lost, dead-man) | 1 Hz | **Deassert immediately**, cancel timers |
| Any exit from FIRING (pulse completed on link loss, cease-fire, key off, disarm, arm sense lost, ERROR, emergency safe) | 10 Hz while TRIG is still asserted, then 1 Hz | Treated as a pulse end: TRIG stays asserted, the `cam_post_ms` timer runs (started if not yet running) |
| ERROR, disarm, emergency safe, LINK_LOST before FIRING | 1 Hz | **Deassert immediately**, cancel timers |
| Hard cap | — | Deassert after `cam_max_ms` (10 s) of continuous assertion, regardless of state |

**Rule (v0.18, after the RLC review):** before FIRING is entered, every abort deasserts TRIG immediately (no igniter current has flowed, the burst only held pre-ignition frames). Once FIRING has been entered, TRIG always ends through the `cam_post_ms` timer (or the hard cap), whatever happens next: normal pulse end, pulse completed on link loss (`COMPLETE_PULSE_ON_LINK_LOSS`), cease-fire, key off, disarm, arm sense lost, ERROR or the emergency-safe path. The igniter may already have lit, so the liftoff should still be photographed; TRIG is an isolated output and cannot affect the fire path. ARM is armed in ARMED, PRE_FIRE, FIRING and POST_FIRE; in every other state (IDLE, **LINK_LOST**, ERROR) it shows 1 Hz, except while TRIG is still asserted (the CRIG only accepts TRIG with ARM armed).

Lead time for preparing the camera: the ARM line goes to 10 Hz at least 5 s before ignition (the PRE_FIRE delay) and up to ~15 s before (plus the time the operator spends in ARMED, at most `ARM_TIMEOUT_MS`), against 2 s for TRIG.

### 8.6 Remote behaviour

- **RLC status** (decoded from ARM, §8.3): **NONE** — no heartbeat seen since power-on (manual use, scenario B); **CONNECTED** (1 Hz); **ARMED** (10 Hz); **LOST** — a heartbeat was seen since power-on but is now absent. LOST is a warning; switching the remote off and on returns it to NONE (manual use).
- **TRIG is accepted only while ARM decodes as ARMED.** A stuck TRIG, or a base that hangs with TRIG asserted, therefore stops the burst within ≈ 0.3 s of the ARM pattern stopping, instead of running to `t_max_burst`.
- **PREPARE:** while ARM decodes as ARMED, the remote sets `prepare` = 1 in every packet to the node (FR-R15); the node prepares the camera (FR-N19).
- **Readiness** (FR-R13): once the remote is in RLC mode (status ≠ NONE), green additionally requires CONNECTED or ARMED.

Details for the RLC side are in Appendix A and in the hand-off document `docs/RLC_change_request_CRIG.md`.

## 9. States and user interface

### 9.1 Node state machine

| State | Shutter | Entry | Exit |
|---|---|---|---|
| BOOT | REST | Power-on / reset | → IDLE |
| IDLE | REST (periodic HALF only if keep-alive is enabled) | Boot, SHOOT timeout, LOCKOUT or FAULT cleared | → SHOOTING on SHOOT; → TEST / CAL on a console command |
| SHOOTING | HALF (`t_prefire`) → FULL | SHOOT while IDLE | → IDLE on SHOOT timeout; → LOCKOUT on `t_max_burst` |
| LOCKOUT | REST | Max burst exceeded | → IDLE on first non-SHOOT packet |
| TEST | HALF → FULL → REST | Console `test` while IDLE | → IDLE |
| CAL | Per console jog | Console `cal` while IDLE (option A only) | → IDLE on leaving calibration |
| FAULT | REST | Pack below cutoff / actuator fault | → IDLE when cleared |

There are no DISARMED and ARMED states (removed v0.15). Link state (up, or lost after `t_link_lost`) is tracked alongside the state machine for the status LED and camera power control (§6.3); it does not gate shooting.

### 9.2 Remote controls and display

| Control | Type | Function |
|---|---|---|
| SHOOT | Large momentary push button; as built a plain push button, with a green and a red 3 mm LED next to it as readiness indicator (O-18 closed) | Hold to shoot (scenario B manual trigger; also works in scenario A). A short tap takes 1–2 test frames. The LEDs show rig readiness (FR-R13) |
| Power | Toggle switch | Remote on/off; as built in the battery P+ line between the 1S protection board and the power-bank module, so the module draws nothing while off. The cell only charges with the switch on (§11.2) |
| RLC socket | 5-pin DIN 180° female, Gotron AD322 (§8.2) | TRIG and ARM from the RLC base |
| Charge port | USB-C on the power-bank charger/boost module | Charging the remote battery (switch on; the remote keeps running while charging) |
| Status LED | On-board WS2812 RGB LED of the dev board, behind a light pipe or window | Overall status (FR-R14), patterns below |

The remote has **no ARM switch and no TEST or MENU buttons** (removed v0.15): the CRIG is not a launch controller, and an accidental trigger only wastes a few frames (§3.3 principle 8). The dev board's USB-C is used for flashing and the serial console only.

Display: 0.96" (128×64) OLED.

- **Main page** (the only page): link quality %, RSSI, node state, node pack voltage, remote battery %, RLC status (NONE / CONNECTED / ARMED / LOST, §8.6), trigger source (BUTTON / TRIG), burst timer (s, 0.1 s resolution) and estimated frame count (`burst_ms / 333`).
- **Warnings** (overlaid on the main page): NO LINK, RLC LOST, LOCKOUT, NODE BATT LOW, NODE FAULT, REMOTE BATT LOW, TRIG ACTIVE AT BOOT.
- **No config page** (v0.15): node parameters are set on the node's USB console; remote parameters are build-time settings.

Status LED (on-board RGB, low brightness):

| Pattern | Meaning |
|---|---|
| Slow green blink (1 Hz) | Link OK, idle |
| Fast green blink (4 Hz) | No link |
| Solid red | Shooting (FULL confirmed by STATUS) |
| Amber blink | Warning: node or remote battery low, LOCKOUT, RLC LOST, TRIG ACTIVE AT BOOT |
| Red/amber alternating | NODE FAULT or remote battery at cutoff |

The discrete green/amber/red LEDs of earlier versions are dropped; the green/red readiness LEDs at the SHOOT button (FR-R13) and the RGB LED replace them.

Enclosure: shape and colour clearly different from the RLC remote; lanyard point; mounting provision for placement next to the RLC base.

### 9.3 Node status LED

| Pattern | Meaning |
|---|---|
| Off | Powered off |
| Slow green blink (1 Hz) | IDLE, link OK |
| Fast green blink (4 Hz) | No link |
| Solid red | SHOOTING / TEST |
| Red/amber alternating | LOCKOUT or FAULT |
| Blue blink | CAL |

The LED shall be shielded so it does not appear in frame or cause flare.

Implementation: the node uses the **on-board WS2812 (NeoPixel) RGB LED** of the ESP32-S3-DevKitC-1 (data on GPIO48 on a v1.0 board, GPIO38 on v1.1; check the board revision, cf. O-8). No external LED, resistors or GPIOs are needed. The LED is inside the enclosure and is made visible through a small light pipe or window placed out of the lens view. Brightness is set low to limit flare. The firmware drives the data signal on **both** GPIO48 and GPIO38 (v0.19), because the node board revision is not recorded; GPIO38 is otherwise unused on the node.

## 10. Camera configuration (D3000)

| Setting | Value |
|---|---|
| Release mode | Continuous |
| Exposure mode | M — 1/1000–1/2000 s, f/8, ISO 200–400 (daylight; verify with a test frame, i.e. a short SHOOT tap) |
| Focus | Lens switch on M, prefocused on the launch rail(s) |
| Image quality | JPEG Fine (RAW buffer too small for long bursts) |
| Image review | Off |
| Auto off timers | Any; a short meter-off saves power, since a SHOOT wakes the camera from standby (FR-N03). Only with keep-alive enabled must `t_keepalive_interval` stay below the meter-off time. Note: with USB connected to a PC the camera does not enter standby (keep the USB cable unplugged in the field and in standby tests) |
| Monitor | Off / shortest monitor-off time (saves power) |
| Lens | 18 mm, portrait orientation (landscape when covering several pads) |
| Card | Fast SD card with ≥ 8 GB free, formatted before each session |
| Battery | EN-EL9a removed; camera powered by the node pack (§11.3) |

## 11. Power

### 11.1 Design basis: one launch day

| Assumption | Value |
|---|---|
| Powered-on time | 12 h (setup to tear-down, no recharging) |
| Recorded flights | 40 |
| Frames per flight | 20 (scenario A ~18, plus margin) |
| Test and extra frames | 200 |
| Total frames | ~1 000 |
| Ambient temperature | 0–35 °C |
| Capacity margin | × 1.3 on top of worst-case consumption (cold, cell ageing, cutoff reserve) |

### 11.2 Remote battery

**Power path (as built 2026-11-03):** 18650 cell → 1S protection board (on the cell) → power toggle switch in P+ → power-bank charger + 5 V boost module → dev board **5V0** pin; 3.3 V comes from the dev board's own LDO. Same arrangement as the RLC remote and the node (external 5 V into 5V0), at the cost of the LDO loss. The vibration motor and the battery divider are on the switched battery (VBAT_SW). *(v0.15 plan, superseded: TP4056 with protection → slide switch → MT3608-class boost.)*

**Consumption estimate** (at the battery, 3.0–4.2 V):

| Load | Current | Notes |
|---|---|---|
| ESP32-S3, ESP-NOW RX continuously on, TX 5–20 Hz | ~100 mA (at 3.3 V) | No power saving assumed |
| 0.96" OLED | ~15 mA (at 3.3 V) | |
| On-board RGB LED and green/red LEDs | ~3 mA (at 3.3 V) | High-efficiency LEDs, direct GPIO drive |
| **Subtotal at 3.3 V** | **~120 mA** | ≈ 0.40 W |
| Boost 3.7 → 5 V (~88 %) × LDO 5 → 3.3 V (66 %) ≈ 58 % | +~65 mA | Referred to the cell at 3.7 V |
| Vibration motor | < 1 mA avg | ~65 mA from the battery, only during bursts |
| **Total at the cell** | **~185 mA** | |

**Energy for a launch day:** 185 mA × 12 h = 2.2 Ah; × 1.3 margin = **2.9 Ah required**.

**Specification:**

| Item | Specification |
|---|---|
| Cell | 1 × 18650 Li-ion, ≥ 3 000 mAh. The ordered LS18650-3 (3 350 mAh) has no protection in the cell; protection comes from the charger module |
| Expected runtime | ~16 h (one launch day; recharge overnight) |
| Charger and protection | As built: 1S protection board (variant "S1") on the cell + power-bank module (LX-LBCE1, "5V 2.4A Type-C + USB") that charges via its own USB-C; details below. *(v0.15 plan: TP4056 with DW01A/FS8205A.)* |
| Regulator | As built: the 5 V boost of the power-bank module, into the dev board 5V0 pin; dev board LDO for 3.3 V. *(v0.15 plan: MT3608-class boost set to 5.0 V.)* |
| Flashing | Over the dev board USB with the power switch **off**; the board then runs from USB, and the 5V0 pin is then fed by USB as well as by the module if the switch is on; prefer switch off while flashing |
| Monitoring | 100 kΩ / 100 kΩ divider on the switched battery to an ADC1 pin (IO1), 100 nF at the pin; `v_remote_warn` 3.5 V, `v_remote_cutoff` 3.2 V |
| Mass | ~50 g cell + holder |

The earlier TPS63020 buck-boost and BQ24074 charger plan (v0.4–v0.14) is dropped.

**As built (2026-11-03):** the TP4056 and the separate boost module were replaced by two AliExpress modules (product pages saved under `netwerkdrive/Electronics/Aliexpress/`):

| Function | Module | Notes |
|---|---|---|
| Cell protection | 1S Li-ion protection board, variant "S1" | Over-charge / over-discharge / over-current cut-off only; no charger. Exact IC and thresholds not documented by the seller |
| Charger + 5 V boost | "5V 2.4A Type-C + USB" power-bank module (LX-LBCE1, variant "2.4A Weld USB Blue") | Charges the cell through its own USB-C; boosts to 5 V on its 5V pad / USB-A output, into the dev board 5V0. Controller IC not identified |

Wiring as built: the protection board (a strip) is soldered directly onto the cell's + and − terminals; its P− goes to B− of the power-bank module, its P+ goes to B+ of the module through the power toggle switch. The switch therefore sits on the battery side, before the module, so the module draws nothing from the cell while the remote is off (a switch in the 5 V output would leave the module's boost and quiescent current running). Consequences: the remote ground is P− (switched by the protection board), and **the cell only charges with the switch ON**.

Power-bank module checks (owner, 2026-11-03, on battery with USB unplugged), all **pass**: (1) no light-load auto-off at the remote's load; (2) the remote keeps running while the charger is plugged in (pass-through); (3) the 5 V comes up by itself when the switch is turned on, no button press needed. The battery sense measures 3.82–3.89 V on a charged cell (IO1, 2026-11-03).

### 11.3 Node and camera pack

The camera's EN-EL9a is removed; leads are soldered to the camera's battery compartment terminals and connected to the node, which powers the camera from its own pack.

**Camera supply voltage:** the EN-EL9a is a 2S Li-ion pack (7.2–7.4 V nominal, 8.4 V full). A 2S Li-ion node pack (6.0–8.4 V) therefore feeds the camera directly, without regulation, within the voltage range the camera is designed for.

**Consumption estimate** — camera figures are estimates to be verified by measurement (O-11):

| Load | Basis | Energy per launch day |
|---|---|---|
| Camera, shooting | CIPA rating ~550 shots per EN-EL9a (8 Wh) → ~15 mWh/shot, incl. overheads; × 1 000 frames | ~15 Wh |
| Camera, powered on and idle (worst case: meters active all day, e.g. keep-alive enabled; with the default no keep-alive the camera is mostly in standby) | ~150 mA at 7.4 V ≈ 1.1 W × 12 h | ~13 Wh |
| Node ESP32-S3 dev board (RX on) + LED | ~120 mA (estimate) through the board's 3.3 V LDO from 5 V ≈ 0.6 W, via the 5 V buck (~85 %) ≈ 0.7 W × 12 h | ~8.5 Wh |
| Servo (option A only) | Detached at rest; keep-alive and burst movements | ~1 Wh |
| **Worst-case total** | | **~37.5 Wh** |
| **Required with × 1.3 margin** | | **~49 Wh** (pack ≈ 48 Wh with 4 × 3350 mAh: tight; camera power control §6.3 saves up to ~13 Wh; current of the dev board on 5 V to be measured, T-33) |

**Specification:**

| Item | Specification |
|---|---|
| Pack | 2S2P Li-ion, 4 × 18650 ≥ 3 450 mAh (e.g. Samsung INR18650-35E) → 7.2 V, ~6.9 Ah, ~50 Wh |
| Expected runtime | ~15 h worst case, > 20 h typical (camera meters mostly off) |
| BMS | 2S protection/balancing board, ≥ 8 A continuous, over-discharge ≤ 2.8 V/cell, short-circuit protection |
| Fuse | **One** fuse directly behind the BMS output, before the main switch, protecting everything behind it: 2.5 A slow-blow (5 × 20 mm). Raise to 5 A if the camera peak current measured in O-11/T-33 requires it |
| Main switch | Powers node and camera together |
| Camera branch | From the switched +VBAT directly (no own fuse), in parallel with the node buck → camera lead: 2 × 0.5 mm² silicone wire, ≤ 0.5 m, detachable polarised 2.54 mm connector J2 (3-pin, §6.2) at the node |
| Node logic | Buck converter 2S → 5.0 V, ≥ 1 A (e.g. MP1584 module, set and measured before connecting), feeding the 5V0 pin of the dev board; its on-board LDO makes 3.3 V. Input must stay ≥ ~6.0 V (`v_node_cutoff` 6.6 V). USB must not be connected while the pack powers the board |
| Servo (option A) | Separate buck converter 2S → 5 V, ≥ 3 A peak, ≥ 470 µF bulk capacitor at the servo |
| Monitoring | Pack voltage divider to ADC; `v_node_warn` 7.0 V (~20 %), `v_node_cutoff` 6.6 V |
| Charging | Off-board with a 2S balance charger: XT30 charge/discharge lead + JST-XH 2S balance lead; ~3.5 h at 2 A |
| Mass | ~200 g cells + ~60 g holder, BMS and wiring |

**Camera connection requirements:**

| ID | Requirement |
|---|---|
| PW-01 | Before removing the EN-EL9a for good, measure the resistance between each of its signal contacts (all contacts other than + and −) and −. **Measured (corrected 2026-10-15):** the EN-EL9a has one signal contact ("S", centre) that measures ≈100 kΩ to − at room temperature (86 kΩ after warming in the hand, rising again as it cools: NTC-like). Earlier versions of this document said 98 Ω; that was wrong. A fixed 96.8 kΩ resistor on S does **not** remove "battery cannot be used". With the original battery in the camera, S shows 0 V DC but bursts of 9600 baud data (≈1–1.5 V) at battery insertion: two bursts, the second with a fixed header (break, 0x55 sync, `25 55 FF`) and a payload that differs on every capture, so a fixed answer is not enough (challenge-response or counter suspected). **Parked:** the camera keeps running on the original Nikon battery; see `docs/en-el9-s-pin/EN-EL9_S-pin_findings.md`. Options when taken up again: Nikon EP-5 + EH-5A DC coupler, or a third-party EN-EL9 known to work in the D3000. The RS 100 Ω plan and the Gotron NTC100 idea are dropped. **Follow-up 2026-10-16/19:** a listen-only sniffer (`firmware/ssniff`, S → 10 kΩ → GPIO2/ADC, bench only) is ready to capture the camera alone and the camera ↔ battery exchange. Web research found no public description of the protocol; "decoded" third-party EP-5 couplers exist and are the practical route if camera power from the node is needed. Re-measured 2026-11-03: 102 kΩ S to − (battery out of the camera). |
| PW-02 | Leads shall be soldered to the camera's battery compartment terminals with strain relief inside the compartment. The lead exits through a notch in the battery door so the door can close; if the camera has a battery-door switch, it must remain actuated. |
| PW-03 | Polarity shall be verified with a multimeter before first connection. The camera lead connector shall be polarised. |
| PW-04 | With option B (wired release), the shutter wires (COMMON, HALF, FULL) may share one multi-core cable with the camera power leads. |
| PW-05 | The camera's own battery gauge reads the pack voltage and is only indicative; the node's pack voltage reading on the remote is authoritative. |

## 12. Hardware summary

### 12.1 Node

Draft schematic: `crig_node_schematic.svg` (PNG alongside), v0.2, not bench-validated; the GPIO assignment in it (IO1 VBAT sense, IO4 FULL, IO5 HALF, IO6 camera power, IO15 buzzer (as built: IO7); status LED = on-board WS2812 on IO48/IO38) is a proposal.

- ESP32-S3-DevKitC-1 dev board (same as the RLC), powered at 5 V by the node buck; on-board LDO, EN reset network, BOOT/RESET buttons and USB-C used for flashing
- Node and camera pack per §11.3
- Actuator output: servo header (option A) and/or 2 × optocoupler output with connector (option B); optional third optocoupler for the camera on/off line (§6.3)
- Active buzzer on GPIO7 (as built; the draft said GPIO15), + to GPIO7 and − to GND, direct drive (2 mA measured at 3.3 V). An active buzzer has one fixed pitch, so patterns are made by rhythm only
- Camera connectors: J2 (3-pin, power and S) and J3 (4-pin, signals), 2.54 mm polarised (§6.2); status LED (on-board WS2812 of the dev board, §9.3), USB-C of the dev board for firmware and serial console
- Splash-resistant enclosure fixed to the camera bracket; grit and exhaust shielding for the camera

### 12.2 Remote

Draft schematic: `crig_remote_schematic.svg` (PNG alongside), v0.1, not bench-validated; the GPIO assignment in it is a proposal.

- **ESP32-S3-DevKitC-1U** dev board (same family as the RLC and the node; WROOM-1U N16R8 like the RLC, so GPIO 35–37 are taken by the octal PSRAM) with an external antenna on the u.FL connector. Powered at 5 V on 5V0; on-board LDO, EN/BOOT buttons and USB-C used for flashing and the console
- Battery, protection, charger and 5 V boost per §11.2 (as built: 1S protection board + power-bank charger/boost module)
- RLC socket (5-pin DIN 180° female, Gotron AD322) with two PC817 optocoupler inputs: TRIG → IO5 (U4) and ARM → IO6 (U6), each with anti-parallel diode, 10 kΩ pull-up and 10 nF (§8). The cable-detect loop is removed (v0.17)
- SHOOT button with bi-colour ring: contact to a GPIO with pull-up; each ring colour **directly from a GPIO through a series resistor**, no transistors (high-efficiency LEDs, ~1–2 mA). The ring must light at ≤ 3.3 V per colour (O-18). **As built (2026-11-03):** a plain push button (no LED ring) plus two separate 3 mm LEDs, green on IO10 and red on IO11, GPIO → resistor → LED → GND
- 0.96" I²C OLED (VMA438) on 3.3 V
- Status LED: the dev board's on-board WS2812 on **GPIO48** (fixed, same as the RLC's `PIN_RGB_LED`; verified working 2026-11-03, O-19 closed)
- Vibration motor (ADA1201) from the switched battery through an NPN low-side switch with flyback diode; at ~65 mA it is too much for a GPIO
- Active buzzer, the same 3.3 V part as the node (internal oscillator, ~2 mA): + to a GPIO, − to GND, direct drive
- No ARM switch, no TEST or MENU buttons, no discrete LEDs (v0.15)
- Distinct enclosure (§9.2)

## 13. Configuration parameters

As implemented (v0.19) all parameters are **compile-time constants** in `firmware/crig/components/crig_common/include/crig_config.h`; NVS storage and console editing are not implemented yet (O-21). Stored in node NVS unless noted; editable from the node's USB console. The remote has no config page (v0.15); remote parameters are build-time settings.

| Parameter | Default | Range | Notes |
|---|---|---|---|
| `radio_channel` | 1 | 1–13, ≥ 5 channels from the RLC channel | Build-time, both units; RLC uses 11 → 1 (fallback 6) (RR-02) |
| `radio_lr_mode` | off | on/off | Both units |
| `radio_tx_power` | max | — | Both units |
| `f_heartbeat` | 5 Hz | 2–10 Hz | Remote |
| `f_shoot` | 20 Hz | 10–50 Hz | Remote |
| `t_trig_debounce` | 10 ms | 5–50 ms | Remote |
| `t_arm_lost` | 2.5 s | 1.5–5 s | Remote: no ARM edge for this long = RLC not connected / LOST (§8.3) |
| `t_prepare_wake` | 300 ms | 100–1000 ms | Node: HALF pulse when `prepare` rises (FR-N19) |
| `t_prepare_keepalive` | 4 s | 1–30 s | Node: HALF repeat interval while `prepare` = 1 (FR-N19); below the camera's meter-off time |
| `t_shoot_timeout` | 300 ms | 100–1000 ms | Must be > 3 × SHOOT period |
| `t_prefire` | 100 ms | 0–500 ms | HALF before FULL |
| `t_max_burst` | 15 s | 3–60 s | Must exceed the RLC `cam_max_ms` (10 s) |
| `t_keepalive_interval` | 0 (off) | 0, 10–600 s | 0 = no keep-alive (default, FR-N03); if used, must be < camera meter-off time |
| `t_keepalive_pulse` | 300 ms | 100–1000 ms | |
| `t_test_full` | 150 ms | 50–500 ms | Single frame |
| `t_link_lost` | 2 s | 1–30 s | Node and remote: NO LINK indication; does not gate shooting (renamed from `t_link_lost_disarm`, v0.15) |
| `v_node_warn` | 7.0 V | — | 2S pack |
| `v_node_cutoff` | 6.6 V | — | 2S pack; above the BMS cutoff |
| `v_remote_warn` | 3.5 V | — | Remote, 1S |
| `v_remote_cutoff` | 3.2 V | — | Remote, 1S |
| `v_no_supply` | 2.0 V | — | Node: below this no pack is sensed (NO_PACK flag, no cutoff) |
| `v_remote_no_cell` | 2.5 V | — | Remote: below this no cell is connected (switch off / USB power) |
| `t_batt_cutoff` | 3 s | — | Remote: time continuously below `v_remote_cutoff` before cutoff |
| `t_batt_shutdown` | 10 s | — | Remote: deep sleep this long after cutoff |
| `t_status_stale` | 1 s | — | Remote: readiness red without STATUS for this long (FR-R13) |
| link quality threshold | 50 % | — | Remote: minimum for "ready" (FR-R13) |
| `actuator_type` | SERVO | NONE / SERVO / OPTO | |
| `servo_us_rest/half/full` | 1500 / — / — | 500–2500 µs | From calibration |
| `servo_detach_at_rest` | on | on/off | |
| `cam_power_ctrl` | off | on/off | Remote camera on/off via green line (§6.3) |
| `t_cam_boot` | 3 s | 1–10 s | Camera on → ready; to be measured (T-41) |
| `t_cam_off_delay` | 30 s | 5–300 s | Minimum on-time after the last FULL (card write-out; T-42) |

RLC-side parameters (`cam_lead_ms`, `cam_post_ms`, `cam_max_ms` and the ARM pattern timings) are compile-time constants in the RLC firmware (Appendix A).

## 14. Test plan

### 14.1 Bench — CRIG

| ID | Test | Pass criterion |
|---|---|---|
| T-01 | Actuator calibration (option A) or contact verification (option B) | HALF wakes camera without exposure; FULL fires |
| T-02 | Burst rate | 5 s burst yields 14–16 frames |
| T-03 | Wake from standby | After 60 min idle without input (camera USB unplugged), a SHOOT burst starts with the default `t_prefire`; if keep-alive is enabled, the camera stays awake 60 min instead. Partly done 2026-10-16: wake verified after 10 min |
| T-04 | Release on button up | REST within `t_shoot_timeout`; frame count stops |
| T-05 | Remote power-off during burst | REST within `t_shoot_timeout` |
| T-06 | Max burst | REST at `t_max_burst` ± 100 ms; LOCKOUT shown; re-shoot requires release |
| T-07 | SHOOT held / TRIG asserted at remote boot or link recovery | No shooting until inactive once, then re-activated |
| T-08 | *Removed in v0.15 (no ARM switch)* | — |
| T-09 | Node reset during burst | Boots to IDLE, REST; next burst only after new SHOOT packets |
| T-10 | Corrupt/foreign packets (fuzzing, wrong key, wrong magic) | No action, no reply |
| T-11 | Low pack / low remote battery (bench supply ramp) | Warnings and cutoffs at the configured voltages, reported on the remote |
| T-12 | TRIG from a bench supply + 470 Ω, with a 10 Hz ARM pattern on pin 3 (any ESP32 GPIO through 470 Ω) | Burst while asserted; REST within `t_trig_debounce` + `t_shoot_timeout` after deassert |
| T-13 | Cable unplugged / no ARM pattern, TRIG forced on | TRIG ignored, no SHOOT; status NONE (or LOST if a heartbeat was seen since power-on) |
| T-14 | Camera powered from the node pack (bench supply 6.6–8.4 V, then the real pack) | Camera operates normally over the full range; battery level plausible (PW-01) |
| T-15 | Galvanic isolation of the RLC interface | ≥ 10 MΩ between pins 1–3 and remote ground |
| T-16 | Short in the camera lead (via fuse test load) | The pack fuse opens; nothing smokes, the BMS does not trip first |
| T-17 | ARM decoding (pattern source on pin 3) | 1 Hz → CONNECTED; 10 Hz → ARMED within 0.5 s; back to 1 Hz → CONNECTED within 0.5 s; steady on or off → LOST within `t_arm_lost` + 0.5 s |
| T-18 | TRIG gating | TRIG asserted with ARM at 1 Hz, steady on or absent → no burst; ARM 10 Hz → burst; ARM stopped during a burst → REST within 0.3 s + `t_shoot_timeout` |
| T-19 | PREPARE (camera in standby; then with `cam_power_ctrl` on and the camera off) | ARM 10 Hz → camera wakes (and switches on) without taking a picture; stays awake while armed; no wake pulses after ARM returns to 1 Hz |

### 14.2 Integration — RLC

| ID | Test | Pass criterion |
|---|---|---|
| T-20 | RLC and CRIG operating simultaneously, 30 min, all units within 1 m | No unintended node or RLC actions; RLC ping failures unchanged vs. baseline; CRIG link quality ≥ 95 % |
| T-21 | Full RLC sequence (inert igniter tester), CRIG connected | ARM 10 Hz from ARMED until TRIG deasserts, 1 Hz otherwise; camera prepared on ARMED; TRIG asserted at PRE_FIRE + 3000 ms ± 50 ms, deasserted at fire pulse end + 3000 ms ± 50 ms; camera bursts ~6 s |
| T-22 | RLC abort during PRE_FIRE (release fire button; key off) | TRIG deasserted within 20 ms of the abort; ARM back to 1 Hz; burst stops |
| T-23 | RLC base power-off during burst | TRIG inactive, ARM pattern stops; node REST within `t_shoot_timeout`; remote shows RLC LOST, stays on and linked |
| T-24 | RLC timing unchanged | PRE_FIRE and fire pulse durations identical (± 5 ms) with and without the CRIG connected |
| T-25 | Short between pin 1 or pin 3 and pin 2 (COM−) at the CRIG end | Current limited by the 470 Ω resistor; RLC base continues to operate normally |
| T-26 | ARM after base power-on | No pulses during boot; 1 Hz within 2 s of power-on |
| T-27 | ARM on RLC transitions (arm, disarm, arm timeout, key off, ERROR) | 10 Hz within 100 ms of entering ARMED; 10 Hz stops within 100 ms of returning to IDLE / ERROR (or of TRIG deasserting after a fire). A new pattern starts in its LOW phase (RLC as built, RLC FSD §15.6), so the first 1 Hz pulse follows about 950 ms after the switch; the CRIG decoder accepts this (host test "LOW-start disarm") |

### 14.3 Field and endurance

| ID | Test | Pass criterion |
|---|---|---|
| T-30 | Range test, line of sight | Link quality ≥ 95 % at 1.5 × the largest expected distance (flight line → camera in scenario B) |
| T-31 | Node + camera endurance: 12 h linked and idle (default, no keep-alive), 1 000 frames in bursts of 20 | Pack voltage ≥ `v_node_warn` at the end |
| T-32 | Remote endurance: 12 h on, linked, 40 bursts | Battery ≥ 15 % at the end (≈ 16 h expected, §11.2) |
| T-33 | Camera current measurement (standby, meters active, during burst) | Values recorded; power budget §11.3 updated (O-11) |
| T-34 | Sunlight and temperature | Operation from 0 °C to 35 °C; display readable in sunlight |
| T-35 | Dry run with the full field procedure, both scenarios (§4) | Operator completes procedures without errors |

### 14.4 Camera power control (§6.3, only if `cam_power_ctrl` is to be enabled)

| ID | Test | Pass criterion |
|---|---|---|
| T-40 | Short green to blue (wire, then optocoupler) with the camera on, knob in ON | Camera switches off cleanly (no error, no card message); releasing it switches the camera on again |
| T-41 | Measure time from releasing green to HALF being accepted | Value recorded; `t_cam_boot` set ≥ 1.3 × measured |
| T-42 | 20-frame JPEG Fine burst, then measure how long the card-access LED stays on | `t_cam_off_delay` ≥ 2 × measured; no card errors after 50 off/on cycles after bursts |
| T-43 | ESP32 reset, crash and brownout with the camera off | Optocoupler released; camera switches on and stays on |
| T-44 | Menu settings and clock after 20 off/on cycles | Unchanged |
| T-45 | Power budget: 12 h with the camera off between 40 bursts | Pack energy lower than in T-31; recorded |

### 14.5 Automated tests (firmware v0.19)

`firmware/crig/tests/host/run.sh` covers 30 unit checks of the CRC, packet encode/decode and the ARM decoder. `firmware/crig/tests/hil/hil.py` runs H01–H17 on both boards over USB, using the remote's console test hooks in place of the button, TRIG, the ARM pattern and the battery. On 2026-11-03 all 17 passed in three consecutive runs, and again on 2026-11-06 with the remote on its native USB port. With the RLC base on the DIN cable, `hil.py rlc` runs R01–R02 against the real ARM and TRIG lines (both passed 2026-11-06, RLC fw 1.2.22).

| HIL | Covers | Measured 2026-11-03 |
|---|---|---|
| H01 | Link up, quality, readiness (FR-R07, FR-R13) | link in ~0.2 s, q 100 %, RSSI −8 dBm |
| H02 | Burst HALF → FULL → REST (FR-N04/05, T-04) | latency ≤ 0 ms (host), prefire 101 ms, release 258–298 ms |
| H03 | SHOOT held through link recovery (FR-R03, T-07) | ignored until released |
| H04 | Remote reset during burst (T-05) | REST 274–313 ms |
| H05 | Max burst, LOCKOUT, release (FR-N06, FR-R08, T-06) | LOCKOUT at 15000 ms, cleared after 221 ms |
| H06 | Node reset during burst (T-09) | boots IDLE / REST |
| H07 | TRIG gating by ARM (§8.6, T-18) | shoot stop 0.28–0.30 s, REST ≤ 0.58 s after the pattern stops |
| H08 | ARM decoding, RLC status, readiness (T-17) | CONNECTED 2.0 s, ARMED 0.32–0.43 s, disarm 0.30 s, LOST 2.5 s after the last edge |
| H09 | PREPARE wake pulses (FR-N19, T-19) | 301 ms pulse, 4001 ms interval, no FULL |
| H10 | Console test cycle (FR-N07) | HALF 101 ms, FULL 150 ms |
| H11 | Corrupt frame (FR-N11, T-10) | dropped, no reply |
| H12 | Link-loss indication (FR-N08, FR-R13) | ready 0 at ~0.9 s, NO LINK at ~1.9 s |
| H13 | Packet rates (FR-R01/02) | 5.0 Hz heartbeat, 19.2–19.6 Hz shoot |
| H14 | STATUS content (FR-R06) | pack voltage, flags, RSSI |
| H15 | Node pack warning / cutoff / hysteresis (FR-N10, T-11) | pass |
| H16 | Remote cell warning / cutoff (FR-R11, T-11) | cutoff 3.5 s after the drop |
| H17 | OLED main page (FR-R06, §9.2) | pass |
| R01 | Real RLC base idle: ARM 1 Hz through U6, TRIG line idle (T-17, idle part of T-21) | CONNECTED 1.6 s after switching to the real line, periods 999–1000 ms, TRIG 0, nothing shoots |
| R02 | RLC base reset (T-26, RLC T-C06) | no pulses during boot; interface up 1.16 s, first ARM pulse 1.18 s after reset, then 1.00 s periods; never ARMED, TRIG 0 |

**Arm-and-fire bench session, 2026-11-06** (RLC fw 1.2.22, inert igniter tester on CH1, all four consoles logged; log in `captures/2026-11-06-rlc-arm-fire.log`, not in git). Times on the base's own clock unless marked.

| Test | Run | Measured | Result |
|---|---|---|---|
| T-27 arm | 5 arms | first 10 Hz pulse 22 ms after IDLE → ARMED; periods 99–101 ms; CRIG ARMED 0.31 s after arming (3 periods) | PASS |
| T-27 arm timeout | 10 s RLC arm timeout | CRIG CONNECTED 0.24 s later; first 1 Hz pulse 956 ms after the disarm (RLC LOW-phase start) | PASS |
| T-21 full sequence | 2 runs | TRIG HIGH at PRE_FIRE + 3000 ms; ignition 1990–2000 ms later; TRIG LOW at pulse end + 3000 ms; ARM 10 Hz until TRIG drops (0.96 s after POST_FIRE → IDLE); CRIG burst from TRIG HIGH to TRIG LOW (± 30 ms, host clock), 6.2 s | PASS |
| T-22a abort in PRE_FIRE (fire released) | 1 run | TRIG LOW in the same millisecond as the abort; CRIG stop at once, node REST 0.31 s later | PASS |
| T-22b / T-27 key off in PRE_FIRE | 1 run | same as T-22a | PASS |
| RLC T-C08 abort during FIRING (fire released) | 2 runs | TRIG stays HIGH until abort + 3000 ms; ARM 10 Hz until then | PASS |
| PREPARE (FR-N19) with the real RLC | every arm | 300 ms HALF pulses every 4.0 s while armed; one extra pulse straight after each burst (PREPARE still 1 for the ~0.3 s the remote needs to see ARM drop), fixed in the node: the next pulse waits `t_prepare_keepalive` after a burst (H07 checks it) | PASS after fix |
| T-25 ARM short (pin 3 to 2, base idle, 5 s) | 1 | no pulses for 5.0 s; CRIG LOST 2.49 s after the last pulse, CONNECTED one period after removal; base unaffected (IDLE, err NONE, continuity and link normal) | PASS |
| T-25 TRIG short (pin 1 to 2) while TRIG HIGH, full fire sequence | 1 | base timing unchanged (TRIG HIGH at PRE_FIRE + 3000, ignition + 2000, 1050 ms pulse, TRIG LOW at pulse end + 3000); CRIG burst stops while shorted and resumes after; base no error | PASS |
| T-23 base powered off during PRE_FIRE with TRIG HIGH | 1 | CRIG stop at once (TRIG LOW), ARMED → CONNECTED after 0.3 s, LOST 2.48 s after the last pulse; no spurious edges at power-off or power-on; no pulses during the reboot, 1 Hz after it | PASS |

Logs: `captures/2026-11-06-rlc-arm-fire.log`, `captures/2026-11-06-rlc-shorts-poweroff.log` (not in git). Not run: T-24 (PRE_FIRE and pulse durations with the CRIG lines loaded and unloaded: needs a scope or logic analyser; the loaded runs match the RLC's own 3000 / 2000 / ~1050 ms timing) and the ERROR case of T-27 (RLC fault-injection build).

**Look-and-listen check, 2026-11-06** (owner watching, consoles logged):

| Check | Result |
|---|---|
| Resting state: remote green LED on, red off, RGB slow green; node RGB slow green; OLED main page | as expected |
| Real SHOOT button: 5 taps and a 3 s hold | one burst per press, no bounce; motor, remote and node RGB red, `>> SHOOTING <<`, no tones during the burst. The green/red LEDs show readiness only and stay green while shooting (as designed) |
| Node off and on (link loss) | remote: one 400 ms beep, red LED, RGB fast green, `NO LINK`; recovery beeps on link-up |
| Node buzzer | silent at first: wired to **GPIO7**, firmware drove GPIO15. Firmware moved to GPIO7 (FR-N18); boot beep lengthened to 200 ms and link-up to 2 × 100 ms so they are noticed |
| DIN cable unplugged and replugged | remote: 300/150/300 ms beeps, red LED, RGB orange, `RLC LOST`; new: 2 × 100 ms beeps when the RLC comes back |
| Remote cutoff and deep sleep (FR-R11, simulated 3.0 V cell) | present 2.3 s, cutoff 3.0 s later, shutdown 10.0 s after that, USB gone = deep sleep, wakes on EN. First run: the OLED stayed lit (it is powered from 3V3 in deep sleep; the display task also redrew the main page over the empty message). Fixed: the display task stops, the message shows 2 s, then display off + charge pump off before sleep |


## 15. Open items

| # | Item |
|---|---|
| O-1 | ~~Actuator choice~~ — closed: internal wired release (option B, §6.2); wires installed and measured 2026-10-07 |
| O-2 | ~~RLC operating channel~~ — closed: RLC uses fixed channel 11, no LR mode; CRIG default channel 1 |
| O-3 | D3000 standby behaviour: **largely answered 2026-10-16**: the HALF lead of a SHOOT wakes the camera from standby (verified after 10 min idle), so keep-alive is off by default (FR-N03). Still open: idle times of 30–60 min and the camera's auto-off/meter-off setting (T-03) |
| O-4 | ~~Club rules for unattended cameras~~ — closed: club has no camera-specific rules |
| O-5 | ~~ESP32 variant~~ — closed: ESP32-S3 on both units; the remote uses an ESP32-S3-DevKitC-1U (external antenna) like the RLC's board family (v0.15) |
| O-6 | Actual camera frame count feedback is not available from the D3000; the remote shows an estimate only |
| O-7 | ~~Node power option~~ — closed: node pack powers node and camera; camera battery removed, leads soldered to its terminals (§11.3) |
| O-8 | Verify on the physical RLC base that **GPIO39 (TRIG) and GPIO41 (ARM)** are unconnected. Chosen 2026-11-03 from the RLC spec App. C.1 spares (38, 39, 41), cross-checked against the RLC `pin_config.h` (base uses 1, 2, 4–18, 21, 40, 42, 47, 48) and the DevKitC-1 N16R8 reserved pins; GPIO38 kept spare (on-board RGB on v1.1 boards; the base is v1.0, LED on GPIO48) |
| O-20 | RLC side of the interface (Appendix A, v0.17): hand-off `docs/RLC_change_request_CRIG.md` to the RLC project; implement, test (T-21…T-27) |
| O-21 | Firmware v0.19 has not been tested with a person present, and some features are not implemented yet. **Not tested:** the real SHOOT button debounce; RLC interface T-24 and the ERROR case of T-27 (T-21–T-23, T-25, T-26, the rest of T-27 and RLC T-C08 passed on the bench 2026-11-06, §14.5); the camera (frames, standby wake, PREPARE effect). The SHOOT button, LEDs, RGB patterns, buzzer tones, motor and deep sleep were checked on 2026-11-06 (§14.5). **Not implemented:** camera power control (`cam_power_ctrl`, §6.3; stays off); optional keep-alive (`t_keepalive_interval`, default off); CAL / servo actuator (option A); NVS storage of parameters; final buzzer tone patterns (O-17, provisional tones in use) |
| O-9 | Location of the 5-pin DIN socket (AD322) on the RLC base panel |
| O-10 | ~~Isolated DC-DC converter~~ — closed: no longer needed; the remote has its own battery and the interface is fully optically isolated |
| O-11 | Measure the D3000's actual current draw (standby, meters active, during a burst) and update the §11.3 budget |
| O-12 | EN-EL9a S contact: ≈100 kΩ NTC-like plus 9600 baud data (not 98 Ω); a fixed resistor does not remove the "battery cannot be used" error. **Parked**; the node cannot yet power the camera, the camera uses the original battery (PW-01). Reopen when camera power from the node is needed. Sniffer `firmware/ssniff` ready (bench only); alternative: a third-party EP-5 coupler that works in the D3000 without error |
| O-13 | ~~Shutter/power flex pinout~~ — closed: blue = COMMON, yellow = HALF, white = FULL, green = on/off (§6.2, §6.3) |
| O-16 | Node dev board antenna: DevKitC-1 (PCB antenna) vs. -1U (u.FL); decide after the range test (T-30). Also measure the dev board current at 5 V |
| O-18 | ~~SHOOT button~~ — closed 2026-11-03: plain push button + two separate 3 mm LEDs (green IO10, red IO11), direct GPIO drive. Original text: choose an illuminated type with a bi-colour (or two-colour) ring whose LEDs light at ≤ 3.3 V per colour and a few mA, since they are driven directly from GPIOs (no drivers). Series resistor values follow from the chosen ring. The three discrete LEDs are dropped (v0.15) |
| O-17 | Buzzer tone patterns for node and remote (events, distinct from RLC tones, silent during bursts). Buzzer part settled: the remote uses the same 3.3 V active buzzer as the node (internal oscillator, ~2 mA, direct GPIO drive) |
| O-19 | ~~Remote dev board revision~~ — closed 2026-11-03: RGB LED fixed on GPIO48 like the RLC, verified working; GPIO38 not used |
| O-14 | Remote camera on/off through the green line: specified in §6.3 (default off); open until T-40…T-45 pass and the feature is enabled |

---

## Appendix A — RLC change request (base unit)

This appendix describes the changes needed in the RLC project (`steemandavid/ESP32-RocketLaunchController`) to support scenario A. A self-contained version for the RLC project is in `docs/RLC_change_request_CRIG.md`.

### A.1 Summary

Add two optically isolated outputs to the RLC base on a new 5-pin DIN panel socket: **TRIG**, a steady level during a fixed window around ignition (unchanged from v0.4–v0.16), and **ARM**, a heartbeat pulse pattern (1 Hz = connected, 10 Hz = armed, §8.3). No power is supplied to the CRIG. No change to the radio protocol, the RLC remote, or any fire-path element.

### A.2 Hardware changes

| # | Change |
|---|---|
| H-1 | New **5-pin DIN 180° female chassis socket** (Gotron AD322), pinout per §8.2. Different connector type from all igniter connections (XT30) and the battery (XT60). |
| H-2 | TRIG: **GPIO39** → 470 Ω series resistor → socket pin 1; 10 kΩ pull-down at the GPIO (IF-01, IF-02). |
| H-3 | ARM: **GPIO41** → 470 Ω series resistor → socket pin 3; 10 kΩ pull-down at the GPIO. |
| H-4 | Base ground → socket pin 2, the centre pin (shared return for both LEDs). Pins 4 and 5 not connected. The cable-detect bridge of v0.4–v0.16 is dropped. |
| H-5 | Verify on the physical base that GPIO39 and GPIO41 are not wired to anything (O-8). Update the RLC spec's pin table (App. C.1) and as-built panel description; spare base GPIOs reduce from 3 (38, 39, 41) to 1 (38). |

### A.3 Firmware changes

| # | Change |
|---|---|
| F-1 | `pin_config.h` (base section): `PIN_CAM_TRIG` 39, `PIN_CAM_ARM` 41, `PIN_CAM_TRIG_ACTIVE` / `PIN_CAM_ARM_ACTIVE` 1 (active HIGH). |
| F-2 | `rlc_config.h`: `CAM_TRIG_LEAD_MS` = 2000, `CAM_TRIG_POST_MS` = 3000, `CAM_TRIG_MAX_MS` = 10000, static assert `CAM_TRIG_LEAD_MS < PRE_FIRE_DELAY_MS`; `CAM_ARM_TICK_MS` = 50, idle pattern 100 ms on / 900 ms off, armed pattern 50 ms on / 50 ms off. |
| F-3 | New module `rlc_cam_trig.c/.h`: `cam_trig_init()` (configure both outputs, drive LOW), `cam_trig_on_prefire_enter()`, `cam_trig_on_firing_enter()`, `cam_trig_on_fire_pulse_end()`, `cam_trig_cancel()`, `cam_arm_set(bool armed)`. TRIG uses `esp_timer` one-shots; `cam_trig_on_firing_enter()` sets a `fired` flag, after which `cam_trig_cancel()` behaves like `cam_trig_on_fire_pulse_end()` (post timer, started once) instead of deasserting (rule in §8.5); ARM is generated by one periodic `esp_timer` (`CAM_ARM_TICK_MS`) from a flag: armed pattern while `armed || trig_asserted`, idle pattern otherwise. The module only calls `gpio_set_level()`: no blocking calls, no radio, no inputs. |
| F-4 | Base FSM hooks. TRIG: `cam_trig_on_prefire_enter()` on ARMED → PRE_FIRE; `cam_trig_on_firing_enter()` on PRE_FIRE → FIRING; `cam_trig_on_fire_pulse_end()` on EVT_FIRE_PULSE_DONE (also when the pulse completes on link loss); `cam_trig_cancel()` on every other exit from PRE_FIRE or FIRING, on disarm, on entry to ERROR or LINK_LOST, and in the emergency-safe path (after FIRING it lets the post timer finish TRIG). ARM: `cam_arm_set(true)` on IDLE → ARMED; `cam_arm_set(false)` on every transition into IDLE, **LINK_LOST** or ERROR, and in the emergency-safe path. |
| F-5 | The lead timer callback asserts TRIG **only if** the FSM is still in PRE_FIRE at expiry. |
| F-6 | Hard cap: TRIG deasserted after `CAM_TRIG_MAX_MS` of continuous assertion, independent of FSM state. |
| F-7 | `cam_trig_init()` runs early in boot, alongside relay initialisation, so both pins are driven LOW as soon as possible (the pull-downs cover the time before that). GPIO39/41 are JTAG pad pins (MTCK, MTDI); their pre-init state is to be confirmed by T-26. Even with an internal weak pull-up (≈ 45 kΩ) the 10 kΩ pull-down holds the line below ≈ 0.6 V, under the PC817 LED threshold, and a boot glitch is neither a 1 Hz nor a 10 Hz pattern. The ARM timer starts at the end of init, with the idle pattern. |
| F-8 | If the ARM timer cannot be created or started, log it and leave ARM LOW. The CRIG then reads "not connected" and ignores TRIG: photographs are lost, the launch is unaffected. |

### A.4 Safety argument

- Both camera signals are outputs only. There is no input from the CRIG into the base, so no CRIG fault or signal can influence the fire path or the state machine.
- The CRIG is galvanically isolated from the base by the two optocouplers, and draws no power from the base.
- The module performs GPIO writes from timer callbacks only; the periodic ARM callback is a few instructions and reads only its own flags. It cannot block or delay the FSM or the fire pulse timer.
- A short at the CRIG end is limited to ~7 mA per line by the 470 Ω resistors; the base continues to operate.
- Failure of either signal (stuck high, stuck low, not connected) affects only the photographs. The CRIG ignores TRIG unless ARM shows the armed pattern, so a stuck TRIG does not cause a long burst either.

### A.5 RLC tests to add

T-21 to T-27 (§14.2), to be added to the RLC test specification with RLC-side IDs, plus a regression run of the existing fire-path safety tests after the change.

---

## 16. Revision history

| Version | Date | Change |
|---|---|---|
| 0.1 | 2026-09-27 | Initial draft |
| 0.2 | 2026-09-27 | Added node power options, closed O-4, added O-7 |
| 0.3 | 2026-09-27 | Two scenarios (RLC automatic / third-party manual); CRIG remote as the node's only controller; wired RLC interface with power from the base battery and single TRIG line; default channel changed to 1 (RLC on 11), O-2 closed; camera wake handled by the node only; no pad matching; integration tests; Appendix A RLC change request |
| 0.20 | 2026-11-06 | RLC interface on the bench (U4, U6 and the DIN socket fitted, RLC fw 1.2.22): the ARM input showed about four falling edges per pulse (optocoupler turn-off), so the remote now samples ARM every 1 ms with a 3 ms debounce (FR-R10 note); with the edge interrupt the 10 Hz armed pattern would never have decoded. New HIL tests R01–R02 against the real base (§14.5); O-21 updated; arm-and-fire bench session with the real RLC (T-21, T-22, T-26, T-27 except ERROR, RLC T-C08 all pass, §14.5; then T-23 and T-25 pass); node no longer gives a wake pulse straight after a burst (FR-N19); look-and-listen check passed (§14.5): node buzzer on GPIO7 as wired (FR-N18), longer node beeps, RLC-restored beep on the remote, OLED switched off before deep sleep |
| 0.19 | 2026-11-03 | Firmware v0.19 written and tested (`firmware/crig`, §14.5: 30 host checks, HIL H01–H17 all pass). Spec additions from the implementation: STATUS fault bits 5 NO_PACK and 6 PREPARING (§7.3); node no-pack rule below 2.0 V (FR-N10); remote no-cell rule below 2.5 V, battery measured only with the motor off, 3 s cutoff persistence and 10 s to deep sleep (FR-R11); node LED on GPIO48 and GPIO38 (§9.3); parameters compile-time in `crig_config.h` (§13, new rows); O-21 added; T-27 accepts the RLC as-built LOW-phase pattern start (first 1 Hz pulse ~950 ms after a disarm) |
| 0.18 | 2026-11-03 | RLC interface connector: GX12-4 replaced by a **5-pin DIN 180°** pair from Gotron (AD322 female chassis socket on base and remote, AD302 male plastic cable plug on both cable ends; ≈ €5.60), pinout 1 = TRIG+, 2 = COM− (centre), 3 = ARM+, 4/5 n.c.; weatherproofing dropped (sheltered placement, safe failure on unplug), straight-through cable rule (§8.2); radio channel rule made explicit (RR-02, RR-05, §7.5: RLC verified on channel 11, CRIG channel 1, fallback 6, ≥ 5 channels apart); RLC review answers (§8.5, App. A F-3/F-4/F-7): once FIRING is entered TRIG always ends via the post timer (incl. pulse completed on link loss and mid-pulse aborts), ARM 1 Hz in LINK_LOST too, `cam_trig_on_firing_enter()` hook, JTAG-pin boot note; T-12, T-17, T-25, H-1, H-3, H-4, O-9, §9.2, §12.2 updated |
| 0.17 | 2026-11-03 | RLC interface reworked (§8 rewritten, Appendix A rewritten): second optocoupler and signal **ARM** (base GPIO41 → remote IO6) next to TRIG (base GPIO39 → remote IO5); GX12-4 pinout TRIG+ / ARM+ / COM− / n.c.; DET loop dropped; ARM heartbeat patterns (none = not connected, 1 Hz = connected, 10 Hz = armed) with decoder rules (§8.3); TRIG accepted only while ARM = armed (§8.6, FR-R02, FR-R10); RLC status NONE / CONNECTED / ARMED / LOST (FR-R12); PREPARE flag in HEARTBEAT/SHOOT (FR-R15, §7.3) and node camera preparation (FR-N19, §6.3); new parameters `t_arm_lost`, `t_prepare_wake`, `t_prepare_keepalive`; tests T-12, T-13, T-15, T-21…T-23, T-25 revised, T-17…T-19, T-26, T-27 added; O-8 updated (GPIO39/41 chosen), O-20 added; hand-off `docs/RLC_change_request_CRIG.md` |
| 0.16 | 2026-11-03 | Remote as built (§4, §9.2, FR-R13, §11.2, §12.2; schematic v0.2): 1S protection board + power-bank charger/5 V boost module replace TP4056 + boost (watch points listed); plain SHOOT button + two 3 mm LEDs (O-18 closed); RGB LED fixed on GPIO48 (O-19 closed). Hardware test `firmware/remote_hwtest`: MCU, battery sense, buzzer, motor, RGB LED, SHOOT button, green/red LEDs (after resoldering, they were fitted reversed) and OLED (0x3C on IO8/IO9, after repairing a lifted trace on the module) all pass; power-bank module: no light-load auto-off, runs while charging, 5 V starts by itself; TRIG/DET untested until J1 is wired |
| 0.15 | 2026-11-01 | Remote simplified: **no arming** (principle 8) — ARM switch, TEST and MENU buttons and the green/amber/red LEDs removed; DISARMED/ARMED states replaced by IDLE (§9.1); `arm_req`, TEST, CFG_GET/SET, JOG and CFG_VAL removed from the protocol (§7.3); config, calibration and test cycles on the node's USB console; a short SHOOT tap replaces TEST in §4; `t_link_lost_disarm` → `t_link_lost` (2 s, indication only); camera power control follows the link (§6.3). Remote hardware: ESP32-S3-DevKitC-1U with external antenna, on-board RGB LED as status indicator (FR-R14), SHOOT ring driven directly from GPIOs, same buzzer as the node, power path 18650 → TP4056 with protection → switch → 5 V boost → 5V0 (§11.2, ~16 h), NPN driver for the vibration motor; draft remote schematic (§12.2). FR-R04, FR-R05 and T-08 removed; O-5 closed; O-17, O-18 updated; O-19 added |
| 0.14 | 2026-10-23 | Stand-by bench results (§6.2): FULL alone never fires, HALF+FULL always, also after 10 min standby; keep-alive made optional and off by default (FR-N03, `t_keepalive_interval` = 0, §4, §6.3, §10, §11.3, T-03, T-31, O-3); camera does not enter standby while USB is connected (§10); J2 pin 2 / PW-01 / O-12: S-line sniffer and protocol research |
| 0.13 | 2026-10-15 | Bench results: HALF and FULL swapped on J3 / GPIO (orange = FULL = GPIO4/U3, red = HALF = GPIO5/U4, §6.2); S contact is ≈100 kΩ plus data, not 98 Ω, RS 100 Ω plan dropped and powering the camera from the node parked (PW-01, O-12); soldering fault on the FULL stage repaired |
| 0.12 | 2026-10-09 | S wire colour: brown on J2, blue on the camera side (same as BLU); identification rule added (§6.2) |
| 0.11 | 2026-10-09 | Camera connectors defined: J2 3-pin power + S, J3 4-pin signals, 2.54 mm, with camera-side and connector wire colours (§6.2); RS 100 Ω on J2 pin 2 to BLU |
| 0.10 | 2026-10-09 | Single fuse behind the BMS, then main switch, then camera and node buck in parallel (§11.3); camera-branch fuse and selectivity (T-16) dropped |
| 0.9 | 2026-10-09 | PAIR switch removed; units bound at compile time by MAC, PMK, LMK and channel (§7.4); PAIRING state, PAIR_REQ/PAIR_ACK and the node PAIR button removed |
| 0.8 | 2026-10-09 | Remote: illuminated SHOOT button with red/green readiness ring (FR-R13, O-18); two use cases (manual remote, RLC TRIG) reconfirmed; active buzzer 2 mA direct drive |
| 0.7 | 2026-10-09 | Buzzer on node (GPIO15) and remote; FR-R09 changed, FR-N18 and O-17 added |
| 0.6 | 2026-10-08 | Node uses the ESP32-S3-DevKitC-1 dev board fed at 5 V by the buck (§11.3, §12.1); power budget updated (~37.5 Wh worst case, ~49 Wh with margin); RR-06 relaxed; O-16 added; draft node schematic added; status LED = dev board's on-board WS2812 (§9.3) |
| 0.5 | 2026-10-07 | Option B selected (O-1 closed). Measured D3000 shutter wiring and levels (§6.2), camera on/off line (§6.3), EN-EL9a S contact = NTC ~98 Ω (PW-01). O-13 closed, O-12 updated, O-14 added. Camera power control specified (§6.3, FR-N14…N17, parameters, STATUS bit, tests T-40…T-45), default off |
| 0.4 | 2026-09-28 | Remote no longer powered by the RLC base: own 1S 18650 battery sized for a launch day (§11.2). RLC interface now fully optically isolated with cable detect (§8). Node pack (2S2P 18650, ~50 Wh) powers node and camera; camera battery removed and leads soldered to its terminals (§11.3). Launch-day power basis defined (§11.1). O-7 and O-10 closed; O-11 and O-12 added; endurance and power tests added |
