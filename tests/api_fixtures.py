#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Shared fixtures for direct-API tests: Python drives the firmware over HTTP.

ApiTestCase boots fm1-live (the native emulator, live mode) with a firmware
built with the REST API (`modd_emu`: CONFIG_RPC, CONFIG_RPC_INPUT,
CONFIG_RPC_MEM, CONFIG_RPC_AUDIO_TAP, CONFIG_DEBUG_FRAMEBUFFER) and forwards a
host port to the firmware's RPC port. From then on every step is a plain HTTP
GET to the firmware itself: no browser, no Playwright, no panel emulation.

    pages      /api/pages, /api/page, /modd/<page>[/<sub>]
    input      /api/input/button|enc|key|pot/...   (ORed into the panel scan)
    audio      /api/audio (RMS, peak), /api/audio/capture (last 8192 samples)
    tracks     /api/track/<n|sel>[/engine/<name>]
    memory     /api/mem/<addr>/<len>, /api/mem/find/<hex>
    LCD        /api/screenshot (240x240 RGB565)
    log        /api/log

Set RPC_HOST=host:port to run against an already running target (an fm1-live
started by hand, or a device on the network) instead of booting one. Waits are
in firmware time (the `ms` that /api/input reports), so a throttled emulator
slows the test down instead of breaking it.

Connection budget: every request is one TCP connection (the firmware answers
with `Connection: close`). FirmwareApi spaces its requests only when the env
variable FM1_API_CONN_BUDGET is set (opt-in): fm1-live builds older than fm1-emu
commit fce5276 keep at most 32 NAT entries and hold each closed one for 30 s, so
a burst of more than ~30 requests locks the forward out; 24 suits such a build.
Since fce5276 the emulator frees finished NAT entries and there is no budget.
"""
import array
import base64
import collections
import json
import math
import os
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parent))
import io_log  # noqa: E402  (tests/io_log.py: the session's IO log)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
import fm1_paths  # noqa: E402
from fm1_paths import EMU_ROOT, FM1_LIVE  # noqa: E402,F401
RESULTS_DIR = io_log.RESULTS
RPC_PORT = 9222                     # CONFIG_RPC_PORT of the API defconfigs
DEFAULT_DEFCONFIG = 'modd_emu'

_run_timestamp = io_log.RUN_STAMP          # one per session: every results folder and debug.log share it

def api_fwsc(defconfig=None):
    """The firmware package for the API tests: fm1_paths.fwsc_path (build/<defconfig>/ by default, build/<defconfig>-open/ with USE_OPEN_TOOLCHAIN=1), or MODD_API_FWSC
    for the defconfig the task built (MODD_API_DEFCONFIG, default modd_emu). A test class
    that names another defconfig (e.g. modd) always gets its own build."""
    main = os.environ.get('MODD_API_DEFCONFIG', DEFAULT_DEFCONFIG)
    dc = defconfig or main
    if dc == main and 'MODD_API_FWSC' in os.environ:
        return Path(os.environ['MODD_API_FWSC'])
    return fm1_paths.fwsc_path(dc)


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


from signal_checks import (NOTE_NAMES, midi_note, note_hz, rms, tone_power,  # noqa: E402,F401
                           write_wav, rgb565_to_rgb)



# ------------------------------------------------------------------ the client

class ApiError(RuntimeError):
    pass


class LateCapture(ApiError):
    """A capture taken later than planned (the emulator stalled in between)."""


class FirmwareApi:
    """HTTP client for the firmware's REST API.

    budget: at most this many connections per CONN_WINDOW_S (None = no limit)."""

    CONN_WINDOW_S = 31.0
    # fm1-emu before fce5276 (feat/usb-link) kept each finished forwarded connection's NAT
    # entry for 30 s and refused new ones at 32: a port forward took 32 requests per 30 s.
    # Since then finished entries make room, so there is no budget unless FM1_API_CONN_BUDGET
    # sets one (24 suits an older fm1-live: its 32 slots less the firmware's own fetches).
    CONN_BUDGET = int(os.environ.get('FM1_API_CONN_BUDGET', '0')) or None

    BUTTONS = ('oct-', 'oct+', 'fx', 'scl', 'env', 'lfo', 'edit', 'glo',
               'home', 'save', 'arp', 'seq', 'play', 'rec')
    ENCODERS = ('select', 'algorithm', 'knob1', 'knob2', 'knob3', 'knob4', 'presets')

    def __init__(self, host, budget=None):
        self.host = host
        self.base = f'http://{host}'
        self.requests = 0
        self.budget = budget
        self.throttled_s = 0.0
        self._conn_times = collections.deque()

    # -- transport --

    def _spend_connection(self):
        """Take a connection from the budget; returns the seconds waited for it."""
        if not self.budget:
            return 0.0
        wait = 0.0
        now = time.monotonic()
        while self._conn_times and now - self._conn_times[0] > self.CONN_WINDOW_S:
            self._conn_times.popleft()
        if len(self._conn_times) >= self.budget:
            wait = self.CONN_WINDOW_S - (now - self._conn_times[0]) + 0.05
            time.sleep(wait)
            self.throttled_s += wait
            self._conn_times.popleft()
        self._conn_times.append(time.monotonic())
        return wait

    def reserve(self, n):
        """Wait until n connections can be made without a budget pause, so a timed
        sequence (press, wait, capture, release) is not stretched in the middle."""
        if not self.budget:
            return
        now = time.monotonic()
        while self._conn_times and now - self._conn_times[0] > self.CONN_WINDOW_S:
            self._conn_times.popleft()
        free = self.budget - len(self._conn_times)
        if free < n:
            idx = n - free - 1                       # the connection that must age out first
            wait = self.CONN_WINDOW_S - (now - self._conn_times[idx]) + 0.05
            time.sleep(max(0.0, wait))
            self.throttled_s += max(0.0, wait)
            io_log.log('throttle', f'reserve {n} connections', 0.0, max(0.0, wait))

    def raw(self, path, timeout=30):
        wait = self._spend_connection()
        self.requests += 1
        req = Request(self.base + path, headers={'Accept': 'application/json'})
        t0 = time.monotonic()
        try:
            with urlopen(req, timeout=timeout) as r:
                body = r.read()
            io_log.log('http', f'GET {path} -> {r.status}, {len(body)} B', time.monotonic() - t0, wait)
            return body
        except HTTPError as e:
            body = e.read().decode('utf-8', 'replace')
            io_log.log('http', f'GET {path} -> HTTP {e.code}', time.monotonic() - t0, wait)
            raise ApiError(f'GET {path}: HTTP {e.code} {body}') from None
        except (URLError, OSError, TimeoutError) as e:
            io_log.log('http', f'GET {path} -> {type(e).__name__}', time.monotonic() - t0, wait)
            raise

    def get(self, path, timeout=30):
        body = self.raw(path, timeout)
        try:
            return json.loads(body)
        except json.JSONDecodeError as e:
            raise ApiError(f'GET {path}: not JSON ({e}): {body[:200]!r}') from None

    def alive(self, timeout=3):
        try:
            self.get('/api/info', timeout=timeout)
            return True
        except (ApiError, URLError, OSError, TimeoutError):
            return False

    # -- state --

    def info(self):
        return self.get('/api/info')

    def pages(self):
        return self.get('/api/pages')['pages']

    def page(self):
        """The page showing, without navigating: {page, index, modal, data?}."""
        return self.get('/api/page')

    def goto(self, page, sub=None):
        """Jump to a page by name (GET /modd/<page>[/<sub>]); returns the reply."""
        return self.get(f'/modd/{page}' + (f'/{sub}' if sub else ''))

    def log(self):
        return self.get('/api/log', timeout=60)['entries']

    def log_mark(self):
        """Absolute index of the next log line (lines ever logged)."""
        return self.get('/api/log', timeout=60)['count']

    def log_since(self, mark):
        """Lines logged since log_mark() returned `mark` (those still in the ring).
        Counting matches in log() is wrong once the ring is full: a new line
        pushes an old one out and the count stays the same."""
        r = self.get('/api/log', timeout=60)
        first = r['count'] - len(r['entries'])
        return r['entries'][max(0, mark - first):]

    def input_state(self):
        return self.get('/api/input')

    def ms(self):
        """Firmware time in ms."""
        return self.input_state()['ms']

    def wait_ms(self, ms, since=None):
        """Wait until `ms` of firmware time have passed since `since` (a firmware
        time; now if None). Sleeps in real time and checks the clock, so a slow
        emulator costs time, not correctness. Returns the firmware time reached."""
        start = self.ms() if since is None else since
        now = start
        while now < start + ms:
            time.sleep(max(0.03, (start + ms - now) / 1000.0 * 1.05))
            now = self.ms()
        return now

    def wait_idle(self, timeout=30, first_sleep=0.05):
        """Until timed releases are done, the scan agrees with what the API holds,
        and the UI has taken every encoder detent."""
        time.sleep(first_sleep)
        t0 = time.monotonic()
        while True:
            st = self.input_state()
            if st['idle']:
                return st
            if time.monotonic() - t0 > timeout:
                raise ApiError(f'input not idle after {timeout} s: {st}')
            time.sleep(0.25)

    def wait_for(self, predicate, timeout=30, poll=0.25, what='condition'):
        """Poll predicate() until it returns something truthy; return that."""
        t0 = time.monotonic()
        last = None
        while time.monotonic() - t0 < timeout:
            last = predicate()
            if last:
                return last
            time.sleep(poll)
        raise ApiError(f'timed out after {timeout} s waiting for {what} (last: {last!r})')

    # -- input --

    def tap(self, button, ms=80, settle=True):
        r = self.get(f'/api/input/button/{button}/tap/{ms}')
        if settle:
            self.wait_idle(first_sleep=ms / 1000.0 + 0.05)
        return r

    def hold(self, button, ms=700, settle=True):
        """A long press (the modd hold threshold is 500 ms of firmware time)."""
        r = self.get(f'/api/input/button/{button}/hold/{ms}')
        if settle:
            self.wait_idle(first_sleep=ms / 1000.0 + 0.05)
        return r

    def button_down(self, button):
        return self.get(f'/api/input/button/{button}/down')

    def button_up(self, button):
        return self.get(f'/api/input/button/{button}/up')

    def enc(self, encoder, clicks, settle=True):
        r = self.get(f'/api/input/enc/{encoder}/{int(clicks)}')
        if settle:
            self.wait_idle()
        return r

    @staticmethod
    def _keys(notes):
        """'C4' / 60 / ['C4', 'E4', 'G4'] -> '60' / '60,64,67' (one request, one scan frame)."""
        if isinstance(notes, (list, tuple)):
            return ','.join(str(midi_note(n)) for n in notes)
        return str(midi_note(notes))

    def key_down(self, notes):
        return self.get(f'/api/input/key/{self._keys(notes)}/down')

    def key_up(self, notes):
        return self.get(f'/api/input/key/{self._keys(notes)}/up')

    def key_tap(self, notes, ms=80, settle=True):
        r = self.get(f'/api/input/key/{self._keys(notes)}/tap/{ms}')
        if settle:
            self.wait_idle(first_sleep=ms / 1000.0 + 0.05)
        return r

    def pot(self, value):
        return self.get(f'/api/input/pot/master/{int(value)}')

    def pot_release(self):
        return self.get('/api/input/pot/master/release')

    # -- tracks --

    def track(self, which='sel'):
        return self.get(f'/api/track/{which}')

    def set_engine(self, name, which='sel'):
        return self.get(f'/api/track/{which}/engine/{name}')

    # -- audio --

    def audio(self):
        return self.get('/api/audio')

    def capture(self, with_meta=False):
        """The last 8192 output samples (mono int16, 44.1 kHz, 186 ms) as an array('h');
        with_meta: (samples, reply without the data), the reply has `ms` and `notes`."""
        d = self.get('/api/audio/capture', timeout=60)
        a = array.array('h')
        a.frombytes(base64.b64decode(d.pop('data')))
        if len(a) != d['n']:
            raise ApiError(f'capture: {len(a)} samples, expected {d["n"]}')
        return (a, d) if with_meta else a

    def record_chord(self, notes, hold_ms=300, attempts=3):
        """record_chord_once, again when the capture came late: under a CPU quota (other
        builds in the container) fm1-live can stall, and a late capture of a decaying
        sound measures its tail. Fails if every attempt is late."""
        for i in range(attempts):
            try:
                return self.record_chord_once(notes, hold_ms)
            except LateCapture:
                if i == attempts - 1:
                    raise

    def record_chord_once(self, notes, hold_ms=300):
        """Hold note keys, capture while they sound, release; returns the samples.
        The capture is the last 186 ms, so it lies inside the held notes; the
        capture reply's `notes` mask proves they were still down."""
        notes = [midi_note(n) for n in notes]
        mask = sum(1 << (n - 53) for n in notes)
        self.reserve(6)         # down, 1-3 clock reads, capture, up: no budget pause in between
        t0 = self.key_down(notes)['ms']
        try:
            self.wait_ms(hold_ms, since=t0)
            samples, meta = self.capture(with_meta=True)
        finally:
            self.key_up(notes)
        if meta['notes'] & mask != mask:
            raise ApiError(f'capture taken with keys {meta["notes"]:#x} down, expected {mask:#x}')
        late = meta['ms'] - t0 - hold_ms
        if late > 250:          # a decaying sound (FM6's piano) would be measured in its tail
            self.wait_idle(first_sleep=0.1)
            raise LateCapture(f'capture {late} ms later than planned ({hold_ms} ms after the press)')
        self.wait_idle(first_sleep=0.1)
        return samples

    def record_note(self, note, hold_ms=300):
        return self.record_chord([note], hold_ms)

    # -- memory --

    def mem_find(self, data):
        return self.get('/api/mem/find/' + bytes(data).hex(), timeout=60)

    def mem_read(self, addr, length):
        d = self.get(f'/api/mem/{addr:08x}/{length}')
        return base64.b64decode(d['data'])

    # -- LCD --

    def screenshot_rgb565(self):
        d = self.get('/api/screenshot', timeout=60)
        return base64.b64decode(d['data']), d['width'], d['height']

    def screenshot(self):
        """The LCD as a PIL image (RGB, 240x240)."""
        from PIL import Image
        raw, w, h = self.screenshot_rgb565()
        return Image.frombytes('RGB', (w, h), rgb565_to_rgb(raw, w, h))


# ------------------------------------------------------------------ fm1-live --cdp

class CdpPanel:
    """Panel input over fm1-live's --cdp WebSocket (JSON-RPC, FM1.pressButton / FM1.turnEncoder).

    A host-side channel: it does not pass through the firmware's network, so it works
    while the radio is off. Stdlib only (a minimal RFC 6455 client); a thread drains the
    LCD frames the server pushes. pressButton taps for 100 ms of emulated time."""

    BUTTON_IDS = {'oct-': 0, 'oct+': 1, 'fx': 2, 'scl': 3, 'env': 4, 'lfo': 5, 'edit': 6,
                  'glo': 7, 'home': 8, 'save': 9, 'arp': 10, 'seq': 11, 'play': 12, 'rec': 13}
    ENCODER_IDS = {'select': 0, 'algorithm': 1, 'knob1': 2, 'knob2': 3, 'knob3': 4, 'knob4': 5, 'presets': 6}

    def __init__(self, port, timeout=10):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f'GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n'
                           f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n'
                           'Sec-WebSocket-Version: 13\r\n\r\n').encode())
        head = b''
        while b'\r\n\r\n' not in head:
            chunk = self.sock.recv(1)
            if not chunk:
                raise ApiError('cdp: closed during the WebSocket handshake')
            head += chunk
        if b' 101 ' not in head.split(b'\r\n', 1)[0]:
            raise ApiError(f'cdp: handshake refused: {head[:80]!r}')
        self.sock.settimeout(None)
        self._id = 0
        self._lock = threading.Lock()
        self._drain = threading.Thread(target=self._pump, daemon=True)
        self._drain.start()

    def _pump(self):
        try:
            while self.sock.recv(65536):
                pass
        except OSError:
            pass

    def _send_text(self, text):
        data = text.encode()
        mask = os.urandom(4)
        n = len(data)
        head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack('>H', n))
        body = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        with self._lock:
            self.sock.sendall(head + mask + body)

    def call(self, method, **params):
        self._id += 1
        self._send_text(json.dumps({'jsonrpc': '2.0', 'id': self._id, 'method': method, 'params': params}))
        io_log.log('cdp', f'{method} {params}', 0.0, count=False)

    def press(self, button):
        """Tap a panel button by name (oct-, oct+, home, ...)."""
        self.call('FM1.pressButton', id=self.BUTTON_IDS[button])

    def enc(self, encoder, clicks):
        self.call('FM1.turnEncoder', encoder=self.ENCODER_IDS[encoder], clicks=int(clicks))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ------------------------------------------------------------------ fm1-live

class Fm1Live:
    """One fm1-live process with a port forward to the firmware's RPC server."""

    def __init__(self, fwsc, flags=(), log_path=None):
        self.fwsc = Path(fwsc)
        self.flags = list(flags)
        self.port = _free_port()
        self.log_path = Path(log_path) if log_path else None
        self.proc = None
        self._log = None

    @staticmethod
    def _windows():
        """X11 window ids of fm1-live windows (title "FM-1 ..."), or None without xdotool."""
        try:
            r = subprocess.run(['xdotool', 'search', '--name', '^FM-1'], capture_output=True, text=True, timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            return None
        return set(r.stdout.split())

    def start(self, timeout=90, wait_api=True):
        self._windows_before = self._windows()
        env = {**os.environ}
        env.setdefault('DISPLAY', ':99')    # live mode opens a window (Xvfb in containers and CI)
        cmd = [str(FM1_LIVE), '--fwsc', str(self.fwsc), *self.flags,
               '--forward', f'{self.port}:{RPC_PORT}']
        if self.log_path:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            self._log = self.log_path.open('w')
            self._log.write('$ ' + ' '.join(cmd) + '\n')
            self._log.flush()
        io_log.log('emu', '$ ' + ' '.join(cmd), count=False)
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                                     cwd=str(ROOT), text=True, errors='replace', bufsize=1)
        self._reader = threading.Thread(target=self._pump, daemon=True)
        self._reader.start()
        api = FirmwareApi(f'127.0.0.1:{self.port}', budget=FirmwareApi.CONN_BUDGET)
        if not wait_api:
            return api
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            if self.proc.poll() is not None:
                raise RuntimeError(f'fm1-live exited with {self.proc.returncode}: {self.tail()}')
            if api.alive(timeout=2):
                self.boot_s = time.monotonic() - t0
                io_log.log('emu', f'firmware API up on port {self.port}', self.boot_s, count=False)
                return api
            time.sleep(1.0)     # (a refused connect does not use a NAT slot, but be modest)
        self.stop()
        raise RuntimeError(f'firmware API not reachable after {timeout} s: {self.tail()}')

    def _pump(self):
        """fm1-live's output: into its log file and, timestamped, into the test's debug.log."""
        for line in self.proc.stdout:
            log = self._log
            if log is not None:
                try:
                    log.write(line)
                    log.flush()
                except ValueError:                  # closed by stop()
                    pass
            io_log.log('fm1-live', line.rstrip('\n'), count=False)

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def tail(self, n=20):
        if not self.log_path or not self.log_path.exists():
            return ''
        return '\n'.join(self.log_path.read_text(errors='replace').splitlines()[-n:])

    def halted(self):
        """The emulator's own report of a firmware halt (bad instruction, fault)."""
        return 'halt:' in (self.log_path.read_text(errors='replace') if self.log_path and self.log_path.exists() else '')

    def quit(self, timeout=15):
        """Close the window so fm1-live exits normally and writes its --flash image back (SIGTERM
        kills it first). Needs xdotool and X11; False when it cannot, then stop() is the fallback.
        A window that was there before this process started is not touched."""
        before = self._windows_before
        mine, t0 = set(), time.monotonic()
        while not mine and time.monotonic() - t0 < 10 and self.running():   # the title is set a little after boot
            now = self._windows()
            if before is None or now is None:
                return False
            mine = now - before
            if not mine:
                time.sleep(0.5)
        if not mine:
            return False
        mine = sorted(mine)
        for w in mine:
            subprocess.run(['xdotool', 'windowquit', w], capture_output=True, timeout=10)
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return False
        return True

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
        if getattr(self, '_reader', None):
            self._reader.join(timeout=5)
        if self._log:
            self._log.close()
            self._log = None
        io_log.log('emu', 'fm1-live stopped', count=False)


# ------------------------------------------------------------------ test case

class ApiTestCase(unittest.TestCase):
    """Base class: one fm1-live per test class, driven over HTTP.

    Class attributes: DEFCONFIG (firmware to boot), EMU_FLAGS (fm1-live options:
    the USB ethernet adapter gives the firmware its network link, which the RPC
    server needs). `self.api` is the FirmwareApi; `self.emu` the process (None
    with RPC_HOST)."""

    DEFCONFIG = DEFAULT_DEFCONFIG
    EMU_FLAGS = ('--usb', 'ethernet', '--net', 'internet')
    CDP = False             # True: also start fm1-live with --cdp <free port>, a host-side panel channel
                            # that works while the firmware is off the network (cls.cdp_port, CdpPanel)

    FLASH = False           # True: fm1-live runs on a flash image file kept across restart_emu()
    api = None
    emu = None
    cdp_port = None
    flash_path = None
    _emu_args = None

    @classmethod
    def results_dir(cls):
        return io_log.results_dir(cls.__module__.rsplit('.', 1)[-1])

    @classmethod
    def setUpClass(cls):
        host = os.environ.get('RPC_HOST')
        if host:
            cls.api = FirmwareApi(host)
            if not cls.api.alive(timeout=10):
                raise unittest.SkipTest(f'RPC_HOST={host} does not answer /api/info')
            return
        fwsc = api_fwsc(cls.DEFCONFIG)
        if not fwsc.is_file():
            raise unittest.SkipTest(f'Firmware not built ({fwsc}): task modd:build:{cls.DEFCONFIG}')
        if not FM1_LIVE.is_file():
            raise unittest.SkipTest(f'fm1-live not found ({FM1_LIVE}): build fm1-emu or set FM1_LIVE')
        # FM1_API_EMU_FLAGS (the task's EMU_FLAGS, e.g. "--wifi oss --net internet") replaces the default
        flags = os.environ.get('FM1_API_EMU_FLAGS', '').split() or list(cls.EMU_FLAGS)
        if cls.CDP:
            cls.cdp_port = _free_port()
            flags += ['--cdp', str(cls.cdp_port)]
        if cls.FLASH:
            cls.flash_path = cls.results_dir() / f'flash-{cls.__name__}.img'
            cls.flash_path.unlink(missing_ok=True)      # a fresh chip: the first boot writes the defaults
            flags += ['--flash', str(cls.flash_path)]
        cls._emu_args = (fwsc, flags)
        cls._boots = 1
        cls.emu = Fm1Live(fwsc, flags, cls.results_dir() / f'fm1-live-{cls.__name__}.log')
        cls.api = cls.emu.start()

    @classmethod
    def restart_emu(cls, timeout=90, wait_api=True, add_flags=()):
        """Stop fm1-live and start it again with the same flags plus add_flags (and, with FLASH, the
        same flash image: settings and projects survive). Returns the new FirmwareApi (also
        cls.api). With wait_api=False it does not wait for the REST API (a firmware off the network
        never answers).

        fm1-live writes its --flash image only when its window is closed: quit() does that with
        xdotool (X11), and the test is skipped without it. In --cdp mode there is no window and no
        write at all, but the image is still read at start: so a boot that saves a setting runs
        without --cdp, and a later one may add `--cdp <port>` through add_flags."""
        fwsc, flags = cls._emu_args
        if cls.FLASH and not cls.emu.quit():
            raise unittest.SkipTest('cannot close the fm1-live window (xdotool, X display, and no --cdp '
                                    'are needed): its flash image is written only on a normal exit')
        cls.emu.stop()
        flags = flags + list(add_flags)
        cls._emu_args = (fwsc, flags)
        cls._boots += 1
        cls.emu = Fm1Live(fwsc, flags, cls.results_dir() / f'fm1-live-{cls.__name__}-boot{cls._boots}.log')
        cls.api = cls.emu.start(timeout=timeout, wait_api=wait_api)
        return cls.api

    @classmethod
    def tearDownClass(cls):
        if cls.emu:
            cls.emu.stop()
            cls.emu = None

    # -- helpers for tests --

    def assertAlive(self):
        """The firmware still answers and the emulator reported no halt."""
        if self.emu:
            self.assertFalse(self.emu.halted(), f'firmware halted:\n{self.emu.tail()}')
            self.assertTrue(self.emu.running(), f'fm1-live exited:\n{self.emu.tail()}')
        self.assertTrue(self.api.alive(timeout=10), 'firmware stopped answering /api/info')

    def go_home(self):
        r = self.api.goto('home')
        self.assertEqual(r['page'], 'HOME')
        return r

    def save_json(self, name, data):
        d = self.results_dir()
        d.mkdir(parents=True, exist_ok=True)
        (d / name).write_text(json.dumps(data, indent=2) + '\n')

    def save_wav(self, name, samples):
        return write_wav(self.results_dir() / name, samples)

    def screenshot(self, name):
        """Save the LCD to screenshots/lcd/<ts>-<name>.png; returns the PIL image."""
        img = self.api.screenshot()
        d = self.results_dir() / 'screenshots' / 'lcd'
        d.mkdir(parents=True, exist_ok=True)
        ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%S.%f')[:-3] + 'Z'
        img.save(d / f'{ts}-{name}.png')
        return img

    @staticmethod
    def _rgb(img):
        b = img.convert('RGB').tobytes()
        return zip(b[0::3], b[1::3], b[2::3])

    @classmethod
    def lit_pixels(cls, img, threshold=30):
        """Pixels with any channel above threshold."""
        return sum(1 for p in cls._rgb(img) if max(p) > threshold)

    @classmethod
    def coloured_pixels(cls, img):
        """Saturated, bright pixels (weather cards, sky gradients)."""
        return sum(1 for r, g, b in cls._rgb(img) if max(r, g, b) - min(r, g, b) > 40 and max(r, g, b) > 120)
