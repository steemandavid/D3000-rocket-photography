#!/usr/bin/env python3
"""Minimal serial terminal for the CRIG bring-up firmware.

Why not minicom/screen/pyserial: on the ESP32-S3 native USB port, opening the port
with a normal terminal toggles DTR/RTS separately, which the USB-Serial/JTAG logic
reads as "reset into download mode". This tool opens the tty raw and never touches
the control lines.

Usage:  ./term.py [/dev/serial/by-id/...]   (default: the only usb-Espressif_* device)
        type a command letter + Enter (see '?'), Ctrl+C to quit.
Reset to the app after flashing: esptool -p <by-id> --after watchdog-reset chip-id
"""
import glob, os, select, sys, termios

path = sys.argv[1] if len(sys.argv) > 1 else (glob.glob('/dev/serial/by-id/usb-Espressif_*') or [None])[0]
if not path:
    sys.exit("no Espressif USB device found in /dev/serial/by-id")
fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = a[1] = a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
termios.tcsetattr(fd, termios.TCSANOW, a)
print(f"connected to {path} (Ctrl+C to quit)", file=sys.stderr)
try:
    while True:
        r, _, _ = select.select([fd, sys.stdin], [], [], 0.2)
        if fd in r:
            try:
                sys.stdout.write(os.read(fd, 4096).decode(errors='replace')); sys.stdout.flush()
            except BlockingIOError:
                pass
        if sys.stdin in r:
            line = sys.stdin.readline()
            if not line:
                break
            if line.strip():
                os.write(fd, line.strip()[:1].encode())
except KeyboardInterrupt:
    pass
