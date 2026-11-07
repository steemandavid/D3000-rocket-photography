#!/usr/bin/env python3
"""Remote runtime test logger: logs the node's console (EVT lines, incl. `rbatt`) and asks for `status` every
PERIOD s; every line gets a host timestamp. The remote runs on its cell without USB; its battery voltage arrives
at the node in HEARTBEAT (FR-N20). Uses the node's native USB port (opened raw, DTR/RTS never touched) and
reopens it if it goes away.

usage: runtime_log.py <logfile> [period_s=30]
run detached:  setsid nohup python3 tools/runtime_log.py captures/<date>-remote-runtime.log >/dev/null 2>&1 &
"""
import os, select, sys, termios, time

PORT = '/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_68:EE:8F:47:8E:C8-if00'   # node
log = open(sys.argv[1], 'a', buffering=1)
period = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0


def emit(line):
    log.write(f'{time.strftime("%Y-%m-%d %H:%M:%S")} {time.time():.1f} {line}\n')


def rawopen():
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    termios.tcsetattr(fd, termios.TCSANOW, a)
    os.write(fd, b'\n')
    return fd


emit(f'# runtime_log start, port {PORT}, status every {period:.0f} s')
fd, buf, t_next = None, b'', 0.0
while True:
    try:
        if fd is None:
            fd = rawopen(); buf = b''; emit('# port open')
        if time.time() >= t_next:
            os.write(fd, b'status\n'); t_next = time.time() + period
        r, _, _ = select.select([fd], [], [], 0.5)
        if r:
            d = os.read(fd, 4096)
            if not d:
                raise OSError('eof')
            buf += d
            while b'\n' in buf:
                l, buf = buf.split(b'\n', 1)
                l = l.decode(errors='replace').strip()
                if l:
                    emit(l)
    except OSError as e:
        if fd is not None:
            emit(f'# port lost: {e}')
            try: os.close(fd)
            except OSError: pass
        fd = None
        time.sleep(2)
