#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: the WiFi radio follows the WIFI setting and the network "need" at run time.

firmware/src/net/net_need.c maps consumers' net_need() / net_release() onto one link
(wired USB ethernet preferred: while an ethernet netif has an address the WiFi need is
dropped); firmware/src/wifi/wifi_need.c keeps the radio active only while the WiFi
setting (Settings > HW > WIFI) is on AND a need is held. It logs "wifi: activate" and
"wifi: deactivate" on every change. wifi_poll() (the WiFi task) does the work: "wifi: init
done" when the radio comes up, "wifi: deinit start" / "wifi: deinit done" when it goes down
(WIFI off), and "wifi: init done" again when it comes back. While it is down nothing polls.

  TestWifiOffOn      WiFi is the only link (--wifi oss --net internet). The REST API
                     travels over WiFi, so WIFI off cuts the API: the switch is made with
                     the panel through fm1-live's --cdp channel (host side, no network).
  TestWifiEthernet   WiFi and USB ethernet together (--usb ethernet --wifi oss --net
                     internet): ethernet wins, the radio is not needed while it is up.

Each class needs its own emulator flags and sets them itself; when FM1_API_EMU_FLAGS
names flags without them, that class is skipped.

Run: build/modd_emu/modd.fwsc (task modd:build:modd_emu) then
    pytest tests/wifi/api_wifi_need_test.py -v
"""
import re
import sys
import time
import unittest
from pathlib import Path
from urllib.error import URLError

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import os
from api_fixtures import ApiError, ApiTestCase, CdpPanel, api_fwsc

WIFI_STACK_BYTES = 4096        # static uint32_t wifi_stack[1024], modd.c
WIFI_STACK_MIN_FREE = 512      # bytes that must stay untouched after init, join, weather, off -> on
STACK_CANARY = bytes.fromhex('efbeadde')       # 0xDEADBEEF little-endian: sched_create() paints the stack


def _env_flags():
    return os.environ.get('FM1_API_EMU_FLAGS', '').split()


def _has(flags, *wanted):
    return all(w in flags for w in wanted)


class WifiNeedCase(ApiTestCase):
    CDP = True
    NEEDS = ()          # flags the class needs when FM1_API_EMU_FLAGS replaces EMU_FLAGS
    FORBIDS = ()

    @classmethod
    def setUpClass(cls):
        flags = _env_flags()
        if os.environ.get('RPC_HOST'):
            raise unittest.SkipTest('needs its own fm1-live with --cdp, not RPC_HOST')
        if flags and (not _has(flags, *cls.NEEDS) or any(f in flags for f in cls.FORBIDS)):
            raise unittest.SkipTest(f'needs fm1-live flags {" ".join(cls.EMU_FLAGS)}; '
                                    f'FM1_API_EMU_FLAGS is "{" ".join(flags)}"')
        super().setUpClass()
        cls.panel = None

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, 'panel', None):
            cls.panel.close()
        super().tearDownClass()

    # -- log --

    def wait_for_log(self, mark, line, what, timeout=60):
        """Until `line` is logged after `mark`; the failure lists what was logged instead."""
        seen = []

        def logged():
            seen[:] = self.api.log_since(mark)
            return any(l == line for l in seen)
        try:
            self.api.wait_for(logged, timeout=timeout, poll=2.0, what=what)
        except ApiError:
            self.fail(f'no {what}; logged since: {[l for l in seen if not l.startswith("rpc:")]}')

    @staticmethod
    def wifi_lines(lines):
        return [l for l in lines if l.startswith(('wifi: activate', 'wifi: deactivate', 'wifi: init done',
                                                  'wifi: deinit start', 'wifi: deinit done', 'net: ip'))]

    def wifi_stack_free(self):
        """(address, free bytes): the painted, still untouched part of the WiFi task stack. The
        symbol comes from the map next to the firmware package; the stack grows down from its end
        (sched.c), so the untouched words are the canary run at its low end."""
        mp = Path(api_fwsc()).with_name('modd.map')
        text = mp.read_text(errors='replace')
        m = re.search(r'^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+fm1_main\.wifi_stack\s*$', text, re.M)
        if not m:
            self.skipTest(f'fm1_main.wifi_stack not in {mp}')
        addr, size = int(m.group(1), 16), int(m.group(2), 16)
        self.assertEqual(size, WIFI_STACK_BYTES, 'the WiFi stack size changed: update WIFI_STACK_BYTES')
        raw = self.api.mem_read(addr, size)
        self.assertEqual(len(raw), size)
        if raw[:4] != STACK_CANARY:
            self.fail('the WiFi stack is not painted (CONFIG_SCHED_STACK_CANARY off?) or overflowed')
        free = 0
        while free < size and raw[free:free + 4] == STACK_CANARY:
            free += 4
        return addr, free

    # -- the WIFI setting --

    def select_wifi_row(self):
        """Settings > HW with the WIFI row selected (over HTTP; the selection stays put)."""
        api = self.api
        r = api.goto('menu', 'hw')
        self.assertTrue(r['ok'])
        d = api.wait_for(lambda: (p := api.page())['page'] == 'MENU' and p['data']['tab'] == 'HW' and p['data'],
                         timeout=15, poll=0.5, what='the HW tab')
        names = [i['name'] for i in d['items']]
        self.assertIn('WIFI', names, f'no WIFI row in {names}: not a WiFi build')
        api.enc('algorithm', names.index('WIFI'))
        return self.wifi_item()

    def wifi_item(self):
        d = self.api.page()['data']
        self.assertEqual(d['tab'], 'HW')
        return {i['name']: i for i in d['items']}['WIFI']

    def wifi_setting_on(self):
        it = self.wifi_item()
        return it['index'] == 1


class TestWifiOffOn(WifiNeedCase):
    """WiFi is the only link: the setting off takes the radio away, on brings it back."""

    EMU_FLAGS = ('--wifi', 'oss', '--net', 'internet')
    NEEDS = ('--wifi', 'oss')
    FORBIDS = ('ethernet',)

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.panel = CdpPanel(cls.cdp_port)

    def test_wifi_off_then_on(self):
        api, panel = self.api, self.panel
        # 1. boot: the API answers over WiFi, the radio was activated and got an address
        boot = api.log()
        lines = self.wifi_lines(boot)
        self.assertIn('wifi: activate', lines, f'no activation at boot: {lines}')
        self.assertTrue(any(l.startswith('net: ip') for l in lines), f'no address at boot: {lines}')
        self.assertEqual(lines.count('wifi: init done'), 1, f'init done not once at boot: {lines}')

        # 2. switch WIFI off with the panel (host side): select the row over HTTP first
        item = self.select_wifi_row()
        self.assertEqual(item['index'], 1, f'WIFI is not on at boot: {item}')
        mark = api.log_mark()
        try:
            panel.press('oct-')
            # 3. the radio leaves the network: the API stops answering
            t0 = time.monotonic()
            try:
                api.wait_for(lambda: not api.alive(timeout=3), timeout=45, poll=1.0,
                             what='the API to become unreachable with WIFI off')
            except ApiError as e:
                self.fail(f'{e}: still reachable over WiFi after WIFI off')
            off_s = time.monotonic() - t0

            # 4. stay off longer than the 15 s auto-join interval, so a radio that kept polling
            #    would start a scan; then switch WIFI on again, host side
            time.sleep(17)
            panel.press('oct+')

            # 5. the radio rejoins: the API answers again
            t0 = time.monotonic()
            try:
                api.wait_for(lambda: api.alive(timeout=3), timeout=120, poll=2.0,
                             what='the API to answer again after WIFI on')
            except ApiError as e:
                self.fail(f'{e}: WiFi did not rejoin; fm1-live:\n{self.emu.tail()}')
            on_s = time.monotonic() - t0
        finally:
            # leave the setting on whatever happened (it is off if the cdp press 1 went through
            # and press 2 did not; the API is unreachable then, so judge by reachability)
            if not api.alive(timeout=3):
                panel.press('oct+')

        # the log (reachable again): order and counts after the mark
        got = self.wait_for_log_sequence(mark)
        self.save_json('wifi-off-on.json', {'off_s': off_s, 'on_s': on_s, 'lines': got})
        # off tears the radio down, on initialises it a second time, in that order
        self.assertEqual(got.count('wifi: deinit start'), 1, f'deinit start not once: {got}')
        self.assertEqual(got.count('wifi: deinit done'), 1, f'deinit done not once: {got}')
        self.assertEqual(got.count('wifi: init done'), 1, f'init done not once after WIFI on: {got}')
        self.assertLess(got.index('wifi: deactivate'), got.index('wifi: deinit start'), got)
        self.assertLess(got.index('wifi: deinit done'), got.index('wifi: activate'), got)
        self.assertLess(got.index('wifi: activate'), got.index('wifi: init done'), got)
        # nothing polled while it was down: no scan, auto-join or receive line between the two
        full = self.log_full        # not a second fetch: one came back without a line the first had
        self.save_json('wifi-off-on-full.json', {'lines': [l for l in full if not l.startswith('rpc:')]})
        down = full[full.index('wifi: deinit done') + 1:full.index('wifi: activate')]
        busy = [l for l in down if l.startswith(('wifi: scan', 'wifi: autojoin', 'wifi: rx', 'wifi: sta state',
                                                 'wifi: associated', 'wifi: dhcp'))]
        self.assertEqual(busy, [], f'the radio kept working while down: {busy}')

        # 6. the network works after off -> on: the weather fetch succeeds
        r = api.goto('wx')
        self.assertEqual(r['page'], 'WX')
        d = api.wait_for(lambda: (d := api.page()['data'])['valid'] and d,
                         timeout=90, poll=2.0, what='valid weather data after WiFi off -> on')
        self.assertEqual(d['city'], 'New York')
        self.assertTrue(d['condition'])

        # the setting is on again
        self.assertEqual(self.select_wifi_row()['index'], 1, 'WIFI setting is not on at the end')
        self.assertAlive()

        # the WiFi task stack held init, join, a weather fetch and an off -> on cycle with room to spare
        addr, free = self.wifi_stack_free()
        self.save_json('wifi-stack.json', {'addr': addr, 'size': WIFI_STACK_BYTES,
                                           'free': free, 'used': WIFI_STACK_BYTES - free})
        self.assertGreaterEqual(free, WIFI_STACK_MIN_FREE,
                                f'WiFi stack nearly full: {WIFI_STACK_BYTES - free} of {WIFI_STACK_BYTES} bytes used')

    def wait_for_log_sequence(self, mark):
        """The wifi lines since `mark`, once they hold deactivate, activate and an address in
        that order. If the 400-line ring evicted the start, say so in the failure."""
        got = []

        def ready():
            self.log_full = self.api.log_since(mark)    # the same fetch, unfiltered, for the caller
            got[:] = self.wifi_lines(self.log_full)
            try:
                d = got.index('wifi: deactivate')
                a = got.index('wifi: activate', d + 1)
            except ValueError:
                return False
            return any(l.startswith('net: ip') for l in got[a + 1:])
        try:
            self.api.wait_for(ready, timeout=60, poll=2.0, what='deactivate, activate, net: ip in order')
        except ApiError:
            self.fail(f'wifi lines since the mark: {got}')
        return got


class TestWifiEthernet(WifiNeedCase):
    """WiFi and ethernet together: while ethernet has an address WiFi is not needed."""

    EMU_FLAGS = ('--usb', 'ethernet', '--wifi', 'oss', '--net', 'internet')
    NEEDS = ('ethernet', '--wifi', 'oss')

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.panel = CdpPanel(cls.cdp_port)

    def test_ethernet_preferred(self):
        api = self.api
        lines = self.wifi_lines(api.log())
        self.save_json('wifi-ethernet-boot.json', {'lines': lines})
        self.assertIn('wifi: deactivate', lines, f'WiFi not dropped while ethernet is up: {lines}')
        first_ip = next((i for i, l in enumerate(lines) if l.startswith('net: ip')), None)
        self.assertIsNotNone(first_ip, f'no address: {lines}')
        self.assertGreater(lines.index('wifi: deactivate'), first_ip,
                           f'deactivate came before any address: {lines}')
        self.assertTrue(api.alive(timeout=5), 'the API stopped answering with ethernet up')

        # toggling the setting must not bring the radio up while ethernet is up
        self.select_wifi_row()
        mark = api.log_mark()
        try:
            api.tap('oct-')
            api.wait_for(lambda: not self.wifi_setting_on(), timeout=10, poll=0.5, what='WIFI off')
            api.tap('oct+')
            api.wait_for(lambda: self.wifi_setting_on(), timeout=10, poll=0.5, what='WIFI on')
        finally:
            if not self.wifi_setting_on():
                api.tap('oct+')
        api.wait_ms(3000)
        after = self.wifi_lines(api.log_since(mark))
        self.assertNotIn('wifi: activate', after, f'the radio came up with ethernet up: {after}')
        self.assertTrue(api.alive(timeout=5))
        self.assertTrue(self.wifi_setting_on(), 'WIFI setting not left on')
        self.assertAlive()


class TestWifiOffAtBoot(WifiNeedCase):
    """The WIFI setting saved as off: the next boot never initialises the radio, and the setting
    survives the reboot.

    Ethernet is there only so that the REST API stays readable while WIFI is off (WiFi alone
    cannot be reached then, and the panel cannot hold HOME to open Settings after a reboot).
    Boot 1 switches WIFI off and saves it. Boot 2 runs on the same flash image (FLASH = True,
    restart_emu(): fm1-live writes the image only when its window is closed, which restart_emu()
    does with xdotool), so the saved setting is what the firmware reads. Then WIFI is switched on:
    ethernet is up, so the radio is still not needed and nothing starts.

    Not covered here: the first wifi_init() after such a boot, with the join. It needs the
    ethernet link gone, and modd_emu has CONFIG_USB_NET_AUTO (the ETHERNET row cannot switch it
    off), while over WiFi alone the panel cannot open Settings (HOME hold is longer than the 100 ms
    a --cdp press lasts). wifi_poll() bringing the radio up from "never initialised" is in
    tests/wifi/wifi_power_test.c; the join after a re-init is in TestWifiOffOn."""

    EMU_FLAGS = ('--usb', 'ethernet', '--wifi', 'oss', '--net', 'internet')
    NEEDS = ('ethernet', '--wifi', 'oss')
    FLASH = True
    CDP = False
    SAVE_WAIT_MS = 2000         # settings_task flushes sys_settings_dirty every 50 ms (modd.c)

    def hw_row(self, name):
        d = self.api.page()['data']
        return [i['name'] for i in d['items']].index(name)

    def test_off_survives_reboot_and_on_inits_once(self):
        api = self.api
        # boot 1 on a fresh flash image: WIFI is on (the default); switch it off and let it save
        item = self.select_wifi_row()
        self.assertEqual(item['index'], 1, f'WIFI is not on by default: {item}')
        api.tap('oct-')
        api.wait_for(lambda: not self.wifi_setting_on(), timeout=10, poll=0.5, what='WIFI off')
        api.wait_ms(self.SAVE_WAIT_MS)

        # boot 2 on the same flash image
        api = self.restart_emu()
        self.assertGreater(self.flash_path.stat().st_size, 0, f'no flash image written to {self.flash_path}')
        boot = [l for l in api.log() if not l.startswith('rpc:')]
        self.save_json('wifi-off-boot.json', {'lines': boot})
        self.assertTrue(boot, 'empty log after the restart')
        wifi = [l for l in boot if l.startswith('wifi: ')]
        for bad in ('wifi: init start', 'wifi: init done', 'wifi: activate'):
            self.assertNotIn(bad, wifi, f'the radio was brought up with WIFI saved as off: {wifi}')
        self.assertEqual(self.select_wifi_row()['index'], 0, 'WIFI setting did not survive the reboot')

        # WIFI on while ethernet carries the network: the radio is not needed, nothing starts
        # (the setting is also saved on again, though this flash image is not read afterwards)
        mark = api.log_mark()
        api.tap('oct+')
        api.wait_for(lambda: self.wifi_setting_on(), timeout=10, poll=0.5, what='WIFI on')
        api.wait_ms(3000)
        quiet = self.wifi_lines(api.log_since(mark))
        self.assertNotIn('wifi: activate', quiet, f'the radio came up with ethernet up: {quiet}')
        self.assertNotIn('wifi: init done', quiet, quiet)

        self.save_json('wifi-off-boot-on.json', {'lines': quiet})
        self.assertAlive()


if __name__ == '__main__':
    unittest.main()
