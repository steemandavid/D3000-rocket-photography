#!/usr/bin/env python3
"""Reset the CRIG remote into the hardware test and print its console live for N seconds.

    python3 run_test.py [seconds] [port]
Default: 45 s, the board's USB-UART bridge port (by-id, board serial 5CCC062258).
Opens the port with DTR/RTS released, then pulses RTS (EN) to restart the app.
"""
import sys, time, serial

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 45
port = sys.argv[2] if len(sys.argv) > 2 else '/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CCC062258-if00'
p = serial.Serial()
p.port, p.baudrate, p.timeout, p.dtr, p.rts = port, 115200, 0.1, False, False
p.open()
p.rts = True; time.sleep(0.1); p.rts = False
end = time.time() + secs
show = False
line = b''
while time.time() < end:
    for ch in p.read(256):
        line += bytes([ch])
        if ch == 10:
            t = line.decode(errors='replace')
            if '=== CRIG' in t: show = True
            if show and 'gpio: GPIO' not in t:
                sys.stdout.write(t); sys.stdout.flush()
            line = b''
print(f'--- end of {secs:.0f} s capture ---')
