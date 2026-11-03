#!/usr/bin/env python3
"""Host side of firmware/ssniff: logs the S-line sniffer, saves every burst, decodes it as UART.

Usage: ./ssniff.py [-p /dev/serial/by-id/...] [-o OUTDIR] [--div 0.906] [--baud 9600]
  -o     output directory (default: captures/<date>-<time> next to this script)
  --div  divider factor GPIO2/S (phase 1, 10k + 96.8k: 96.8/106.8 = 0.906; phase 2, 10k only: 1.0)
Per burst: burst_NNN.csv (t_s, v_gpio, v_s) and a line in bursts.txt with the PC time, levels,
the bit string (run lengths at the baud rate) and the UART 8N1 decode in both polarities.
Everything the board prints goes to log.txt. Keys: + / - threshold, i info, s stats, q quit.
Like optotest.py it never touches DTR/RTS (which would reset the ESP32-S3).
Replay a saved burst offline: ./ssniff.py --decode burst_001.csv
"""
import argparse, datetime, glob, os, select, sys, termios, tty


def levels(v):
    s = sorted(v)
    lo, hi = s[len(s) // 10], s[len(s) * 9 // 10]
    if hi - lo < 0.2:                      # mostly one level: use extremes instead
        lo, hi = s[0], s[-1]
    return lo, hi


def runs(d):
    out, n = [], 1
    for a, b in zip(d, d[1:]):
        if a == b: n += 1
        else: out.append((a, n)); n = 1
    out.append((d[-1], n))
    return out


def bitstring(d, spb):
    return ''.join(str(lvl) * max(1, round(n / spb)) for lvl, n in runs(d))


def uart(d, spb, idle):
    """8N1, LSB first. idle = line level when idle (1 = normal UART, 0 = inverted)."""
    act, out, i, n = 1 - idle, [], 1, len(d)
    while i < n:
        if d[i - 1] == idle and d[i] == act:
            c = [i + (k + 0.5) * spb for k in range(10)]
            if c[-1] >= n: break
            b = [d[int(x)] for x in c]
            if b[0] == act:
                val = sum((1 if (b[1 + k] == idle) else 0) << k for k in range(8))
                out.append(('%02X' % val) if b[9] == idle else ('%02X!' % val))   # ! = framing error
                i = int(c[9])
                continue
        i += 1
    return ' '.join(out)


def analyse(v_s, rate, baud):
    lo, hi = levels(v_s)
    thr = (lo + hi) / 2
    d = [1 if x >= thr else 0 for x in v_s]
    spb = rate / baud
    return (f'lo {lo:.3f} V hi {hi:.3f} V thr {thr:.3f} V, {spb:.2f} samples/bit\n'
            f'  bits  {bitstring(d, spb)}\n'
            f'  uart idle-high {uart(d, spb, 1)}\n'
            f'  uart idle-low  {uart(d, spb, 0)}\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-p', '--port', default=(glob.glob('/dev/serial/by-id/usb-Espressif_*') or [None])[0])
    ap.add_argument('-o', '--out')
    ap.add_argument('--div', type=float, default=96.8 / 106.8)
    ap.add_argument('--baud', type=float, default=9600)
    ap.add_argument('--decode', help='decode a saved burst CSV and exit')
    a = ap.parse_args()

    if a.decode:
        rows = [l.split(',') for l in open(a.decode).read().split('\n')[1:] if l]
        t = [float(r[0]) for r in rows]
        print(analyse([float(r[2]) for r in rows], (len(t) - 1) / (t[-1] - t[0]), a.baud), end='')
        return
    if not a.port:
        sys.exit('no Espressif USB device found in /dev/serial/by-id')
    out = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'captures',
                                datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    log, summ = open(os.path.join(out, 'log.txt'), 'a'), open(os.path.join(out, 'bursts.txt'), 'a')

    fd = os.open(a.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    t = termios.tcgetattr(fd)
    t[0] = t[1] = t[3] = 0
    t[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    termios.tcsetattr(fd, termios.TCSANOW, t)
    old = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin.fileno())
    print(f'connected to {a.port}, saving to {out}, divider {a.div:.3f}\r\n'
          f'+/- threshold  i info  s stats  q quit\r', file=sys.stderr)
    pend, hdr, samples = b'', None, []
    try:
        while True:
            r, _, _ = select.select([fd, sys.stdin], [], [], 0.2)
            if sys.stdin in r:
                ch = sys.stdin.read(1)
                if ch in ('q', '\x03'): break
                if ch in '+-is': os.write(fd, ch.encode())
            if fd not in r: continue
            try: pend += os.read(fd, 65536)
            except BlockingIOError: continue
            *lines, pend = pend.split(b'\n')
            for raw in lines:
                l = raw.decode(errors='replace').strip()
                now = datetime.datetime.now().strftime('%H:%M:%S.%f')[:-3]
                if l.startswith('D ') and hdr:
                    h = l[2:]
                    samples += [int(h[i:i + 3], 16) for i in range(0, len(h) - 2, 3)]
                    continue
                log.write(f'{now} {l}\n'); log.flush()
                f = l.split()
                if f and f[0] == 'B' and len(f) == 7:
                    hdr, samples = f, []
                elif f and f[0] == 'E' and hdr:
                    n, t_us, rate, length, pre, ovf = hdr[1], int(hdr[2]), int(hdr[3]), int(hdr[4]), int(hdr[5]), hdr[6]
                    v_g = [s / 1000 for s in samples]
                    v_s = [x / a.div for x in v_g]
                    name = os.path.join(out, f'burst_{int(n):03d}.csv')
                    with open(name, 'w') as c:
                        c.write('t_s,v_gpio,v_s\n')
                        for i, (g, s) in enumerate(zip(v_g, v_s)):
                            c.write(f'{(i - pre) / rate:.6f},{g:.3f},{s:.3f}\n')
                    ok = '' if len(samples) == length else f' (got {len(samples)} of {length} samples!)'
                    txt = (f'{now} burst {n}: t_esp {t_us / 1e6:.3f} s, {length / rate * 1000:.1f} ms, '
                           f'ovf {ovf}{ok}\n  ' + analyse(v_s, rate, a.baud))
                    summ.write(txt + '\n'); summ.flush()
                    sys.stdout.write(txt.replace('\n', '\r\n')); sys.stdout.flush()
                    hdr = None
                else:                        # S = idle stats (min/max/avg mV at GPIO2, rate, ovf), # = info
                    sys.stdout.write(l + '\r\n'); sys.stdout.flush()
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)


if __name__ == '__main__':
    main()
