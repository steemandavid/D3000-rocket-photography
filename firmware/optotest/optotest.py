#!/usr/bin/env python3
"""Manual optocoupler test terminal for firmware/optotest.

h / f / p switch ON the HALF (IO5), FULL (IO4), PWR (IO6) optocoupler immediately (no Enter needed).
Enter switches ALL off. q quits (and switches everything off first).
Like term.py it never touches DTR/RTS (which would reset the ESP32-S3).
Usage: ./optotest.py [/dev/serial/by-id/...]   (default: the only usb-Espressif_* device)
"""
import glob, os, select, sys, termios, tty

path = sys.argv[1] if len(sys.argv) > 1 else (glob.glob('/dev/serial/by-id/usb-Espressif_*') or [None])[0]
if not path:
    sys.exit("no Espressif USB device found in /dev/serial/by-id")
fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = a[1] = a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
termios.tcsetattr(fd, termios.TCSANOW, a)
old = termios.tcgetattr(sys.stdin)
tty.setcbreak(sys.stdin.fileno())
print(f"connected to {path}\r\nh=HALF f=FULL p=PWR (ON)   Enter=all OFF   q=quit\r", file=sys.stderr)
try:
    while True:
        r, _, _ = select.select([fd, sys.stdin], [], [], 0.2)
        if fd in r:
            try:
                sys.stdout.write(os.read(fd, 4096).decode(errors='replace').replace('\n', '\r\n')); sys.stdout.flush()
            except BlockingIOError:
                pass
        if sys.stdin in r:
            ch = sys.stdin.read(1)
            if ch in ('h', 'f', 'p'):
                os.write(fd, ch.encode())
            elif ch in ('\n', '\r'):
                os.write(fd, b'\n')
            elif ch in ('q', '\x03'):
                break
finally:
    try: os.write(fd, b'\n')
    except OSError: pass
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
