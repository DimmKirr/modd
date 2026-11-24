#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""USB SysEx safety: every firmware stays reflashable over USB.

`task flash` updates the FM-1 with a SysEx message (the UBOOT key, F0 22 24 35 7D F7)
that the running firmware answers by rebooting into its updater. A firmware without
that handler can only be recovered in UBOOT mode, by hand. CONFIG_USB defaults to n,
so a defconfig that forgets it builds such a firmware. Four checks:

  1. CONFIG: every defconfig enables CONFIG_USB and lists "usb" in its manifest
     (genconfig only, no build).
  2. BINARY: the UBOOT key is in the built binary of modd, modd_emu and felucca
     (build/<defconfig>/text.bin). melodee and sloop are config-only: they do not
     build yet.
  3. LIVE (production modd, fm1-live headless, about a second): with a computer on
     the port (--usb device) the firmware enumerates as USB-MIDI and STAYS on the bus
     (the OTG port must not leave device mode while a computer drives it); the UBOOT
     key sent over USB-MIDI (FM1_USB_SEND) detaches it and it reboots (the firmware's
     clock in the USB trace starts again) and comes back on the bus.
  4. HOST (production modd, fm1-live, about a second): with only a USB ethernet adapter
     on the port (--usb ethernet) no computer drives it, so the OTG port goes to host
     mode, enumerates the adapter and sends it Ethernet frames.

Every USB event of the live runs is in the session's IO log
(tests/results/<RUN_STAMP>-<run>/usb_otg_presence_safety_test/debug.log, tests/io_log.py) with the
emulator's time.

The test builds nothing. `task test:api` builds those firmwares first, in parallel
(configs/Taskfile.yml, test:modd_emu:api); under `task test:unit` a binary that is not
built skips its check. Results: tests/results/<timestamp>-<run>/usb_otg_presence_safety_test/.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'build'))
from fm1_paths import FM1_LIVE, build_dir as _build_dir
import io_log
from build_fixtures import genconfig_defconfig

ROOT = Path(__file__).resolve().parents[2]
TEST_RESULTS = io_log.results_dir(Path(__file__).stem)

UBOOT_SYSEX_KEY = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])


def _read_binary(defconfig):
    """The linked image of a build (build/<defconfig>/text.bin, else modd.bin / body.bin)."""
    for name in ('text.bin', 'modd.bin', 'body.bin'):
        path = _build_dir(defconfig) / name
        if path.exists():
            return path.read_bytes()
    return None




def _save(filename, data):
    TEST_RESULTS.mkdir(parents=True, exist_ok=True)
    text = json.dumps(data, indent=2) + '\n' if isinstance(data, (list, dict)) else f'{data}\n'
    (TEST_RESULTS / filename).write_text(text)


class TestAllDefconfigsBinaryUSB(unittest.TestCase):
    """Every shipped defconfig produces a binary with USB SysEx support."""

    def _check_defconfig(self, name, built=True):
        dc = ROOT / 'configs' / f'{name}_defconfig'
        if not dc.exists():
            self.skipTest(f'configs/{name}_defconfig not yet created')
        results = TEST_RESULTS / name
        gen_dir = genconfig_defconfig(name, results)
        header = (gen_dir / 'config.h').read_text()
        self.assertIn('#define CONFIG_USB 1', header, f'SAFETY: defconfig {name!r} does not enable CONFIG_USB.')
        manifest = json.loads((gen_dir / 'manifest.json').read_text())['components']
        self.assertIn('usb', manifest, f'SAFETY: defconfig {name!r} manifest missing "usb".')

        if not built:
            return                              # config only: this defconfig does not build yet
        data = _read_binary(name)
        if data is None:
            self.skipTest(f'no {name} build: task modd:build:{name} (task test:api builds it)')
        offset = data.find(UBOOT_SYSEX_KEY)
        _save(f'all-{name}-uboot-key.json', {'defconfig': name, 'uboot_key_found': offset >= 0,
                                             'uboot_key_offset': offset, 'binary_size': len(data)})
        self.assertGreaterEqual(offset, 0, f'SAFETY: UBOOT SysEx key not in the {name} binary. '
                                           'Device will NOT be reflashable over USB.')

    def test_modd(self):
        self._check_defconfig('modd')

    def test_modd_emu(self):
        self._check_defconfig('modd_emu')

    def test_felucca(self):
        self._check_defconfig('felucca')

    def test_melodee(self):
        self._check_defconfig('melodee', built=False)

    def test_sloop(self):
        self._check_defconfig('sloop', built=False)


KEY_AT_MS = 8000            # after the ~6 s a host-mode probe would take to show
RUN_MS = 11000


def _usb_session(fwsc, role='device', send=None, run_ms=RUN_MS):
    """Boot FWSC headless in fm1-live with ROLE on the USB port ('device': a computer;
    'ethernet': a CDC-ECM adapter, the FM-1 the host). Returns the emulator's device-side
    events [(ms, 'Configured'|'Detached'|'out')], the firmware's trace clock per 'pull-up:
    device attached' line (it starts again after a reboot), the host-side transfers in order
    and the process result. Every event goes to the IO log (tests/io_log.py)."""
    env = {**os.environ, 'FM1_USB_TRACE': '1'}
    if send:
        env['FM1_USB_SEND'] = send
    io_log.log('emu', f'USB role {role}, {run_ms} ms' + (f', FM1_USB_SEND={send}' if send else ''), count=False)
    with tempfile.TemporaryDirectory() as work:
        r = subprocess.run([str(FM1_LIVE), '--fwsc', str(fwsc), '--usb', role, '--shot', str(run_ms)],
                           capture_output=True, text=True, cwd=work, env=env, timeout=120)
    events = [(int(m[1]), m[2] or 'out') for m in
              re.finditer(r'^\[\s*(\d+) ms\] usb(?:: (Configured|Detached)| (?:out))', r.stdout, re.M)]
    attaches = [int(m[1]) for m in re.finditer(r'^usb\s+(\d+): pull-up: device attached', r.stderr, re.M)]
    host = re.findall(r'^usb host: (ep\d+ (?:IN|OUT) addr=\d+)[^\n]*', r.stderr, re.M)
    for line in r.stdout.splitlines():
        m = re.match(r'^\[\s*(\d+) ms\] (usb.*)$', line)
        if m:
            io_log.log('usb', f'emu {int(m[1]):6d} ms  {m[2]}', count=False)
    for line in r.stderr.splitlines():
        m = re.match(r'^usb\s+(\d+): (.*)$', line)
        if m:
            io_log.log('usb', f'emu {int(m[1]):6d} ms  trace: {m[2]}', count=False)
    # host transfers carry no time: in order, repeats of one endpoint and direction collapsed
    run, prev = 0, None
    for h in host + [None]:
        if h == prev:
            run += 1
            continue
        if prev is not None:
            io_log.log('usb', f'host {prev}' + (f'  x{run}' if run > 1 else ''), count=False)
        prev, run = h, 1
    return events, attaches, host, r


def _print_timeline(title, events, attaches=(), host=()):
    """The USB timeline in the test output (pytest -s, or on a failure)."""
    print(f'\n  {title}')
    for ms, kind in events:
        print(f'    emu {ms:6d} ms  {kind}')
    if attaches:
        print(f'    firmware pull-up at its own ms: {list(attaches)}')
    if host:
        print(f'    host transfers: {len(host)} (first {host[0]}, last {host[-1]})')


class TestUsbDeviceLive(unittest.TestCase):
    """Production modd in fm1-live with a computer attached: on the bus, and the key works."""

    DEFCONFIG = 'modd'

    @classmethod
    def setUpClass(cls):
        fwsc = _build_dir(cls.DEFCONFIG) / 'modd.fwsc'
        if not fwsc.is_file():
            raise unittest.SkipTest(f'no {cls.DEFCONFIG} build: task modd:build:{cls.DEFCONFIG}')
        if not FM1_LIVE.is_file():
            raise unittest.SkipTest(f'fm1-live not found ({FM1_LIVE})')
        cls.events, cls.attaches, _, r = _usb_session(fwsc, 'device', send=f'{KEY_AT_MS}:F02224357DF7')
        _print_timeline(f'{cls.DEFCONFIG}, a computer on the port, UBOOT key at {KEY_AT_MS} ms',
                        cls.events, cls.attaches)
        _save(f'live-{cls.DEFCONFIG}-usb.json', {'key_at_ms': KEY_AT_MS, 'events': cls.events,
                                                'trace_attaches_ms': cls.attaches})
        _save(f'live-{cls.DEFCONFIG}-fm1-live.log', r.stdout + '\n--- stderr ---\n' + r.stderr)

    def test_01_enumerates_as_usb_midi(self):
        self.assertTrue(any(k == 'Configured' and ms < KEY_AT_MS for ms, k in self.events),
                        'the firmware never enumerated as a USB device: task flash cannot reach it')

    def test_02_stays_on_the_bus(self):
        """From the first enumeration until the key: no detach while the computer drives the port."""
        first = next((ms for ms, k in self.events if k == 'Configured'), None)
        if first is None:
            self.skipTest('never enumerated (test_01)')
        drops = [ms for ms, k in self.events if k == 'Detached' and first < ms < KEY_AT_MS]
        self.assertEqual(drops, [], f'SAFETY: the USB device left the bus at {drops} ms with a computer '
                                    'attached (OTG port left device mode): task flash finds no FM-1 then')

    def test_03_uboot_key_reboots_into_the_updater(self):
        delivered = [ms for ms, k in self.events if k == 'out' and ms >= KEY_AT_MS]
        self.assertTrue(delivered, 'the UBOOT key was not delivered over USB-MIDI')
        gone = [ms for ms, k in self.events if k == 'Detached' and ms >= delivered[0]]
        self.assertTrue(gone and gone[0] - delivered[0] < 200,
                        f'SAFETY: no detach within 200 ms of the UBOOT key (events {self.events})')
        # the firmware's trace clock starts again at a reset: a second pull-up stamped before the
        # key was sent is a restart (boot time differs by a ms between builds, so no b <= a)
        rebooted = len(self.attaches) >= 2 and self.attaches[-1] < KEY_AT_MS
        self.assertTrue(rebooted, 'SAFETY: the firmware did not restart after the UBOOT key '
                                  f'(trace attaches {self.attaches})')
        back = [ms for ms, k in self.events if k == 'Configured' and ms > gone[0]]
        self.assertTrue(back, 'the device did not come back on the bus after the restart')



class TestUsbHostWithoutComputer(unittest.TestCase):
    """Production modd with only a USB ethernet adapter on the port: no computer drives it,
    so the OTG port must go to host mode and run the adapter (the network link)."""

    DEFCONFIG = 'modd'
    RUN_MS = 12000

    @classmethod
    def setUpClass(cls):
        fwsc = _build_dir(cls.DEFCONFIG) / 'modd.fwsc'
        if not fwsc.is_file():
            raise unittest.SkipTest(f'no {cls.DEFCONFIG} build: task modd:build:{cls.DEFCONFIG}')
        if not FM1_LIVE.is_file():
            raise unittest.SkipTest(f'fm1-live not found ({FM1_LIVE})')
        cls.events, _, cls.host, r = _usb_session(fwsc, 'ethernet', run_ms=cls.RUN_MS)
        _print_timeline(f'{cls.DEFCONFIG}, a USB ethernet adapter on the port', cls.events, host=cls.host)
        _save(f'live-{cls.DEFCONFIG}-ethernet.json', {'events': cls.events, 'host_transfers': cls.host})
        _save(f'live-{cls.DEFCONFIG}-ethernet-fm1-live.log', r.stdout + '\n--- stderr ---\n' + r.stderr)

    def test_01_goes_to_host_and_enumerates_the_adapter(self):
        self.assertTrue(any(h.endswith('addr=1') and h.startswith('ep0') for h in self.host),
                        'the OTG port never went to host mode: no adapter enumerated (address 1)')

    def test_02_sends_frames_to_the_adapter(self):
        frames = [h for h in self.host if h.startswith('ep1 OUT') or h.startswith('ep2 OUT')]
        self.assertTrue(frames, 'host mode, but no Ethernet frames sent to the adapter (bulk OUT)')

    def test_03_not_a_usb_device_meanwhile(self):
        self.assertFalse([e for e in self.events if e[1] == 'Configured'],
                         'enumerated as a USB device with no computer on the port')


if __name__ == '__main__':
    unittest.main()
