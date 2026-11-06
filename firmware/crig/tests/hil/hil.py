#!/usr/bin/env python3
"""CRIG hardware-in-the-loop tests: node + remote on USB, driven through their consoles.

    python3 tests/hil/hil.py [test-id ...]        (default: all)

The remote's console test hooks (sim shoot / trig / arm, sim badcrc) replace the button, the TRIG
optocoupler and the RLC ARM pattern. Board resets use the USB-UART bridge's RTS line (EN).
Timing is taken from each board's own "EVT <ms>" timestamps where both events come from the same
board, and from host receive times across boards.
NOTE: if the camera is connected to the node, the burst tests take real pictures.
"""
import os, re, sys, time, threading, termios, select, serial

BYID = '/dev/serial/by-id/'
PORTS = {'node': BYID + 'usb-1a86_USB_Single_Serial_5CCC062602-if00',
         'remote': BYID + 'usb-1a86_USB_Single_Serial_5CCC062258-if00'}
NATIVE = {'node': BYID + 'usb-Espressif_USB_JTAG_serial_debug_unit_68:EE:8F:47:8E:C8-if00',
          'remote': BYID + 'usb-Espressif_USB_JTAG_serial_debug_unit_68:EE:8F:47:96:D4-if00'}
RLC_BASE = BYID + 'usb-1a86_USB_Single_Serial_5B5E042156-if00'     # RLC base UART bridge (RLC tests R01-R03)
EVT = re.compile(r'^EVT (\d+) (.*)$')


class Dev:
    def __init__(self, name, port):
        self.name = name
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout, self.s.dtr, self.s.rts = port, 115200, 0.02, False, False
        self.s.open()
        self.lines = []          # (host_time, text)
        self.lock = threading.Lock()
        self.run = True
        threading.Thread(target=self._rd, daemon=True).start()

    def _rd(self):
        buf = b''
        while self.run:
            buf += self.s.read(512)
            while b'\n' in buf:
                l, buf = buf.split(b'\n', 1)
                with self.lock:
                    self.lines.append((time.time(), l.decode(errors='replace').strip()))

    def mark(self):
        with self.lock:
            return len(self.lines)

    def send(self, line):
        self.s.write((line + '\n').encode())

    def reset(self, hold=0.1):
        self.s.rts = True
        time.sleep(hold)
        self.s.rts = False

    def hold(self, on):
        self.s.rts = on

    def find(self, pat, since=0):
        r = re.compile(pat)
        with self.lock:
            out = []
            for t, l in self.lines[since:]:
                m = EVT.match(l)
                body = m.group(2) if m else l
                mm = r.search(body)
                if mm:
                    out.append((t, int(m.group(1)) if m else None, body, mm))
            return out

    def wait(self, pat, timeout, since=0):
        end = time.time() + timeout
        while time.time() < end:
            f = self.find(pat, since)
            if f:
                return f[0]
            time.sleep(0.01)
        return None

    def status(self):
        m = self.mark()
        self.send('status')
        r = self.wait(r'^STATUS ', 1.0, m)
        if not r:
            return {}
        return dict(kv.split('=', 1) for kv in r[2].split()[1:] if '=' in kv)


class NativeDev(Dev):
    """Board on its native USB-Serial/JTAG port. The tty is opened raw and the control lines are never
    touched (they would put the chip in download mode); reset = console 'reboot', and the reader
    reopens the port after the USB re-enumeration."""
    def __init__(self, name, port):
        self.name, self.port, self.fd = name, port, None
        self.lines, self.lock, self.run = [], threading.Lock(), True
        self._open()
        threading.Thread(target=self._rd, daemon=True).start()

    def _open(self):
        end = time.time() + 8
        while time.time() < end:
            try:
                fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                a = termios.tcgetattr(fd)
                a[0] = a[1] = a[3] = 0
                a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
                termios.tcsetattr(fd, termios.TCSANOW, a)
                self.fd = fd
                os.write(fd, b'\n')          # flush anything the tty echoed before raw mode
                return
            except OSError:
                time.sleep(0.05)
        raise Fail(f'{self.name}: cannot open {self.port}')

    def _rd(self):
        buf = b''
        while self.run:
            try:
                r, _, _ = select.select([self.fd], [], [], 0.05)
                if r:
                    d = os.read(self.fd, 4096)
                    if not d:
                        raise OSError('eof')
                    buf += d
            except (OSError, ValueError):
                try: os.close(self.fd)
                except OSError: pass
                time.sleep(0.1); self._open(); buf = b''
                continue
            while b'\n' in buf:
                l, buf = buf.split(b'\n', 1)
                with self.lock:
                    self.lines.append((time.time(), l.decode(errors='replace').strip()))

    def send(self, line):
        try: os.write(self.fd, (line + '\n').encode())
        except OSError: pass

    def reset(self, hold=0.1):
        self.send('reboot')

    def hold(self, on):
        if on: raise Fail(f'{self.name}: cannot hold a native-USB board in reset')


def open_dev(name):
    if os.path.exists(PORTS[name]):
        return Dev(name, PORTS[name])
    return NativeDev(name, NATIVE[name])


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


def need(r, msg):
    check(r is not None, msg)
    return r


node = remote = None
results = []


def boot_both():
    m_n, m_r = node.mark(), remote.mark()
    node.reset(); remote.reset()
    need(remote.wait(r'^link up', 6, m_r), 'remote link up after reset')
    need(node.wait(r'^link up', 6, m_n), 'node link up after reset')
    remote.send('sim arm none')
    time.sleep(2.5)


def reset_remote():
    m = remote.mark()
    remote.reset()
    need(remote.wait(r'^link up', 6, m), 'remote link up after remote reset')
    remote.send('sim arm none')          # the real ARM line may be connected: start every test from NONE
    time.sleep(2.5)


def idle():
    """Neutral start: SHOOT/TRIG off; if the RLC status is not NONE (an earlier ARM test), reset the remote."""
    for c in ('sim shoot 0', 'sim trig 0', 'sim arm none'):
        remote.send(c)
        time.sleep(0.05)
    node.hold(False)
    time.sleep(0.5)
    if remote.status().get('arm') != 'NONE':
        reset_remote()
        remote.send('sim trig 0'); remote.send('sim shoot 0'); time.sleep(0.1)


# ---------------------------------------------------------------- tests
def H01():
    """Boot, link up, link quality, readiness (FR-R07, FR-R13, FR-N09)"""
    boot_both()
    s = remote.status()
    check(int(s.get('q', 0)) >= 95, f"link quality {s.get('q')} >= 95 %")
    check(s.get('ready') == '1', f"ready=1 (got {s.get('ready')})")
    check(s.get('node') == 'IDLE' and s.get('sh') == 'REST', 'node IDLE / REST')
    n = node.status()
    check(n.get('state') == 'IDLE' and n.get('link') == '1', 'node IDLE, link=1')
    return f"q={s['q']}% rssi={s.get('nrssi')} dBm"


def H02():
    """Button burst: HALF -> FULL after t_prefire, release -> REST within t_shoot_timeout (FR-N04, FR-N05, T-04)"""
    idle()
    mr, mn = remote.mark(), node.mark()
    remote.send('sim shoot 1')
    r0 = need(remote.wait(r'^shoot start src=BUTTON', 1, mr), 'remote shoot start')
    h = need(node.wait(r'^shutter HALF', 1, mn), 'node HALF')
    f = need(node.wait(r'^shutter FULL', 1, mn), 'node FULL')
    lat = h[0] - r0[0]
    pre = f[1] - h[1]
    check(lat < 0.15, f'remote start -> node HALF {lat*1000:.0f} ms < 150 ms')
    check(95 <= pre <= 130, f'HALF -> FULL {pre} ms (t_prefire 100)')
    time.sleep(1.0)
    ms = remote.mark()
    remote.send('sim shoot 0')
    s = need(remote.wait(r'^shoot stop', 1, ms), 'remote shoot stop')
    r = need(node.wait(r'^shutter REST', 1, mn), 'node REST')
    rel = r[0] - s[0]
    check(rel <= 0.45, f'release -> REST {rel*1000:.0f} ms <= 450 ms (t_shoot_timeout 300)')
    e = need(node.wait(r'^burst end ms=(\d+) reason=release', 1, mn), 'burst end')
    b = int(e[3].group(1))
    check(900 <= b <= 1350, f'burst {b} ms ~ 1 s')
    return f'latency {lat*1000:.0f} ms, prefire {pre} ms, release {rel*1000:.0f} ms, burst {b} ms'


def H03():
    """SHOOT held through link recovery is ignored until released once (FR-R03, T-07)"""
    idle()
    remote.send('sim shoot 1')
    mn = node.mark()
    need(node.wait(r'^shutter FULL', 1, mn), 'burst running')
    mr = remote.mark()
    node.hold(True)
    need(remote.wait(r'^link down', 3, mr), 'remote link down while node held in reset')
    mn = node.mark()
    node.hold(False)
    need(node.wait(r'^boot shutter=REST', 4, mn), 'node boots in REST')
    need(remote.wait(r'^link up', 4, mr), 'link recovered')
    time.sleep(2.0)
    check(not node.find(r'^shutter (HALF|FULL)', mn), 'no burst after link recovery while SHOOT still held')
    s = remote.status()
    check(s.get('act') == '0' and s.get('nr') == '1', f"remote act=0 nr=1 (got act={s.get('act')} nr={s.get('nr')})")
    remote.send('sim shoot 0'); time.sleep(0.4)
    mn = node.mark()
    remote.send('sim shoot 1')
    need(node.wait(r'^shutter HALF', 0.4, mn), 'new press after release shoots')
    remote.send('sim shoot 0')
    need(node.wait(r'^shutter REST', 1, mn), 'REST after release')
    return 'held SHOOT ignored after recovery; re-press works'


def H04():
    """Remote reset during a burst -> node REST within t_shoot_timeout (T-05)"""
    idle()
    mn = node.mark()
    remote.send('sim shoot 1')
    need(node.wait(r'^shutter FULL', 1, mn), 'burst running')
    time.sleep(0.5)
    t = time.time()
    remote.reset()
    r = need(node.wait(r'^shutter REST', 1, mn), 'node REST after remote reset')
    d = r[0] - t
    check(d <= 0.45, f'remote reset -> REST {d*1000:.0f} ms <= 450 ms')
    mr = remote.mark()
    need(remote.wait(r'^link up', 6, mr), 'remote back')
    time.sleep(2.5)
    return f'REST {d*1000:.0f} ms after remote reset'


def H05():
    """Max burst -> LOCKOUT, release required, LOCKOUT cleared (FR-N06, FR-R08, T-06)"""
    idle()
    mn, mr = node.mark(), remote.mark()
    remote.send('sim shoot 1')
    f = need(node.wait(r'^shutter FULL', 1, mn), 'burst running')
    e = need(node.wait(r'^burst end ms=(\d+) reason=max_burst', 17, mn), 'max burst end')
    b = int(e[3].group(1))
    check(15000 <= b <= 15100, f'max burst {b} ms (15000 + <=100)')
    need(node.wait(r'^state SHOOTING -> LOCKOUT', 1, mn), 'node LOCKOUT')
    need(remote.wait(r'^lockout', 1, mr), 'remote shows LOCKOUT')
    check(not remote.find(r'^batt ', mr), 'no battery state change during a 15 s burst with the motor running')
    c = need(node.wait(r'^lockout cleared', 1, mn), 'LOCKOUT cleared by heartbeat')
    check(c[0] - e[0] < 0.6, f'LOCKOUT -> cleared {1000*(c[0]-e[0]):.0f} ms')
    m2 = node.mark()
    time.sleep(1.5)
    check(not node.find(r'^shutter (HALF|FULL)', m2), 'no new burst while SHOOT still held')
    s = remote.status()
    check(s.get('nr') == '1' and s.get('act') == '0', 'remote needs release')
    remote.send('sim shoot 0'); time.sleep(0.3)
    m3 = node.mark()
    remote.send('sim shoot 1')
    need(node.wait(r'^shutter HALF', 0.4, m3), 're-press shoots')
    remote.send('sim shoot 0')
    need(node.wait(r'^shutter REST', 1, m3), 'REST')
    return f'LOCKOUT at {b} ms, cleared after {1000*(c[0]-e[0]):.0f} ms'


def H06():
    """Node reset during a burst: boots IDLE / REST (FR-N02, T-09)"""
    idle()
    mn = node.mark()
    remote.send('sim shoot 1')
    need(node.wait(r'^shutter FULL', 1, mn), 'burst running')
    m = node.mark()
    node.reset()
    b = need(node.wait(r'^boot shutter=REST', 4, m), 'node boot line')
    i = need(node.wait(r'^state BOOT -> IDLE', 4, m), 'BOOT -> IDLE')
    first = node.find(r'^shutter (HALF|FULL)', m)
    check(not first or first[0][0] >= i[0], 'no shutter action before IDLE')
    remote.send('sim shoot 0')
    time.sleep(1.0)
    n = node.status()
    check(n.get('shutter') == 'REST', 'REST after release')
    return 'boots IDLE/REST'


def H07():
    """TRIG accepted only while ARM = armed; burst stops when the ARM pattern stops (§8.6, T-18)"""
    idle(); reset_remote()
    mr, mn = remote.mark(), node.mark()
    remote.send('sim trig 1')
    time.sleep(1.5)
    check(not remote.find(r'^shoot start', mr), 'TRIG ignored without ARM pattern')
    remote.send('sim arm 1')
    need(remote.wait(r'^rlc CONNECTED', 2.6, mr), 'ARM 1 Hz -> CONNECTED')
    time.sleep(1.5)
    check(not remote.find(r'^shoot start', mr), 'TRIG ignored while only CONNECTED')
    m2 = remote.mark()
    remote.send('sim arm 10')
    a = need(remote.wait(r'^rlc ARMED', 0.7, m2), 'ARM 10 Hz -> ARMED')
    s = need(remote.wait(r'^shoot start src=TRIG', 0.2, m2), 'TRIG burst starts when armed')
    need(node.wait(r'^burst start src=TRIG', 0.3, mn), 'node burst src=TRIG')
    need(node.wait(r'^shutter FULL', 0.5, mn), 'node FULL')
    time.sleep(0.5)
    m3, mn3 = remote.mark(), node.mark()
    t = time.time()
    remote.send('sim arm none')
    st = need(remote.wait(r'^shoot stop', 0.8, m3), 'TRIG ignored once ARM pattern stops')
    r = need(node.wait(r'^shutter REST', 1.2, mn3), 'node REST')
    check(st[0] - t <= 0.45, f'pattern stop -> shoot stop {1000*(st[0]-t):.0f} ms <= 450')
    check(r[0] - t <= 0.9, f'pattern stop -> node REST {1000*(r[0]-t):.0f} ms <= 900')
    time.sleep(1.0)
    check(not node.find(r'^prepare pulse', mn3), 'no wake pulse straight after the burst')
    remote.send('sim trig 0')
    return f'armed->shoot {1000*(s[0]-a[0]):.0f} ms, stop {1000*(st[0]-t):.0f} ms, REST {1000*(r[0]-t):.0f} ms'


def H08():
    """ARM decoding and RLC status / readiness (§8.3, FR-R12, FR-R13, T-17)"""
    idle(); reset_remote()
    mr = remote.mark()
    t = time.time(); remote.send('sim arm 1')
    c = need(remote.wait(r'^rlc CONNECTED', 2.6, mr), 'CONNECTED')
    t1 = c[0] - t
    t = time.time(); remote.send('sim arm 10')
    a = need(remote.wait(r'^rlc ARMED', 0.6, mr), 'ARMED')
    t2 = a[0] - t
    t = time.time(); remote.send('sim arm 1')
    time.sleep(0.8)
    c2 = [x for x in remote.find(r'^rlc CONNECTED', mr) if x[0] > a[0]]
    check(c2, 'back to CONNECTED after ARMED')
    t3 = c2[0][0] - t
    check(t3 <= 0.6, f'ARMED -> CONNECTED {1000*t3:.0f} ms <= 600')
    t = time.time(); remote.send('sim arm none')
    l = need(remote.wait(r'^rlc LOST', 3.5, mr), 'LOST')
    t4 = l[0] - t
    # LOST comes t_arm_lost (2.5 s) after the LAST edge, which was 0..1 s before the stop command (1 Hz)
    check(1.4 <= t4 <= 2.7, f'pattern stop -> LOST {t4:.2f} s (t_arm_lost 2.5 s after the last edge)')
    time.sleep(0.3)
    s = remote.status()
    check(s.get('ready') == '0', 'not ready while RLC LOST')
    m = remote.mark(); remote.send('sim arm 1')
    need(remote.wait(r'^ready 1', 3, m), 'ready again when RLC CONNECTED')
    return f'CONNECTED {t1:.2f} s, ARMED {1000*t2:.0f} ms, disarm {1000*t3:.0f} ms, LOST {t4:.2f} s'


def H09():
    """PREPARE: RLC armed -> node HALF wake pulse, repeated every 4 s, stops when disarmed (FR-R15, FR-N19, T-19)"""
    idle(); reset_remote()
    remote.send('sim arm 1'); time.sleep(2.5)
    mn = node.mark()
    t = time.time(); remote.send('sim arm 10')
    p = need(node.wait(r'^prepare pulse', 1.0, mn), 'first prepare pulse')
    check(p[0] - t <= 0.8, f'armed -> wake pulse {1000*(p[0]-t):.0f} ms')
    h = need(node.wait(r'^shutter HALF', 0.2, mn), 'HALF')
    r = need(node.wait(r'^shutter REST', 0.6, mn), 'REST after wake pulse')
    w = r[1] - h[1]
    check(280 <= w <= 330, f'wake pulse {w} ms (300)')
    pp = need(node.wait(r'^prepare pulse', 5.0, node.mark()), 'second prepare pulse')
    k = pp[1] - p[1]
    check(3950 <= k <= 4100, f'keep-alive interval {k} ms (4000)')
    check(not node.find(r'^shutter FULL', mn), 'prepare never fires FULL')
    m2 = node.mark()
    remote.send('sim arm 1')
    need(node.wait(r'^prepare 0', 1.0, m2), 'prepare 0 after disarm')
    time.sleep(0.6)
    m3 = node.mark()
    time.sleep(4.5)
    check(not node.find(r'^prepare pulse', m3), 'no wake pulses after disarm')
    return f'first pulse {1000*(p[0]-t):.0f} ms, width {w} ms, interval {k} ms'


def H10():
    """Node console test cycle (FR-N07)"""
    idle()
    mn = node.mark()
    node.send('test')
    h = need(node.wait(r'^shutter HALF', 1, mn), 'HALF')
    f = need(node.wait(r'^shutter FULL', 1, mn), 'FULL')
    r = need(node.wait(r'^shutter REST', 1, mn), 'REST')
    need(node.wait(r'^test done', 1, mn), 'test done')
    check(95 <= f[1] - h[1] <= 130 and 145 <= r[1] - f[1] <= 180, f'HALF {f[1]-h[1]} ms, FULL {r[1]-f[1]} ms')
    return f'HALF {f[1]-h[1]} ms, FULL {r[1]-f[1]} ms'


def H11():
    """Corrupt frame dropped silently: no action, no reply (FR-N11, T-10)"""
    idle()
    b0 = int(node.status().get('bad', 0))
    mr, mn = remote.mark(), node.mark()
    remote.send('sim badcrc')
    a = need(remote.wait(r'^badcrc acked=(\d)', 1.5, mr), 'badcrc result')
    check(a[3].group(1) == '0', 'corrupt frame not answered')
    check(not node.find(r'^shutter', mn), 'corrupt SHOOT ignored')
    b1 = int(node.status().get('bad', 0))
    check(b1 == b0 + 1, f'node bad counter {b0} -> {b1}')
    return 'dropped, no STATUS, counter +1'


def H12():
    """Link loss indication on the remote: ready red after t_status_stale, NO LINK after t_link_lost (FR-R13, FR-N08)"""
    idle()
    mr = remote.mark()
    t = time.time()
    node.hold(True)
    try:
        rd = need(remote.wait(r'^ready 0', 2.0, mr), 'ready 0')
        ld = need(remote.wait(r'^link down', 3.0, mr), 'link down')
    finally:
        node.hold(False)
    need(remote.wait(r'^link up', 5.0, mr), 'link up again')
    need(remote.wait(r'^ready 1', 5.0, mr), 'ready again')
    check(rd[0] - t <= 1.3, f'ready 0 after {1000*(rd[0]-t):.0f} ms (stale 1 s)')
    check(1.7 <= ld[0] - t <= 2.4, f'link down after {ld[0]-t:.2f} s (2 s after the last packet, <= 0.2 s before the hold)')
    time.sleep(2.0)
    return f'ready 0 at {1000*(rd[0]-t):.0f} ms, link down at {ld[0]-t:.2f} s'


def H13():
    """Packet rates: HEARTBEAT 5 Hz idle, SHOOT 20 Hz while shooting (FR-R01, FR-R02)"""
    idle()
    a = int(node.status().get('rx', 0)); time.sleep(5.0); b = int(node.status().get('rx', 0))
    hb = (b - a) / 5.0
    remote.send('sim shoot 1'); time.sleep(0.5)
    a = int(node.status().get('rx', 0)); time.sleep(5.0); b = int(node.status().get('rx', 0))
    sh = (b - a) / 5.0
    remote.send('sim shoot 0'); time.sleep(0.6)
    check(4.4 <= hb <= 5.6, f'heartbeat {hb:.1f} Hz (5)')
    check(18.0 <= sh <= 21.0, f'shoot {sh:.1f} Hz (20)')
    s = remote.status()
    check(int(s.get('q', 0)) >= 95, f"quality {s.get('q')} %")
    return f'heartbeat {hb:.1f} Hz, shoot {sh:.1f} Hz'


def H14():
    """STATUS contents reach the remote: node pack voltage, fault flags, RSSI (FR-R06, FR-N10, FR-N17)"""
    idle()
    n, s = node.status(), remote.status()
    check(abs(int(n['vbat']) - int(s['nv'])) <= 150, f"node vbat {n['vbat']} vs remote view {s['nv']}")
    check(n['fault'] == s['nf'], f"fault flags {n['fault']} vs {s['nf']}")
    check(int(s['nf'], 16) & 0x10, 'camera-on bit set (power control disabled)')
    check(-95 <= int(s['nrssi']) <= -1, f"rssi {s['nrssi']} dBm")
    return f"vbat {n['vbat']} mV, fault {n['fault']}, rssi {s['nrssi']} dBm"


def H15():
    """Node pack warning and cutoff: FAULT, REST, SHOOT refused, recovery with hysteresis (FR-N10, T-11)"""
    idle()
    mr, mn = remote.mark(), node.mark()
    try:
        node.send('sim vbat 6900')
        time.sleep(0.8)
        s = remote.status()
        check(int(s['nf'], 16) & 0x01 and not int(s['nf'], 16) & 0x02, f"6.9 V: warning flag only (nf={s['nf']})")
        check(s.get('ready') == '1', 'still ready with a low-pack warning')
        node.send('sim vbat 6400')
        need(node.wait(r'^state IDLE -> FAULT', 1.0, mn), 'node FAULT below 6.6 V')
        need(remote.wait(r'^node state FAULT', 1.0, mr), 'remote sees FAULT')
        need(remote.wait(r'^ready 0', 1.0, mr), 'not ready on node FAULT')
        m2 = node.mark()
        remote.send('sim shoot 1'); time.sleep(1.0); remote.send('sim shoot 0')
        check(not node.find(r'^shutter (HALF|FULL)', m2), 'SHOOT refused in FAULT')
        time.sleep(0.5)
        node.send('sim vbat 6700'); time.sleep(0.8)
        check(node.status().get('state') == 'FAULT', 'still FAULT at 6.7 V (hysteresis 200 mV)')
        node.send('sim vbat 7400')
        need(node.wait(r'^fault cleared', 1.0, mn), 'FAULT cleared above 6.8 V')
        need(remote.wait(r'^ready 1', 1.5, mr), 'ready again')
        time.sleep(0.5)
        check(not int(remote.status()['nf'], 16) & 0x03, 'warning cleared at 7.4 V')
    finally:
        node.send('sim vbat real'); time.sleep(0.5)
    return 'warn 6.9 V, FAULT 6.4 V, hold 6.7 V, clear 7.4 V'


def H16():
    """Remote cell warning and cutoff: SHOOT blocked, cleared again (FR-R11, T-11)"""
    idle()
    mr = remote.mark()
    try:
        remote.send('sim batt 3400')
        need(remote.wait(r'^batt present', 3.5, mr), 'cell present (after 2 s)')
        need(remote.wait(r'^batt warn 1', 1.0, mr), 'warning below 3.5 V')
        check(remote.status().get('ready') == '1', 'ready with a low-cell warning')
        t = time.time()
        remote.send('sim batt 3000')
        c = need(remote.wait(r'^batt cutoff 1', 4.5, mr), 'cutoff below 3.2 V')
        check(2.8 <= c[0] - t <= 4.0, f'cutoff after {c[0]-t:.1f} s (3 s sustained)')
        need(remote.wait(r'^ready 0', 0.5, mr), 'not ready at cutoff')
        mn = node.mark(); m2 = remote.mark()
        remote.send('sim shoot 1'); time.sleep(0.8); remote.send('sim shoot 0')
        check(not remote.find(r'^shoot start', m2) and not node.find(r'^shutter', mn), 'SHOOT blocked at cutoff')
        remote.send('sim batt 3900')
        need(remote.wait(r'^batt cutoff 0', 1.5, mr), 'cutoff cleared')
        need(remote.wait(r'^ready 1', 1.5, mr), 'ready again')
        check(not remote.find(r'shutting down', mr), 'no shutdown (restored within 10 s)')
    finally:
        remote.send('sim batt real')
    need(remote.wait(r'^batt none', 3.5, mr), 'back to no cell (USB)')
    return f'cutoff after {c[0]-t:.1f} s, cleared'


def H17():
    """OLED main page content (FR-R06, §9.2)"""
    idle()
    def screen():
        m = remote.mark(); remote.send('screen'); time.sleep(0.4)
        return {int(x[3].group(1)): x[3].group(2) for x in remote.find(r'^SCREEN (\d) \|(.*)\|$', m)}
    time.sleep(0.5)
    sc = screen()
    check(re.match(r'LINK +(9\d|100)% RSSI -\d+', sc.get(0, '')), f"line 0 '{sc.get(0)}'")
    check(sc.get(1, '').startswith('NODE IDLE REST'), f"line 1 '{sc.get(1)}'")
    check(sc.get(3) == 'RLC NONE', f"line 3 '{sc.get(3)}'")
    check(sc.get(7) == 'READY', f"line 7 '{sc.get(7)}'")
    remote.send('sim shoot 1'); time.sleep(1.2)
    sc2 = screen()
    remote.send('sim shoot 0'); time.sleep(0.6)
    check(sc2.get(1, '').startswith('NODE SHOOTING FULL'), f"shooting line 1 '{sc2.get(1)}'")
    check(sc2.get(7) == '>> SHOOTING <<', f"shooting line 7 '{sc2.get(7)}'")
    check(re.match(r'BUTTON \d+\.\dS', sc2.get(4, '')), f"burst timer line 4 '{sc2.get(4)}'")
    return ' / '.join(sc.get(i, '') for i in range(8) if sc.get(i))


# ------------------------------------------------ tests with the real RLC base on the DIN cable
def arm_edges(samples):
    """Edge times (host clock) from status samples: (t_host, aedges, aage_ms)."""
    out, last = [], None
    for t, n, age in samples:
        if n != last and age >= 0:
            out.append(t - age / 1000.0); last = n
    return out


def sample_remote(secs, every=0.1):
    smp, end = [], time.time() + secs
    while time.time() < end:
        st = remote.status()
        if st:
            smp.append((time.time(), int(st['aedges']), int(st['aage']), st))
        time.sleep(every)
    return smp


def R01():
    """Real RLC base idle: ARM 1 Hz through U6 -> CONNECTED; TRIG line idle; nothing shoots (T-21 idle part, T-17)"""
    idle()
    mr, mn = remote.mark(), node.mark()
    t0 = time.time()
    remote.send('sim trig real'); remote.send('sim arm real')
    c = need(remote.wait(r'^rlc CONNECTED', 2.8, mr), 'CONNECTED from the real ARM line')
    smp = sample_remote(6.0, 0.2)
    per = sorted({int(x[3]['aper']) for x in smp})
    check(all(970 <= p <= 1030 for p in per), f'ARM periods {per} ms (1000)')
    n = smp[-1][1] - smp[0][1]
    check(5 <= n <= 7, f'{n} edges in 6 s')
    check(all(x[3]['trig'] == '0' for x in smp), 'TRIG line idle (opto off)')
    check(all(x[3]['arm'] == 'CONNECTED' for x in smp), 'stays CONNECTED')
    check(all(x[3]['ready'] == '1' for x in smp), 'ready')
    check(not remote.find(r'^shoot start', mr) and not node.find(r'^shutter (HALF|FULL)', mn), 'nothing shoots')
    return f'CONNECTED {c[0]-t0:.2f} s after switching to the real line, periods {per} ms'


def R02():
    """RLC base reset: no ARM/TRIG pulses while it boots, 1 Hz back within 2 s, CRIG never armed (T-26 / RLC T-C06)"""
    idle()
    m = remote.mark()
    remote.send('sim trig real'); remote.send('sim arm real')
    need(remote.wait(r'^rlc CONNECTED', 2.8, m), 'CONNECTED before the base reset')
    base = Dev('rlcbase', RLC_BASE)          # opening the bridge may itself reset the base: let it settle
    time.sleep(4.0)
    mb, mr, mn = base.mark(), remote.mark(), node.mark()
    t0 = time.time()
    base.reset()
    smp = sample_remote(6.0, 0.05)
    up = base.wait(r'rlc_cam: camera rig interface up', 0.1, mb)
    base.run = False; base.s.close()
    check(up is not None, 'base boot log shows the camera rig interface')
    edges = arm_edges([(t, n, a) for t, n, a, _ in smp])
    boot = [e for e in edges if t0 + 0.15 < e < up[0]]
    check(not boot, f'no ARM pulses during base boot ({[round(e - t0, 2) for e in boot]})')
    after = [e for e in edges if e > t0 + 0.15]
    need(after or None, 'ARM pulses resume')
    first = after[0] - t0
    check(first <= 2.0, f'first ARM pulse {first:.2f} s after reset (<= 2 s)')
    check(all(x[3]['trig'] == '0' for x in smp), 'TRIG line stayed low')
    arms = {x[3]['arm'] for x in smp}
    check('ARMED' not in arms, f'never ARMED ({arms})')
    check(not remote.find(r'^shoot start', mr) and not node.find(r'^shutter (HALF|FULL)', mn), 'nothing shoots')
    gaps = [round(b - a, 3) for a, b in zip(after, after[1:])]
    return f'interface up {up[0]-t0:.2f} s, first pulse {first:.2f} s, then periods {gaps} s, RLC state seen {sorted(arms)}'


TESTS = [H01, H02, H03, H04, H05, H06, H07, H08, H09, H10, H11, H12, H13, H14, H15, H16, H17]
RLC_TESTS = [R01, R02]


def main():
    global node, remote
    sel = sys.argv[1:]
    node, remote = open_dev('node'), open_dev('remote')
    time.sleep(0.2)
    sel = [a for a in sel if not a.startswith('-')]
    todo = [t for t in TESTS + RLC_TESTS if (not sel and t in TESTS) or t.__name__ in sel or ('rlc' in sel and t in RLC_TESTS)]
    if todo[0] is not H01:
        boot_both()
    for t in todo:
        t0 = time.time()
        m0 = (node.mark(), remote.mark())
        try:
            info = t()
            results.append((t.__name__, 'PASS', info or ''))
        except Fail as e:
            results.append((t.__name__, 'FAIL', str(e)))
            if '-v' in sys.argv or True:
                for d, m in ((node, m0[0]), (remote, m0[1])):
                    with d.lock:
                        ev = [l for _, l in d.lines[m:] if l.startswith(('EVT', 'STATUS', 'ERR', 'W ', 'E '))]
                    print(f'  --- {d.name} log ({len(ev)} lines, last 25):')
                    for l in ev[-25:]:
                        print('   ', l[:150])
            node.hold(False)
            remote.send('sim shoot 0'); remote.send('sim trig 0'); remote.send('sim arm none'); remote.send('sim batt real'); node.send('sim vbat real')
            time.sleep(1.0)
        print(f'{t.__name__} {results[-1][1]} ({time.time()-t0:.1f} s) {results[-1][2]}  — {t.__doc__}', flush=True)
    remote.send('sim shoot real'); remote.send('sim trig real'); remote.send('sim arm real'); remote.send('sim batt real'); node.send('sim vbat real')
    n = sum(1 for r in results if r[1] == 'PASS')
    print(f'\n{n}/{len(results)} passed')
    sys.exit(0 if n == len(results) else 1)


if __name__ == '__main__':
    main()
