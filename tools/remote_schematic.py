#!/usr/bin/env python3
"""CRIG remote schematic -> SVG (+ PNG via cairosvg), same style as the RLC and node schematics.

Source: camera-rig-functional-spec.md v0.15 §8, §9.2, §11.2, §12.2 and the owner's answers
of 2026-11-01: ESP32-S3-DevKitC-1U (N16R8, external antenna), 18650 -> TP4056 with
protection -> switch -> 5 V boost -> 5V0, on-board RGB LED as status indicator, SHOOT
ring driven directly from GPIOs, same active buzzer as the node, no ARM/TEST/MENU.
The SHOOT button type is not chosen yet (O-18): the ring is drawn as a placeholder.

    pip install cairosvg
    python3 tools/remote_schematic.py crig_remote_schematic
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schematic_svg import *  # noqa: E402,F401,F403

OUT = sys.argv[1] if len(sys.argv) > 1 else 'crig_remote_schematic'
W, H = 1910, 1220

C_IN = C_SENSE      # inputs to U1
C_OUT = C_GATE      # U1 outputs
C_BUS = C_DATA      # I2C + WS2812 data
C_ISO = '#f08c00'   # RLC side of the isolation barrier


def npn(x, y, name, part):
    """NPN: base at (x-34,y), collector top at (x+10,y-36), emitter bottom at (x+10,y+36)."""
    out.append(f'<circle cx="{x}" cy="{y}" r="22" fill="white" stroke="{INK}" stroke-width="2"/>')
    line(x - 34, y, x - 8, y, C_OUT)
    line(x - 8, y - 14, x - 8, y + 14, INK, 3)
    wire([(x - 8, y - 6), (x + 10, y - 18), (x + 10, y - 36)])
    wire([(x - 8, y + 6), (x + 10, y + 18), (x + 10, y + 36)])
    poly([(x + 10, y + 18), (x + 1, y + 17), (x + 6, y + 10)], INK, INK, 1)
    text(x + 28, y - 2, name, 12, bold=True)
    text(x + 28, y + 12, part, 10, fill=GREY)


def switch_no(x, y, label=None):
    """Momentary NO push contact between (x,y) and (x+44,y)."""
    ocirc(x + 4, y)
    ocirc(x + 40, y)
    line(x - 6, y - 14, x + 50, y - 14)
    line(x + 22, y - 14, x + 22, y - 26)
    line(x + 14, y - 26, x + 30, y - 26)
    if label:
        text(x + 22, y + 18, label, 10, 'middle', fill=GREY)


begin(W, H, 'CRIG remote — schematic',
      'Rocket liftoff camera rig · v0.4 2026-11-03 (as built + ARM line, DIN-5 connector, spec v0.18) · hardware test: MCU, VBAT, buzzer, motor, RGB, SHOOT, LEDs, OLED pass; '
      'J1 / TRIG / DET not wired yet · spec v0.18 §8, §9.2, §11.2, §12.2')

# =================================================================== A · POWER
frame(20, 60, 600, 450, 'A · POWER (18650, 1S PROTECTION, CHARGE + 5 V BOOST, SENSE)')
rect(40, 110, 120, 84)
text(100, 134, 'BT1  18650', 13, 'middle', True)
text(100, 150, 'LS18650-3', 11, 'middle')
text(100, 164, '3 350 mAh, no prot.', 10, 'middle', fill=GREY)
text(100, 178, 'in BAT120 holder', 10, 'middle', fill=GREY)
# TP4056 module
rect(200, 100, 150, 130)
text(275, 124, 'U2  1S protection', 13, 'middle', True)
text(275, 140, 'AliExpress BMS, variant S1', 10, 'middle')
text(275, 154, 'over-charge / -discharge', 10, 'middle', fill=GREY)
text(275, 168, '/ -current cut-off', 10, 'middle', fill=GREY)
text(275, 182, 'strip, soldered on the cell', 10, 'middle', fill=GREY)
text(206, 132, 'B+', 9, fill=GREY)
text(206, 214, 'B−', 9, fill=GREY)
text(344, 132, 'P+', 9, 'end', fill=GREY)
text(344, 214, 'P−', 9, 'end', fill=GREY)
wire([(160, 128), (200, 128)], C_VBAT, 2.5)
wire([(160, 176), (180, 176), (180, 210), (200, 210)])
text(166, 122, '+', 11, bold=True, fill=C_VBAT)
wire([(350, 210), (370, 210), (370, 240)])
gnd(370, 240)
# SW1
wire([(350, 128), (392, 128)], C_VBAT, 2.5)
ocirc(392, 128)
ocirc(428, 128)
line(392, 128, 428, 114)
text(410, 150, 'SW1 toggle', 11, 'middle')
text(410, 164, 'in P+, power on/off', 10, 'middle', fill=GREY)
wire([(428, 128), (600, 128)], C_VBAT, 2.5)
dot(470, 128, C_VBAT)
dot(540, 128, C_VBAT)
tag(520, 112, 'VBAT_SW', C_VBAT, size=12)
text(600, 144, '3.0–4.2 V', 10, 'end', fill=GREY)
text(600, 158, '→ M1 (E)', 10, 'end', fill=GREY)
# boost U3
wire([(470, 128), (470, 190), (230, 190), (230, 300)], C_VBAT)
rect(160, 300, 170, 90)
text(245, 322, 'U3  charger + 5 V boost', 12, 'middle', True)
text(245, 338, 'power-bank module LX-LBCE1', 10, 'middle', fill=GREY)
text(245, 352, '5V 2.4A, USB-C charge', 10, 'middle', fill=GREY)
text(245, 366, '("2.4A Weld USB Blue")', 9, 'middle', fill=GREY)
text(168, 314, 'B+', 9, fill=GREY)
text(322, 380, 'OUT', 9, 'end', fill=GREY)
text(245, 384, 'B− (= U2 P−, GND)', 9, 'middle', fill=GREY)
line(245, 390, 245, 404)
gnd(245, 404)
wire([(330, 362), (380, 362), (380, 440), (420, 440)], C_5V, 2.5)
tag(426, 436, '+5V', C_5V)
text(426, 452, '→ U1 5V0', 10, fill=GREY)
# divider 100k / 100k
res(540, 128, 540, 230, C_VBAT)
text(552, 184, '100 k', 11)
dot(540, 230, C_IN)
res(540, 230, 540, 320)
text(552, 280, '100 k', 11)
gnd(540, 320)
wire([(540, 230), (500, 230)], C_IN)
cap(500, 230, 500, 290, C_IN)
text(488, 264, '100 n', 10, 'end')
gnd(500, 290)
wire([(540, 230), (600, 230)], C_IN)
text(552, 222, 'VBAT_ADC → IO1', 10, bold=True, fill=C_IN)
lines(40, 478, ['GND = U2 P− (switched by U2). SW1 off isolates U3 from the cell (no standby drain) — charge with SW1 ON.',
                'U3 checked 2026-11-03: no light-load auto-off, runs while charging (pass-through), 5 V starts by itself.'],
      10, fill=GREY, italic=True)

# =================================================================== B · MCU
frame(640, 60, 380, 780, 'B · MCU (ESP32-S3 dev board)')
BX, BY, BW, BH = 780, 110, 160, 690
rect(BX, BY, BW, BH)
text(BX + BW / 2, 400, 'U1', 18, 'middle', True)
lines(BX + BW / 2, 422, ['ESP32-S3-', 'DevKitC-1U', 'WROOM-1U', 'N16R8'], 11, 'middle', dy=14)
lines(BX + BW / 2, 494, ['u.FL → ext. antenna', 'USB: flash, console', 'on-board LDO 3V3'], 10, 'middle', fill=GREY, dy=13)
lines(BX + BW / 2, 550, ['on-board RGB:', 'IO48 (fixed,', 'as RLC) ✓'], 10, 'middle', fill=C_BUS, dy=13)
lines(BX + BW / 2, 604, ['spare:', 'IO2 IO7 IO13 IO14', 'IO16 IO17 IO18', 'IO21 IO39–42 IO47'], 10, 'middle', fill=GREY, dy=13)

left = [('IO1', 'VBAT_ADC', C_IN), ('IO4', 'SHOOT_BTN', C_IN), ('IO5', 'TRIG_IN', C_IN),
        ('IO6', 'ARM_IN', C_IN), ('IO8', 'I2C_SDA', C_BUS), ('IO9', 'I2C_SCL', C_BUS),
        ('3V3', '+3V3  (out)', C_3V3), ('5V0', '+5V', C_5V)]
right = [('IO10', 'RING_GRN', C_OUT), ('IO11', 'RING_RED', C_OUT), ('IO12', 'VIB', C_OUT),
         ('IO15', 'BUZZER', C_OUT), ('GND', 'GND', INK)]
step = 66
for i, (pin, netname, c) in enumerate(left):
    y = BY + 40 + i * step
    wire([(660, y), (BX, y)], c, 2)
    text(BX + 6, y + 4, pin, 10, fill=GREY)
    text(664, y - 6, netname, 11, bold=True)
text(BX + 6, BY + 40 + step + 16, 'int. pull-up', 9, fill=GREY)
for i, (pin, netname, c) in enumerate(right):
    y = BY + 40 + i * step
    text(BX + BW - 6, y + 4, pin, 10, 'end', fill=GREY)
    if netname == 'GND':
        wire([(BX + BW, y), (BX + BW + 40, y)])
        gnd(BX + BW + 40, y)
        continue
    wire([(BX + BW, y), (1014, y)], c, 2)
    text(1014, y - 6, netname, 11, 'end', True)
lines(660, 818, ['GPIO 1 is ADC1 (works with ESP-NOW active).',
                 'IO48 = on-board RGB LED. IO38 not used.'], 10, fill=GREY, italic=True)

# =================================================================== C · RLC INTERFACE
frame(1040, 60, 850, 450, 'C · RLC INTERFACE (5-pin DIN 180°: TRIG + ARM, two optocouplers)')
rect(1070, 96, 130, 314)
text(1135, 116, 'J1  DIN-5 180°', 13, 'middle', True)
text(1135, 130, 'AD322 female chassis', 10, 'middle', fill=GREY)
for y, sname in ((150, '1 TRIG+  ← GPIO39'), (270, '3 ARM+  ← GPIO41'), (360, '2 COM−  ← GND'), (392, '4, 5 n.c.')):
    text(1078, y + 4, sname, 10, fill=GREY)
# isolation barrier
line(1440, 100, 1440, 420, C_ISO, 1.5, '8 5')
text(1440, 96, 'isolation barrier ≥ 10 MΩ (IF-05)', 10, 'middle', fill=C_ISO)
text(1425, 418, 'RLC side', 10, 'end', fill=C_ISO)
text(1455, 418, 'remote side', 10, fill=GREY)
# common return bus pin 3 -> both cathode lines
wire([(1200, 360), (1330, 360), (1330, 200)], C_ISO)
dot(1330, 320, C_ISO)
dot(1330, 200, C_ISO)
text(1255, 352, 'COM− (RLC GND)', 9, fill=C_ISO)


def opto(ya, pin_label, uname, dname, net, gpio, note):
    """One channel: anode line at y=ya from J1, LED ya+10..ya+40, cathode line ya+50 to the COM bus."""
    yc = ya + 50
    wire([(1200, ya), (1400, ya), (1400, ya + 10)], C_ISO)
    diode(1400, ya + 10, 1400, ya + 40, 'led', INK, body=C_ISO, side=1)
    wire([(1400, ya + 40), (1400, yc), (1290, yc)], C_ISO)
    dot(1290, ya, C_ISO)
    diode(1290, yc, 1290, ya, 'std', C_ISO)
    text(1278, ya + 22, dname, 10, 'end', True)
    text(1278, ya + 34, '1N4148', 9, 'end', fill=GREY)
    text(1212, ya - 6, pin_label, 9, fill=GREY)
    rect(1372, ya - 12, 160, 76, FRAME, 1.5, rx=6, dash='5 4')
    text(1432, ya - 18, uname, 11, 'end', True)
    PX, PY = 1480, ya + 25
    line(PX - 8, PY - 16, PX - 8, PY + 16, INK, 3)
    wire([(PX - 8, PY - 6), (PX + 12, PY - 20), (PX + 12, PY - 45)])
    wire([(PX - 8, PY + 6), (PX + 12, PY + 20), (PX + 12, PY + 40)])
    poly([(PX + 12, PY + 20), (PX + 3, PY + 19), (PX + 8, PY + 12)], INK, INK, 1)
    for k in (-6, 6):
        line(1420, PY + k, 1462, PY + k, INK, 1.2)
        poly([(1462, PY + k), (1455, PY + k - 3), (1455, PY + k + 3)], INK, INK, 1)
    gnd(PX + 12, PY + 40)
    yo = PY - 45
    wire([(PX + 12, yo), (1870, yo)], C_IN)
    dot(1600, yo, C_IN)
    res(1600, yo, 1600, yo - 44, C_3V3, body=26)
    tag(1600, yo - 50, '+3V3', C_3V3, 'middle', 10)
    text(1612, yo - 18, '10 k', 10)
    dot(1660, yo, C_IN)
    cap(1660, yo, 1660, yo + 44, C_IN)
    text(1674, yo + 26, '10 n', 10)
    gnd(1660, yo + 44)
    text(1870, yo - 6, f'{net} → {gpio}', 11, 'end', True, C_IN)
    text(1870, yo + 14, note, 9, 'end', fill=GREY)


opto(150, '470 Ω in base', 'U4 PC817', 'D1', 'TRIG_IN', 'IO5', 'LOW = TRIG on')
opto(270, '470 Ω in base', 'U6 PC817', 'D3', 'ARM_IN', 'IO6', 'pattern: none / 1 Hz / 10 Hz')
lines(1060, 446, ['Pins 1–3 reach only the two PC817 LEDs and D1/D3: no copper path to the remote (IF-05, T-15). Cable: AD302 both ends, straight through.',
                  'ARM (§8.3): no pulses = RLC not connected / hung; 1 Hz (100 ms on) = connected; 10 Hz = armed → PREPARE.',
                  'TRIG is accepted only while ARM decodes as armed (§8.6). Filters 10 k × 10 n ≈ 0.1 ms.'],
      10, fill=GREY, italic=True)

# =================================================================== D · SHOOT + OLED
frame(1040, 530, 850, 310, 'D · SHOOT BUTTON + STATUS LEDS (as built), OLED')
rect(1060, 570, 470, 250, FRAME, 1.5, rx=6, dash='5 4')
text(1070, 588, 'S1 plain push button + two separate 3 mm LEDs (as built 2026-11-03)', 10, bold=True, fill=GREY)
text(1076, 632, 'SHOOT_BTN → IO4', 11, bold=True, fill=C_IN)
text(1076, 646, 'LOW = pressed (int. pull-up)', 9, fill=GREY)
wire([(1076, 680), (1110, 680)], C_IN)
switch_no(1110, 680)
wire([(1154, 680), (1180, 680), (1180, 692)])
gnd(1180, 692)
for x, netname, gpio, col, name in ((1330, 'RING_GRN', 'IO10', C_LAMP_G, 'green'),
                                    (1450, 'RING_RED', 'IO11', C_LAMP_R, 'red')):
    text(x, 614, netname, 10, 'middle', True)
    text(x, 628, f'← {gpio}', 10, 'middle', fill=C_OUT)
    wire([(x, 634), (x, 650)], C_OUT)
    res(x, 650, x, 700, INK, body=30)
    text(x + 12, 680, 'R', 10)
    diode(x, 700, x, 750, 'led', INK, body=col, side=-1)
    line(x, 750, x, 770)
    gnd(x, 770)
    text(x - 14, 730, name, 9, 'end', fill=col)
lines(1076, 742, ['Direct GPIO drive, no transistors.', 'Anode to the resistor, cathode', '(flat side, short leg) to GND.',
                  '2026-11-03: fitted reversed first,', 'resoldered; both OK.'], 9, fill=GREY, italic=True, dy=12)
# OLED
OX, OY = 1640, 600
rect(OX, OY, 200, 150)
text(OX + 100, OY + 70, 'U5  OLED 0.96"', 12, 'middle', True)
lines(OX + 100, OY + 88, ['VMA438, SSD1306', '128 × 64, I²C 0x3C', 'pull-ups on module'], 10, 'middle', fill=GREY, dy=13)
op = [('VCC', '+3V3', C_3V3), ('GND', 'GND', INK), ('SCL', 'SCL ← IO9', C_BUS), ('SDA', 'SDA ↔ IO8', C_BUS)]
for i, (pin, netname, c) in enumerate(op):
    y = OY + 20 + i * 14 + (0 if i < 2 else 0)
for i, (pin, netname, c) in enumerate(op):
    y = OY + 166 + i * 0
pins_y = [OY + 18, OY + 38, OY + 120, OY + 140]
for (pin, netname, c), y in zip(op, pins_y):
    text(OX + 6, y + 4, pin, 9, fill=GREY)
    if netname == 'GND':
        wire([(OX, y), (OX - 40, y)])
        gnd(OX - 40, y)
        continue
    wire([(OX, y), (OX - 30, y)], c, 2)
    text(OX - 34, y + 4, netname, 10, 'end', True, c)

# =================================================================== E · HAPTICS + BUZZER
frame(20, 530, 600, 310, 'E · VIBRATION MOTOR, BUZZER')
# motor from VBAT_SW, low side Q1
MX = 200
tag(MX, 572, 'VBAT_SW', C_VBAT, 'middle', 11)
wire([(MX, 578), (MX, 600)], C_VBAT, 2.5)
dot(MX, 600, C_VBAT)
out.append(f'<circle cx="{MX}" cy="630" r="20" fill="white" stroke="{INK}" stroke-width="2"/>')
text(MX, 636, 'M', 16, 'middle', True)
line(MX, 600, MX, 610)
line(MX, 650, MX, 670)
dot(MX, 670)
text(MX - 30, 626, 'M1', 12, 'end', True)
text(MX - 30, 640, 'ADA1201', 10, 'end', fill=GREY)
text(MX - 30, 654, '~65 mA', 10, 'end', fill=GREY)
# flyback D2 across motor (anode at bottom, cathode at VBAT)
wire([(MX, 600), (MX + 50, 600)], C_VBAT)
diode(MX + 50, 670, MX + 50, 600)
wire([(MX, 670), (MX + 50, 670)])
text(MX + 62, 632, 'D2', 10, bold=True)
text(MX + 62, 644, '1N4148', 9, fill=GREY)
# Q1
QX, QY = MX - 10, 706
line(MX, 670, QX + 10, QY - 36)
npn(QX, QY, 'Q1', 'BC337')
gnd(QX + 10, QY + 36)
res(QX - 34, QY, QX - 94, QY, C_OUT, body=30)
text(QX - 64, QY - 12, '1 k', 10, 'middle')
dot(QX - 94, QY, C_OUT)
res(QX - 94, QY, QX - 94, QY + 56, INK, body=26)
text(QX - 108, QY + 36, '10 k', 10, 'end')
gnd(QX - 94, QY + 56)
wire([(QX - 94, QY), (44, QY)], C_OUT)
text(44, QY - 8, 'VIB ← IO12', 11, bold=True, fill=C_OUT)
# buzzer
BZX = 470
rect(BZX - 50, 600, 100, 60, INK, 2, 'white')
text(BZX, 624, 'BZ1', 12, 'middle', True)
text(BZX, 640, 'active, 3.3 V', 10, 'middle')
text(BZX, 652, 'same as node', 9, 'middle', fill=GREY)
text(BZX - 44, 594, '+', 12, bold=True)
wire([(BZX - 40, 600), (BZX - 40, 580), (BZX - 120 + 20, 580)], C_OUT)
text(BZX - 100, 574, 'BUZZER ← IO15', 10, 'start', True, C_OUT)
line(BZX, 660, BZX, 680)
gnd(BZX, 680)
text(BZX, 714, '~2 mA, direct drive', 9, 'middle', fill=GREY)
text(BZX, 726, 'fixed pitch (internal osc.)', 9, 'middle', fill=GREY)
lines(40, 790, ['Motor on VBAT_SW (not the boost): ~65 mA at 3.7 V, too much for a GPIO → Q1.',
                'Buzzer: + to IO15, − to GND; never on during a burst (FR-R09).'], 10, fill=GREY, italic=True)

# =================================================================== NOTES
rect(20, 860, 1000, 340, FRAME, 1.5, rx=8)
text(32, 884, 'NOTES', 15, bold=True, fill=GREY)
notes = [
    '1. All GPIO are 3.3 V logic. Not used: IO0/IO3/IO45/IO46 (strapping), IO19/20 (USB), IO35–37 (octal PSRAM),',
    '    IO43/44 (UART0 console), IO38. IO48 = on-board RGB LED. IO6 (ARM_IN), U4/U6 and J1 not wired yet (2026-11-03).',
    '2. No ARM switch, no TEST or MENU buttons, no discrete LEDs (spec v0.15). Status = on-board RGB LED (FR-R14).',
    '3. Power: BT1 → U2 (1S protection, P+/P−) → SW1 (in P+) → VBAT_SW → U3 B+ (charger + 5 V boost) → U1 5V0; +3V3 = U1 LDO,',
    '    which feeds the OLED, pull-ups and ring LEDs. ~185 mA at the cell, ~16 h on 3 350 mAh (§11.2).',
    '4. Battery sense 1 : 2, 4.2 V → 2.1 V at IO1 (reads 3.85 V, 2026-11-03); warn 3.5 V, cutoff 3.2 V (firmware).',
    '5. Ring LEDs and the buzzer run straight from GPIOs; only the vibration motor has a transistor.',
    '6. SHOOT = SHOOT_BTN OR (TRIG while ARM = armed) (FR-R02, §8.6); both inactive once after boot (FR-R03).',
    '7. Same 3.3 V active buzzer as the node (O-17: tone patterns still open).',
    '8. Wire colours as built: IO1 geel/blauw, IO4 groen/zwart, IO5 grijs/wit, IO8 blauw/wit, IO9 wit/blauw,',
    '    IO10 groen/zwart (same as IO4!), IO11 blauw/paars, IO12 wit/grijs, IO15 geel/oranje.',
]
lines(32, 910, notes, 11, dy=24)

# =================================================================== legend
frame(1040, 860, 850, 340, 'WIRE COLOURS (by function)')
legend = [(C_VBAT, 2.5, None, 'VBAT / VBAT_SW — 18650 cell'),
          (C_5V, 2.5, None, '+5V — from U3 (power-bank module)'),
          (C_3V3, 2.5, None, '+3V3 — U1 on-board LDO output'),
          (C_IN, 2.5, None, 'input to U1 (switch, sense, TRIG, ARM)'),
          (C_OUT, 2.5, None, 'U1 output (ring LEDs, motor, buzzer)'),
          (C_BUS, 2.5, None, 'I²C bus / RGB LED'),
          (C_ISO, 2.5, None, 'RLC side of the optocoupler')]
for i, (c, w, d, s) in enumerate(legend):
    lx = 1060 + (i % 2) * 400
    ly = 910 + (i // 2) * 26
    line(lx, ly - 4, lx + 40, ly - 4, c, w + 0.5, d)
    text(lx + 52, ly, s, 11)
text(1060, 1030, 'Remote pin map (as built)', 13, bold=True)
pmap = [('Battery ADC', 'IO1'), ('SHOOT button', 'IO4'), ('TRIG (opto)', 'IO5'), ('RLC ARM (opto)', 'IO6'),
        ('I²C SDA / SCL (OLED)', 'IO8 / IO9'), ('LED green / red', 'IO10 / IO11'),
        ('Vibration motor', 'IO12'), ('Buzzer', 'IO15'), ('RGB status LED', 'IO48')]
for i, (k, v) in enumerate(pmap):
    lx = 1060 + (i % 2) * 400
    ly = 1056 + (i // 2) * 24
    text(lx, ly, k, 11, fill=GREY)
    text(lx + 210, ly, v, 11, bold=True)

finish(OUT, W)
