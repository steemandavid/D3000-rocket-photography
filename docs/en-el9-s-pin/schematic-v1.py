import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

fig, ax = plt.subplots(figsize=(22, 14), dpi=150)
ax.set_xlim(0, 22); ax.set_ylim(0, 14); ax.axis('off')
LW = 1.5

def line(*pts, ls='-'):
    xs, ys = zip(*pts)
    ax.plot(xs, ys, color='k', lw=LW, ls=ls, zorder=2, solid_capstyle='round')

def dot(x, y):
    ax.plot([x], [y], 'o', color='k', ms=4.5, zorder=5)

def txt(x, y, s, size=9, ha='left', va='center', w='normal', c='k'):
    ax.text(x, y, s, fontsize=size, ha=ha, va=va, fontweight=w, color=c)

def res(p1, p2, ref, val, w=0.62, h=0.26):
    x1, y1 = p1; x2, y2 = p2
    dx, dy = x2-x1, y2-y1; ln = (dx*dx + dy*dy) ** 0.5
    ux, uy = dx/ln, dy/ln; px, py = -uy, ux
    cx, cy = (x1+x2)/2, (y1+y2)/2
    body = [(cx-ux*w/2+px*h/2, cy-uy*w/2+py*h/2), (cx+ux*w/2+px*h/2, cy+uy*w/2+py*h/2),
            (cx+ux*w/2-px*h/2, cy+uy*w/2-py*h/2), (cx-ux*w/2-px*h/2, cy-uy*w/2-py*h/2)]
    ax.add_patch(plt.Polygon(body, fill=False, ec='k', lw=LW, zorder=4))
    line((x1, y1), (cx-ux*w/2, cy-uy*w/2)); line((cx+ux*w/2, cy+uy*w/2), (x2, y2))
    if abs(dx) >= abs(dy):
        txt(cx, cy+0.32, ref, size=8, ha='center'); txt(cx, cy-0.38, val, size=8, ha='center')
    else:
        txt(cx+0.30, cy+0.14, ref, size=8); txt(cx+0.30, cy-0.16, val, size=8)

def gnd(x, y):
    line((x, y), (x, y-0.22))
    for i, wd in enumerate((0.46, 0.30, 0.14)):
        line((x-wd/2, y-0.24-i*0.13), (x+wd/2, y-0.24-i*0.13))

def fuse(p1, p2, ref, val):
    res(p1, p2, '', '')
    line(p1, p2)
    x1, y1 = p1; x2, y2 = p2
    cx, cy = (x1+x2)/2, (y1+y2)/2
    txt(cx, cy-0.40, ref+' '+val, size=8, ha='center')

def pin(x, y, name):
    ax.plot([x], [y], 'o', ms=7, mfc='white', mec='k', mew=1.4, zorder=5)
    txt(x-0.22, y, name, size=8.5, ha='right')

def hdr(name, x, ys, nets):
    for i, (y, n) in enumerate(zip(ys, nets)):
        ax.add_patch(plt.Rectangle((x-0.13, y-0.13), 0.26, 0.26, fill=False, ec='k', lw=1.2, zorder=4))
        txt(x+0.30, y, chr(65+i)+' = '+n, size=7.5)
    txt(x, max(ys)+0.45, name, size=9, w='bold', ha='center')

def jarc(x, y1, y2, label):
    import numpy as np
    t = np.linspace(0, 1, 30)
    xs = [x - 0.24 - 0.5*np.sin(np.pi*u) for u in t]
    ys = [y1 + (y2-y1)*u for u in t]
    ax.plot(xs, ys, color='tab:red', lw=1.2, ls=(0, (4, 3)), zorder=3)
    txt(x-0.90, (y1+y2)/2, label, size=7.5, ha='right', c='tab:red')

def block(x, y, w, h, title):
    ax.add_patch(plt.Rectangle((x, y), w, h, fill=False, ec='0.55', lw=1.0, ls=(0, (5, 3)), zorder=1))
    txt(x+0.15, y+h-0.24, title, size=9.5, w='bold', c='0.35')

def outlab(x, y, label):
    ax.annotate('', xy=(x+0.18, y), xytext=(x, y),
                arrowprops=dict(arrowstyle='-|>', lw=1.1, color='k'))
    txt(x+0.28, y, label, size=8.5, w='bold')

def instub(x, y, label):
    ax.annotate('', xy=(x, y), xytext=(x+0.55, y),
                arrowprops=dict(arrowstyle='-|>', lw=1.1, color='k'))
    txt(x-0.08, y, label, size=8.5, ha='right', w='bold')

# ---- title & ground bus ----
txt(0.3, 13.6, "CRIG - EN-EL9(a) S-line sniff & spoof rig - schematic v1", size=15, w='bold')
txt(0.3, 13.15, "ESP32-S3-DevKitC-1 (N16R8) | 2026-10-25 | companion: docs/en-el9-s-pin/EN-EL9_S-pin_findings.md", size=9)
line((1.4, 0.5), (20.1, 0.5))
gnd(10.5, 0.5)
txt(11.0, 0.5, "BLK - common ground (camera -, battery -, VIN-, ESP32 GND)", size=8.5, va='center')

# ---- connectors ----
txt(1.4, 11.0, "J1 CAMERA BAY", size=9.5, w='bold')
pin(1.4, 10.3, "S"); pin(1.4, 5.5, "+"); pin(1.4, 0.9, "-")
line((1.4, 0.9), (1.4, 0.5))
txt(1.4, 8.85, "J2 REAL BATTERY", size=9.5, w='bold')
pin(1.4, 8.3, "S"); pin(1.4, 4.8, "+"); pin(1.4, 1.1, "-")
line((1.4, 1.1), (1.4, 0.5))
txt(1.4, 3.45, "J3 VIN 7.4-8.4 V", size=9.5, w='bold')
pin(1.4, 2.9, "+"); pin(1.4, 1.3, "-")
line((1.4, 1.3), (1.4, 0.5))

# ---- S path: shunt + J_MODE ----
line((1.4, 10.3), (6.6, 10.3))
dot(2.8, 8.3)
line((1.4, 8.3), (2.8, 8.3))
res((2.8, 8.3), (4.6, 8.3), "R1", "100 R")
line((4.6, 8.3), (6.6, 8.3))
line((2.8, 8.3), (2.8, 7.4), (6.6, 7.4))
txt(5.9, 7.4, "S_BAT direct", size=7.5, ha='right', va='bottom')
hdr("J_MODE 1x3", 6.6, [10.3, 8.3, 7.4], ["S_CAM", "S_LINK", "S_BAT direct"])
jarc(6.6, 8.3, 10.3, "A-B: SNIFF via 100 R")
jarc(6.6, 7.4, 8.3, "B-C: PASS 0 R")
txt(6.25, 6.72, "jumper off: SPOOF", size=7.5, c='tab:red', ha='center')
line((6.6, 10.3), (9.6, 10.3))
outlab(9.6, 10.3, "S_CAM")
line((6.9, 8.3), (9.6, 8.3))
outlab(9.6, 8.3, "S_BAT")

# ---- B+ path: fuse + J_PWR ----
line((1.4, 5.5), (6.0, 5.5))
line((1.4, 4.8), (3.0, 4.8), (3.0, 6.35), (6.0, 6.35))
fuse((2.0, 2.9), (3.8, 2.9), "F1", "2.5 A")
line((1.4, 2.9), (2.0, 2.9))
line((3.8, 2.9), (5.0, 2.9), (5.0, 4.5), (6.0, 4.5))
hdr("J_PWR 1x3", 6.0, [6.35, 5.5, 4.5], ["B+_BAT", "B+_CAM", "F1 out"])
jarc(6.0, 5.5, 6.35, "A-B: battery powers cam")
jarc(6.0, 4.5, 5.5, "B-C: VIN powers cam (SPOOF)")

# ---- mode table ----
txt(7.3, 2.55, "MODES", size=9, w='bold')
txt(7.3, 2.15, "SNIFF : J_MODE A-B | J_PWR A-B | real battery inserted", size=8.5)
txt(7.3, 1.75, "PASS  : J_MODE B-C | J_PWR A-B | baseline, shunt bypassed", size=8.5)
txt(7.3, 1.35, "SPOOF : J_MODE off | J_PWR B-C | battery removed, VIN feeds camera", size=8.5)
txt(7.3, 0.85, "ESP32 on USB (PC/laptop) | camera USB unplugged | GPIO10 low = driver Hi-Z", size=8.5, c='0.35')

# ---- RX front end block ----
block(11.8, 10.2, 6.4, 3.1, "RX front end - high-Z, dual side (direction via R1 shunt)")
instub(12.0, 12.75, "S_CAM")
res((12.0, 12.75), (12.9, 12.75), "R2", "100 k")
instub(12.0, 11.75, "S_BAT")
res((12.0, 11.75), (12.9, 11.75), "R3", "100 k")
ax.add_patch(plt.Rectangle((13.1, 11.2), 2.0, 1.8, fill=False, ec='k', lw=LW, zorder=3))
txt(14.1, 12.55, "LM393", size=9, ha='center', w='bold')
txt(14.1, 12.15, "open collector", size=7, ha='center', c='0.35')
txt(13.18, 12.85, "1IN+", size=7.5); txt(13.18, 12.45, "1IN-", size=7.5)
txt(13.18, 11.85, "2IN+", size=7.5); txt(13.18, 11.45, "2IN-", size=7.5)
txt(15.02, 12.85, "OUT1", size=7.5, ha='right'); txt(15.02, 11.85, "OUT2", size=7.5, ha='right')
line((12.9, 12.75), (13.1, 12.75))
line((12.9, 11.75), (13.1, 11.75))
line((13.1, 12.45), (12.95, 12.45), (12.95, 11.45), (13.1, 11.45))
dot(12.95, 11.95)
line((12.95, 11.95), (12.95, 10.85), (16.5, 10.85), (16.5, 12.5), (16.9, 12.5))
dot(16.9, 12.5)
txt(16.75, 12.28, "VREF 0.58 V", size=7.5, ha='right')
line((15.0, 13.1), (17.9, 13.1))
txt(18.0, 13.1, "3V3", size=8, w='bold')
res((16.9, 13.1), (16.9, 12.5), "R4", "47 k", w=0.40, h=0.22)
res((16.9, 12.5), (16.9, 11.9), "R5", "10 k", w=0.40, h=0.22)
gnd(16.9, 11.9)
line((15.1, 12.75), (15.55, 12.75)); dot(15.55, 12.75)
res((15.55, 12.75), (15.55, 13.1), "R6", "10 k", w=0.40, h=0.22)
outlab(15.55, 12.75, "")
line((15.55, 12.75), (15.9, 12.75)); outlab(15.9, 12.75, "GPIO6")
line((15.1, 11.75), (15.55, 11.75)); dot(15.55, 11.75)
res((15.55, 11.75), (15.55, 12.1), "R7", "10 k", w=0.40, h=0.22)
line((15.55, 11.75), (15.9, 11.75)); outlab(15.9, 11.75, "GPIO8")
txt(12.0, 10.45, "R_hys 1 M from each OUT back to its IN+ (~60 mV) - not drawn", size=7, c='0.35')

# ---- ADC taps block ----
block(11.8, 8.4, 6.4, 1.7, "ADC taps")
instub(12.2, 9.4, "S_CAM")
res((12.2, 9.4), (13.0, 9.4), "R8", "10 k")
outlab(13.0, 9.4, "GPIO2 (ADC1_CH1)")
instub(15.0, 9.4, "S_BAT")
res((15.0, 9.4), (15.8, 9.4), "R9", "10 k")
outlab(15.8, 9.4, "GPIO4 (ADC1_CH3)")
instub(17.0, 9.4, "VIN+")
res((17.0, 9.4), (17.6, 9.4), "R10", "100 k", w=0.40, h=0.22)
dot(17.6, 9.4)
line((17.6, 9.4), (17.6, 9.75)); outlab(17.6, 9.75, "GPIO5")
res((17.6, 9.4), (17.6, 8.95), "R11", "27 k", w=0.40, h=0.22)
gnd(17.6, 8.95)

# ---- TX spoof driver block ----
block(11.8, 3.1, 6.4, 4.8, "TX spoof driver - J_DRV selects personality")
instub(12.2, 6.9, "GPIO7")
ax.add_patch(plt.Rectangle((12.2, 6.3), 1.8, 0.8, fill=False, ec='k', lw=LW, zorder=3))
txt(13.1, 6.85, "74LVC1G125", size=8, ha='center', w='bold')
txt(12.28, 7.05, "A", size=7.5); txt(13.9, 7.05, "Y", size=7.5, ha='right'); txt(13.3, 6.38, "OE", size=7.5)
line((13.1, 6.3), (13.1, 5.9))
res((13.1, 5.9), (13.1, 5.3), "R17", "10 k", w=0.40, h=0.22)
gnd(13.1, 5.3)
txt(13.25, 5.95, "OE low = Hi-Z", size=7, c='0.35')
line((14.0, 6.9), (14.2, 6.9))
res((14.2, 6.9), (15.0, 6.9), "R12", "1.8 k")
dot(15.1, 6.9)
res((15.1, 6.9), (15.1, 6.35), "R13", "1.2 k", w=0.40, h=0.22)
gnd(15.1, 6.35)
res((15.2, 6.9), (16.0, 6.9), "R14", "1 k")
line((16.0, 6.9), (17.0, 6.9))
instub(16.9, 6.1, "S_CAM")
instub(12.0, 4.6, "GPIO9")
res((12.0, 4.6), (12.8, 4.6), "R15", "4.7 k")
line((12.8, 4.6), (13.35, 4.6))
line((13.35, 4.35), (13.35, 4.85))
line((13.35, 4.75), (13.8, 5.15))
line((13.35, 4.45), (13.8, 4.05))
ax.annotate('', xy=(13.8, 4.05), xytext=(13.62, 4.23),
            arrowprops=dict(arrowstyle='-|>', lw=1.1, color='k'))
txt(13.95, 4.35, "Q1 BC547", size=8)
gnd(13.8, 4.05)
res((13.8, 5.15), (13.8, 5.75), "R16", "100 R", w=0.40, h=0.22)
line((13.8, 5.75), (14.5, 5.75), (14.5, 5.3), (17.0, 5.3))
hdr("J_DRV 1x3", 17.0, [6.9, 6.1, 5.3], ["PP node", "S_CAM", "OD node"])
jarc(17.0, 6.1, 6.9, "push-pull ~1.3 V")
jarc(17.0, 5.3, 6.1, "open-drain")
instub(12.2, 3.7, "S_CAM")
res((12.2, 3.7), (13.0, 3.7), "R18", "100 k")
gnd(13.3, 3.7)
txt(13.45, 3.7, "J_NTC: NTC emulation, jumper", size=7, c='0.35')

# ---- ESP32 box ----
ax.add_patch(plt.Rectangle((19.0, 3.1), 2.5, 10.2, fill=False, ec='k', lw=1.6, zorder=3))
txt(20.25, 13.0, "ESP32-S3", size=10, ha='center', w='bold')
txt(20.25, 12.65, "DevKitC-1 N16R8", size=8, ha='center')
pins = ["GPIO2  ADC1_CH1  S_CAM", "GPIO4  ADC1_CH3  S_BAT", "GPIO5  ADC1_CH4  B+ rail",
        "GPIO6  UART1 RX  cam side", "GPIO8  UART2 RX  bat side", "GPIO7  UART1 TX  driver",
        "GPIO9  OD driver base", "GPIO10 buffer OE (R17)", "", "GND    -> BLK",
        "3V3    -> R4/R6/R7", "", "console: native USB", "flash: /dev/serial/by-id"]
for i, s in enumerate(pins):
    txt(19.15, 11.9 - i*0.55, s, size=7.5)
line((20.25, 3.1), (20.25, 0.5))
dot(20.25, 0.5)
txt(20.4, 1.9, "camera USB unplugged", size=7, c='0.35', ha='center')

fig.savefig("/home/john/claudecode/projects/D3000-rocket-photography/docs/en-el9-s-pin/schematic-v1.png",
            bbox_inches='tight', facecolor='white')
print("saved")
