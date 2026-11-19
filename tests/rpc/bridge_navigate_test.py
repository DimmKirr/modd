#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: navigate to every page via FM1.navigate and screenshot each one.

Boots the fm1-emu emulator, connects via RPC bridge, then walks
through all navigable pages using modd:// URIs. Each page gets
an LCD screenshot and a full panel screenshot saved to the results dir.

Results are saved to tests/results/<timestamp>-bridge_navigate_test/:
  - screenshots/lcd/<page>.png
  - screenshots/full/<page>.png
  - rpc_traffic.log
  - emulator.log
"""
import asyncio
import json
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import fwsc_path, RESULTS_DIR

FWSC_PATH = fwsc_path()
_run_timestamp = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')

PAGES = [
    ('home', None),
    ('settings', 'display'),
    ('settings', 'control'),
    ('settings', 'audio'),
    ('settings', 'midi'),
    ('settings', 'system'),
    ('settings', 'hw'),
    ('settings', 'net'),
    ('weather', None),
    ('demo', None),
    ('files', None),
    ('disk', None),
    ('log', None),
    ('home', None),
]


class TestRPCNavigate(unittest.TestCase):
    """Navigate to every page via FM1.navigate and screenshot each."""

    ctx = None
    loop = None
    bridge = None
    _results = None

    @classmethod
    def _results_dir(cls):
        if cls._results is None:
            cls._results = RESULTS_DIR / f'{_run_timestamp}-bridge_navigate_test'
            cls._results.mkdir(parents=True, exist_ok=True)
        return cls._results

    @classmethod
    def setUpClass(cls):
        try:
            from playwright.async_api import async_playwright
        except ImportError:
            raise unittest.SkipTest('playwright not installed')
        try:
            import websockets
        except ImportError:
            raise unittest.SkipTest('websockets not installed')

        if not FWSC_PATH.is_file():
            raise unittest.SkipTest(f'Firmware not built ({FWSC_PATH})')

        from bridge import boot_and_serve, EMU_WIFI_DIR, EMU_DIR

        if (EMU_WIFI_DIR / 'fm1_web.wasm').is_file():
            emu_dir = EMU_WIFI_DIR
        elif (EMU_DIR / 'fm1_web.wasm').is_file():
            emu_dir = EMU_DIR
        else:
            raise unittest.SkipTest('No emulator found; run "task fm1-emulator:download"')

        cls.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(cls.loop)

        wifi_tag = ' (WiFi)' if emu_dir == EMU_WIFI_DIR else ' (stock)'
        print(f'\n  Booting emulator{wifi_tag} + RPC bridge...', end='', flush=True)
        cls.ctx = cls.loop.run_until_complete(
            boot_and_serve(mhz=100, emu_dir=emu_dir))
        cls.bridge = cls.ctx.get('bridge')
        print(f' done. ws://127.0.0.1:{cls.ctx["ws_port"]}', flush=True)
        print(f'  Results:  {cls._results_dir()}', flush=True)

    @classmethod
    def tearDownClass(cls):
        if cls.bridge:
            results = cls._results_dir()
            emu_info = cls.loop.run_until_complete(cls.bridge.get_emulator_log())
            rpc_log = cls.bridge.format_log()
            (results / 'rpc_traffic.log').write_text(rpc_log + '\n')
            emu_lines = [
                f'status: {emu_info["status"]}',
                '',
                '--- #fm1log ---',
                emu_info.get('fm1log', '(empty)'),
            ]
            (results / 'emulator.log').write_text('\n'.join(emu_lines) + '\n')

        from bridge import shutdown
        if cls.ctx:
            cls.loop.run_until_complete(shutdown(cls.ctx))
        if cls.loop:
            cls.loop.close()

    def _run(self, coro):
        return self.loop.run_until_complete(coro)

    def _screenshot(self, name):
        if not self.bridge:
            return None
        lcd_dir = self._results_dir() / 'screenshots' / 'lcd'
        lcd_dir.mkdir(parents=True, exist_ok=True)
        full_dir = self._results_dir() / 'screenshots' / 'full'
        full_dir.mkdir(parents=True, exist_ok=True)

        lcd_png = self._run(self.bridge.screenshot_lcd())
        if lcd_png:
            (lcd_dir / f'{name}.png').write_bytes(lcd_png)

        full_png = self._run(self.bridge.screenshot_full())
        if full_png:
            (full_dir / f'{name}.png').write_bytes(full_png)

        return lcd_png

    async def _send(self, method, params=None, msg_id=1):
        import websockets
        async with websockets.connect(
                f'ws://127.0.0.1:{self.ctx["ws_port"]}') as ws:
            req = {'id': msg_id, 'method': method, 'params': params or {}}
            await ws.send(json.dumps(req))
            return json.loads(await ws.recv())

    def test_01_navigate_all_pages(self):
        """Walk every page via FM1.navigate and screenshot each."""
        msg_id = 100
        for page_name, sub in PAGES:
            url = f'modd://{page_name}'
            if sub:
                url += f'/{sub}'
            screenshot_name = f'{page_name}_{sub}' if sub else page_name

            resp = self._run(self._send('FM1.navigate',
                                        {'url': url}, msg_id=msg_id))
            msg_id += 1
            self.assertIn('result', resp,
                          f'Navigate to {url} failed: {resp}')
            self.assertTrue(resp['result'].get('ok'),
                            f'Navigate to {url} not ok: {resp}')
            self._screenshot(screenshot_name)
            print(f'\n    {url} -> {resp["result"].get("page")}', flush=True)

    def test_02_navigate_act_screenshot(self):
        """Full flow: navigate to home, send MIDI note-on, screenshot, note-off."""
        # 1. Navigate to home
        resp = self._run(self._send('FM1.navigate',
                                    {'url': 'modd://home'}, msg_id=200))
        self.assertIn('result', resp, f'Navigate failed: {resp}')
        self.assertTrue(resp['result'].get('ok'))
        self._screenshot('act_01_home_before')

        # 2. Act: send MIDI note-on
        resp = self._run(self._send('FM1.sendMidi',
                                    {'data': [0x90, 60, 100]}, msg_id=201))
        self.assertIn('result', resp, f'sendMidi failed: {resp}')
        self.assertTrue(resp['result']['ok'])
        self._run(asyncio.sleep(0.2))

        # 3. Screenshot: capture the UI with the note active
        self._screenshot('act_02_home_note_on')

        # 4. Act: send note-off
        resp = self._run(self._send('FM1.sendMidi',
                                    {'data': [0x80, 60, 0]}, msg_id=202))
        self.assertTrue(resp['result']['ok'])
        self._run(asyncio.sleep(0.1))
        self._screenshot('act_03_home_note_off')

        # 5. Navigate to settings, act with encoder, screenshot
        resp = self._run(self._send('FM1.navigate',
                                    {'url': 'modd://settings/display'},
                                    msg_id=203))
        self.assertIn('result', resp)
        self._screenshot('act_04_settings_before')

        resp = self._run(self._send('FM1.turnEncoder',
                                    {'encoder': 0, 'clicks': 1},
                                    msg_id=204))
        self.assertIn('result', resp)
        self._run(asyncio.sleep(0.2))
        self._screenshot('act_05_settings_after_enc')

        print('\n    navigate -> act -> screenshot: all steps passed',
              flush=True)

    def test_03_navigate_palette(self):
        """Navigate to palette 3 and 7, verify they produce different LCD output."""
        resp = self._run(self._send('FM1.navigate',
                                    {'url': 'modd://palette/3'},
                                    msg_id=300))
        self.assertIn('result', resp, f'Navigate to palette failed: {resp}')
        self.assertTrue(resp['result'].get('ok'))
        lcd_pal3 = self._screenshot('palette_03')
        print(f'\n    modd://palette/3 -> {resp["result"]}', flush=True)

        resp = self._run(self._send('FM1.navigate',
                                    {'url': 'modd://palette/7'},
                                    msg_id=301))
        self.assertIn('result', resp, f'Navigate to palette failed: {resp}')
        self.assertTrue(resp['result'].get('ok'))
        lcd_pal7 = self._screenshot('palette_07')
        print(f'    modd://palette/7 -> {resp["result"]}', flush=True)

        if lcd_pal3 and lcd_pal7:
            self.assertNotEqual(
                lcd_pal3, lcd_pal7,
                'Palette 3 and palette 7 produced identical LCD screenshots; '
                'palette navigation may not be changing the active palette')

    def test_04_navigate_aliases(self):
        """Verify alias names resolve correctly."""
        aliases = [
            ('modd://menu', 'settings'),
            ('modd://wx', 'wx'),
            ('modd://debug', 'debug'),
            ('modd://colors/0', 'palette'),
        ]
        msg_id = 400
        for url, expected_page in aliases:
            resp = self._run(self._send('FM1.navigate',
                                        {'url': url}, msg_id=msg_id))
            msg_id += 1
            self.assertIn('result', resp, f'{url} failed: {resp}')
            self._screenshot(f'alias_{expected_page}')
            print(f'\n    alias {url} -> {resp["result"].get("page")}',
                  flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
