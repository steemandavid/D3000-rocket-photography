#!/usr/bin/env python3
"""Capture both consoles at once:  cap2.py <seconds> [reset] [cmd-at-time ...]  (cmd as  t:unit:line)"""
import sys, time, threading, serial
PORTS = {'node': '/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CCC062602-if00',
         'remote': '/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CCC062258-if00'}
secs = float(sys.argv[1]); reset = len(sys.argv) > 2 and sys.argv[2] == 'reset'
cmds = [a.split(':', 2) for a in sys.argv[3 if reset else 2:]]
ser = {}
for u, p in PORTS.items():
    s = serial.Serial(); s.port, s.baudrate, s.timeout, s.dtr, s.rts = p, 115200, 0.05, False, False; s.open(); ser[u] = s
t0 = time.time()
if reset:
    for s in ser.values(): s.rts = True
    time.sleep(0.1)
    for s in ser.values(): s.rts = False
out = []
def rd(u):
    buf = b''
    while time.time() - t0 < secs:
        buf += ser[u].read(512)
        while b'\n' in buf:
            l, buf = buf.split(b'\n', 1)
            out.append((time.time() - t0, u, l.decode(errors='replace').rstrip()))
th = [threading.Thread(target=rd, args=(u,)) for u in ser]
for t in th: t.start()
for tc, u, line in sorted(cmds, key=lambda c: float(c[0])):
    while time.time() - t0 < float(tc): time.sleep(0.01)
    ser[u].write((line + '\n').encode())
for t in th: t.join()
for t, u, l in sorted(out): print(f'{t:7.3f} {u:6} {l}')
