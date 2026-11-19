#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""HTTP smoke test: walk REST API pages, capture screenshots, assert data.

Connects to a running firmware (fm1-live with --forward, or real hardware)
via the REST API on port 9222. Navigates pages, verifies JSON responses,
and saves LCD screenshots to tests/results/<timestamp>-http_smoke/.

Usage:
    # Against fm1-live (start it first with --forward 9222:9222):
    uv run python -m pytest tests/ui/themes/modd/http/http_smoke_test.py -v

    # With a custom host:
    RPC_HOST=10.0.2.15:9222 uv run python -m pytest tests/ui/themes/modd/http/http_smoke_test.py -v

    # Via Taskfile (builds firmware, starts fm1-live, runs test, collects results):
    task modd:test:modd_dev_rpc:api
"""
import base64
import json
import os
import struct
import unittest
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import urlopen, Request
from urllib.error import URLError

RESULTS_DIR = Path(__file__).resolve().parents[4] / 'results'
HOST = os.environ.get('RPC_HOST', '127.0.0.1:9222')
BASE = f'http://{HOST}'

_run_ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')


def _get(path, timeout=5):
    """GET a path, return parsed JSON."""
    url = f'{BASE}{path}'
    req = Request(url, headers={'Accept': 'application/json'})
    with urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read())


def _screenshot_png(data_b64, width, height):
    """Convert RGB565 base64 data to a PNG byte string."""
    raw = base64.b64decode(data_b64)
    pixels = []
    for i in range(0, len(raw), 2):
        if i + 1 >= len(raw):
            break
        px = struct.unpack('<H', raw[i:i+2])[0]
        r = ((px >> 11) & 0x1F) * 255 // 31
        g = ((px >> 5) & 0x3F) * 255 // 63
        b = (px & 0x1F) * 255 // 31
        pixels.extend([r, g, b])

    try:
        from PIL import Image
        img = Image.frombytes('RGB', (width, height), bytes(pixels))
        import io
        buf = io.BytesIO()
        img.save(buf, format='PNG')
        return buf.getvalue()
    except ImportError:
        return None


class TestHTTPSmoke(unittest.TestCase):
    """REST API smoke test with screenshots."""

    _results = None

    @classmethod
    def _results_dir(cls):
        if cls._results is None:
            cls._results = RESULTS_DIR / f'{_run_ts}-http_smoke'
            cls._results.mkdir(parents=True, exist_ok=True)
            (cls._results / 'screenshots').mkdir(exist_ok=True)
        return cls._results

    @classmethod
    def setUpClass(cls):
        try:
            _get('/api/info', timeout=3)
        except (URLError, OSError) as e:
            raise unittest.SkipTest(
                f'Cannot reach firmware at {BASE}: {e}. '
                f'Start fm1-live with --forward 9222:9222')

    def _save_screenshot(self, name):
        """Capture /api/screenshot and save as PNG."""
        try:
            data = _get('/api/screenshot', timeout=10)
        except (URLError, OSError):
            return
        if 'data' not in data:
            return
        png = _screenshot_png(data['data'], data['width'], data['height'])
        if png:
            path = self._results_dir() / 'screenshots' / f'{name}.png'
            path.write_bytes(png)

    # --- tests ---

    def test_01_api_info(self):
        """GET /api/info returns firmware metadata."""
        r = _get('/api/info')
        self.assertEqual(r['firmware'], 'modd')
        self.assertIn('version', r)
        self.assertIsInstance(r['uptime_ms'], int)
        self.assertGreaterEqual(r['uptime_ms'], 0)
        self._save_screenshot('01_info')
        print(f'\n    firmware={r["firmware"]} version={r["version"]} '
              f'uptime={r["uptime_ms"]}ms', flush=True)

    def test_02_api_audio(self):
        """GET /api/audio returns audio pipeline stats."""
        r = _get('/api/audio')
        self.assertEqual(r['sample_rate'], 44100)
        self.assertEqual(r['block_size'], 128)
        self._save_screenshot('02_audio')
        print(f'\n    sample_rate={r["sample_rate"]} block={r["block_size"]}',
              flush=True)

    def test_03_api_engine(self):
        """GET /api/engine returns current engine state."""
        r = _get('/api/engine')
        self.assertIn('engine_index', r)
        self.assertIn('voice_count', r)
        self._save_screenshot('03_engine')
        print(f'\n    engine_index={r["engine_index"]} '
              f'voice_count={r["voice_count"]}', flush=True)

    def test_04_navigate_home(self):
        """GET /modd/home navigates to home and returns page data."""
        r = _get('/modd/home')
        self.assertTrue(r['ok'])
        self.assertEqual(r['page'], 'HOME')
        self.assertIn('data', r)
        d = r['data']
        self.assertGreaterEqual(d['selected_track'], 0)
        self.assertEqual(len(d['tracks']), 4)
        self.assertIn('engine', d['tracks'][0])
        self.assertIn('palette_name', d)
        self._save_screenshot('04_home')
        print(f'\n    page={r["page"]} track={d["selected_track"]} '
              f'palette={d["palette_name"]}', flush=True)

    def test_05_navigate_menu(self):
        """GET /modd/menu opens settings page."""
        r = _get('/modd/menu')
        self.assertTrue(r['ok'])
        self.assertEqual(r['page'], 'MENU')
        self.assertIn('tab', r['data'])
        self._save_screenshot('05_menu')
        print(f'\n    page={r["page"]} tab={r["data"]["tab"]}', flush=True)

    def test_06_navigate_menu_net(self):
        """GET /modd/menu/net switches to NET tab."""
        r = _get('/modd/menu/net')
        self.assertTrue(r['ok'])
        self.assertEqual(r['data']['tab'], 'NET')
        self.assertIn('link', r['data'])
        self.assertIn('ip', r['data'])
        self.assertIn('connected', r['data'])
        self._save_screenshot('06_menu_net')
        print(f'\n    tab={r["data"]["tab"]} link={r["data"]["link"]} '
              f'connected={r["data"]["connected"]}', flush=True)

    def test_07_navigate_weather(self):
        """GET /modd/wx opens weather page."""
        r = _get('/modd/wx')
        self.assertTrue(r['ok'])
        self.assertEqual(r['page'], 'WX')
        self.assertIn('valid', r['data'])
        self.assertIn('fetching', r['data'])
        self._save_screenshot('07_weather')
        print(f'\n    page={r["page"]} valid={r["data"]["valid"]}', flush=True)

    def test_08_navigate_demo(self):
        """GET /modd/demo opens demo page."""
        r = _get('/modd/demo')
        self.assertTrue(r['ok'])
        self._save_screenshot('08_demo')
        print(f'\n    page={r["page"]}', flush=True)

    def test_09_navigate_log(self):
        """GET /modd/log opens debug log."""
        r = _get('/modd/log')
        self.assertTrue(r['ok'])
        self._save_screenshot('09_log')
        print(f'\n    page={r["page"]}', flush=True)

    def test_10_api_log(self):
        """GET /api/log returns debug log entries."""
        r = _get('/api/log')
        self.assertIn('count', r)
        self.assertIn('entries', r)
        self.assertIsInstance(r['entries'], list)
        print(f'\n    count={r["count"]} entries={len(r["entries"])}',
              flush=True)

    def test_11_roundtrip_home(self):
        """Navigate back to home to confirm round-trip works."""
        r = _get('/modd/home')
        self.assertTrue(r['ok'])
        self.assertEqual(r['page'], 'HOME')
        self._save_screenshot('11_roundtrip_home')
        print(f'\n    round-trip OK, page={r["page"]}', flush=True)

    def test_12_api_screenshot(self):
        """GET /api/screenshot returns framebuffer data."""
        try:
            r = _get('/api/screenshot', timeout=10)
        except (URLError, OSError) as e:
            self.skipTest(f'screenshot endpoint not available: {e}')
        self.assertEqual(r['width'], 240)
        self.assertEqual(r['height'], 240)
        self.assertEqual(r['format'], 'rgb565')
        raw = base64.b64decode(r['data'])
        expected_size = 240 * 240 * 2
        self.assertEqual(len(raw), expected_size,
                         f'Expected {expected_size} bytes, got {len(raw)}')
        print(f'\n    framebuffer: {r["width"]}x{r["height"]} {r["format"]} '
              f'({len(raw)} bytes)', flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
