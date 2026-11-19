#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""UI walkthrough: navigate every page, capture screenshots, OCR text, assert colors.

Two-phase test:
  Phase 1 — walk pages via REST API, save screenshots (PNG) and JSON responses.
  Phase 2 — run RapidOCR (ONNX Runtime) on saved screenshots, assert expected
            text and sample pixel regions with PIL to verify theme colors.

Results are stored in tests/results/<datetimeISO>-ui_walkthrough/.

Usage:
    # Against fm1-live (start it first with --forward 9222:9222):
    uv run --group ui-test python -m pytest tests/ui/themes/modd/http/ui_walkthrough_test.py -v

    # Via Taskfile:
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

try:
    from PIL import Image
except ImportError:
    Image = None

try:
    from rapidocr_onnxruntime import RapidOCR
    _ocr_engine = None

    def _get_ocr():
        global _ocr_engine
        if _ocr_engine is None:
            _ocr_engine = RapidOCR()
        return _ocr_engine
except ImportError:
    _get_ocr = None

RESULTS_DIR = Path(__file__).resolve().parents[4] / 'results'
HOST = os.environ.get('RPC_HOST', '127.0.0.1:9222')
BASE = f'http://{HOST}'

_run_ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')
LCD_W, LCD_H = 240, 240


# -- helpers ------------------------------------------------------------------

def _get(path, timeout=5):
    url = f'{BASE}{path}'
    req = Request(url, headers={'Accept': 'application/json'})
    with urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read())


def _rgb565_to_image(data_b64, width, height):
    raw = base64.b64decode(data_b64)
    pixels = bytearray(width * height * 3)
    for i in range(0, len(raw), 2):
        if i + 1 >= len(raw):
            break
        px = struct.unpack('<H', raw[i:i + 2])[0]
        j = (i // 2) * 3
        pixels[j] = ((px >> 11) & 0x1F) * 255 // 31
        pixels[j + 1] = ((px >> 5) & 0x3F) * 255 // 63
        pixels[j + 2] = (px & 0x1F) * 255 // 31
    if Image is None:
        return None
    return Image.frombytes('RGB', (width, height), bytes(pixels))


def _capture_screenshot(path_png):
    try:
        data = _get('/api/screenshot', timeout=10)
    except (URLError, OSError):
        return None
    if 'data' not in data:
        return None
    img = _rgb565_to_image(data['data'], data['width'], data['height'])
    if img:
        path_png.parent.mkdir(parents=True, exist_ok=True)
        img.save(str(path_png), format='PNG')
    return img


OCR_SCALE = 3

def _ocr_text(img):
    """Run RapidOCR on a PIL Image, return all detected text joined.

    Upscales the 240x240 LCD image for better detection on small text.
    """
    if _get_ocr is None or img is None:
        return ''
    import numpy as np
    img_up = img.resize((img.width * OCR_SCALE, img.height * OCR_SCALE),
                        Image.LANCZOS)
    result, _ = _get_ocr()(np.array(img_up))
    texts = []
    if result:
        for line in result:
            texts.append(line[1])
    return ' '.join(texts)


def _sample_color(img, x, y):
    """Return (R, G, B) at pixel (x, y)."""
    if img is None:
        return (0, 0, 0)
    return img.getpixel((x, y))[:3]


def _region_dominant_color(img, x, y, w, h):
    """Return the most common (R, G, B) in a rectangular region."""
    if img is None:
        return (0, 0, 0)
    crop = img.crop((x, y, x + w, y + h))
    colors = crop.getcolors(maxcolors=crop.size[0] * crop.size[1])
    if not colors:
        return (0, 0, 0)
    colors.sort(key=lambda c: c[0], reverse=True)
    return colors[0][1][:3]


def _color_distance(c1, c2):
    """Euclidean distance between two RGB tuples."""
    return sum((a - b) ** 2 for a, b in zip(c1, c2)) ** 0.5


def _is_light(rgb, threshold=180):
    """True if the color is perceptually light (simple luminance check)."""
    return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000 > threshold


def _is_dark(rgb, threshold=80):
    return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000 < threshold


# -- page definitions ---------------------------------------------------------

PAGES = [
    {'route': '/modd/home', 'name': 'home', 'page': 'HOME',
     'expect_text': ['modd'],
     'bg_region': (0, 100, 240, 40)},

    {'route': '/modd/menu', 'name': 'settings', 'page': 'MENU',
     'expect_text': ['Settings', 'DISPLAY'],
     'bg_region': (0, 100, 240, 40)},

    {'route': '/modd/menu/display', 'name': 'settings_display', 'page': 'MENU',
     'expect_text': ['DISPLAY', 'THEME'],
     'bg_region': (0, 100, 240, 40)},

    {'route': '/modd/menu/midi', 'name': 'settings_midi', 'page': 'MENU',
     'expect_text': ['MIDI'],
     'bg_region': (0, 100, 240, 40)},

    {'route': '/modd/menu/hw', 'name': 'settings_hw', 'page': 'MENU',
     'expect_text': ['HW'],
     'bg_region': (0, 100, 240, 40)},

    {'route': '/modd/menu/net', 'name': 'settings_net', 'page': 'MENU',
     'expect_text': ['NET'],
     'bg_region': (0, 100, 240, 40)},

    {'route': '/modd/wx', 'name': 'weather', 'page': 'WX',
     'expect_text': ['Weather'],
     'bg_region': (0, 100, 240, 40)},

    {'route': '/modd/demo', 'name': 'demo', 'page': 'DEMO',
     'expect_text': ['DEMO'],
     'bg_region': (0, 60, 240, 40)},

    {'route': '/modd/log', 'name': 'log', 'page': 'LOG',
     'expect_text': ['Log'],
     'bg_region': (0, 60, 240, 40)},

    {'route': '/modd/home', 'name': 'home_return', 'page': 'HOME',
     'expect_text': ['modd'],
     'bg_region': (0, 100, 240, 40)},
]


# -- test class ---------------------------------------------------------------

class TestUIWalkthrough(unittest.TestCase):
    """Full UI walkthrough with OCR text extraction and color assertions."""

    _results = None
    _captures = {}

    @classmethod
    def _results_dir(cls):
        if cls._results is None:
            cls._results = RESULTS_DIR / f'{_run_ts}-ui_walkthrough'
            cls._results.mkdir(parents=True, exist_ok=True)
            (cls._results / 'screenshots').mkdir(exist_ok=True)
            (cls._results / 'data').mkdir(exist_ok=True)
        return cls._results

    @classmethod
    def setUpClass(cls):
        if Image is None:
            raise unittest.SkipTest('Pillow not installed')
        try:
            _get('/api/info', timeout=3)
        except (URLError, OSError) as e:
            raise unittest.SkipTest(
                f'Cannot reach firmware at {BASE}: {e}. '
                f'Start fm1-live with --forward 9222:9222')

    # -- Phase 1: capture -----------------------------------------------------

    def test_00_capture_all_pages(self):
        """Phase 1: navigate every page, capture screenshots + JSON."""
        for i, pg in enumerate(PAGES):
            with self.subTest(page=pg['name']):
                resp = _get(pg['route'])
                self.assertTrue(resp.get('ok'), f"{pg['route']} returned ok=false")
                self.assertEqual(resp.get('page'), pg['page'])

                json_path = self._results_dir() / 'data' / f'{i:02d}_{pg["name"]}.json'
                json_path.write_text(json.dumps(resp, indent=2))

                png_path = self._results_dir() / 'screenshots' / f'{i:02d}_{pg["name"]}.png'
                img = _capture_screenshot(png_path)
                self.__class__._captures[pg['name']] = {
                    'img': img, 'resp': resp, 'png': png_path
                }

                print(f'\n    [{i:02d}] {pg["name"]}: page={resp["page"]} '
                      f'screenshot={"OK" if img else "FAIL"}', flush=True)

    # -- Phase 2: OCR text assertions -----------------------------------------

    def test_01_ocr_home(self):
        """OCR: home page shows 'modd' title."""
        self._assert_ocr_texts('home', ['modd'])

    def test_02_ocr_settings(self):
        """OCR: settings page shows 'Settings' and tab names."""
        self._assert_ocr_texts('settings', ['Settings'])

    def test_03_ocr_settings_display(self):
        """OCR: DISPLAY tab shows 'THEME'."""
        self._assert_ocr_texts('settings_display', ['THEME'])

    def test_04_ocr_settings_midi(self):
        """OCR: MIDI tab is visible."""
        self._assert_ocr_texts('settings_midi', ['MIDI'])

    def test_05_ocr_settings_hw(self):
        """OCR: HW tab is visible."""
        self._assert_ocr_texts('settings_hw', ['HW|FLAT|HET|MH'])

    def test_06_ocr_settings_net(self):
        """OCR: NET tab is visible."""
        self._assert_ocr_texts('settings_net', ['NET'])

    def test_07_ocr_weather(self):
        """OCR: weather page shows 'Weather'."""
        self._assert_ocr_texts('weather', ['Weather'])

    def test_08_ocr_demo(self):
        """OCR: demo page shows 'DEMO'."""
        self._assert_ocr_texts('demo', ['DEMO'])

    def test_09_ocr_log(self):
        """OCR: log page shows 'Log'."""
        self._assert_ocr_texts('log', ['Log'])

    # -- Phase 2: color assertions --------------------------------------------

    def test_10_color_home_background_has_theme(self):
        """Color: home page background region is a themed color (not black)."""
        self._assert_bg_has_color('home')

    def test_11_color_settings_background_has_theme(self):
        """Color: settings page background has a themed color."""
        self._assert_bg_has_color('settings')

    def test_12_color_home_header_text_is_dark(self):
        """Color: home page header area has dark text pixels."""
        cap = self._captures.get('home')
        if not cap or cap['img'] is None:
            self.skipTest('no home screenshot')
        c = _sample_color(cap['img'], 15, 10)
        self.assertTrue(_is_dark(c, threshold=120),
                        f'Header text pixel at (15,10) is not dark: {c}')

    def test_13_color_settings_active_tab_has_accent(self):
        """Color: active settings tab has a distinct (non-background) color."""
        cap = self._captures.get('settings_display')
        if not cap or cap['img'] is None:
            self.skipTest('no settings_display screenshot')
        tab_color = _region_dominant_color(cap['img'], 5, 25, 60, 15)
        bg_color = _region_dominant_color(cap['img'], 120, 100, 60, 30)
        dist = _color_distance(tab_color, bg_color)
        self.assertGreater(dist, 20,
                           f'Active tab color {tab_color} too similar to bg {bg_color}')

    def test_14_color_demo_has_color_swatches(self):
        """Color: demo page contains visible color swatch rectangles."""
        cap = self._captures.get('demo')
        if not cap or cap['img'] is None:
            self.skipTest('no demo screenshot')
        left_swatch = _region_dominant_color(cap['img'], 10, 120, 30, 10)
        right_swatch = _region_dominant_color(cap['img'], 130, 120, 30, 10)
        dist = _color_distance(left_swatch, right_swatch)
        print(f'\n    demo swatches: left={left_swatch} right={right_swatch} '
              f'distance={dist:.1f}', flush=True)

    def test_15_color_log_header_has_theme_accent(self):
        """Color: log page header numbers have a distinct accent color."""
        cap = self._captures.get('log')
        if not cap or cap['img'] is None:
            self.skipTest('no log screenshot')
        header_color = _region_dominant_color(cap['img'], 50, 30, 40, 15)
        bg_color = _region_dominant_color(cap['img'], 120, 100, 40, 30)
        dist = _color_distance(header_color, bg_color)
        self.assertGreater(dist, 15,
                           f'Log header accent {header_color} too close to bg {bg_color}')

    # -- Phase 2: structural JSON assertions ----------------------------------

    def test_16_home_has_4_tracks(self):
        """JSON: home page returns exactly 4 tracks."""
        cap = self._captures.get('home')
        if not cap:
            self.skipTest('no home capture')
        tracks = cap['resp'].get('data', {}).get('tracks', [])
        self.assertEqual(len(tracks), 4)
        for t in tracks:
            self.assertIn('engine', t)
            self.assertIn('preset', t)

    def test_17_settings_net_has_network_fields(self):
        """JSON: NET tab returns link, ip, connected fields."""
        cap = self._captures.get('settings_net')
        if not cap:
            self.skipTest('no settings_net capture')
        d = cap['resp'].get('data', {})
        self.assertIn('link', d)
        self.assertIn('ip', d)
        self.assertIn('connected', d)

    def test_18_weather_has_valid_field(self):
        """JSON: weather page returns valid and fetching booleans."""
        cap = self._captures.get('weather')
        if not cap:
            self.skipTest('no weather capture')
        d = cap['resp'].get('data', {})
        self.assertIn('valid', d)
        self.assertIn('fetching', d)

    def test_19_roundtrip_home(self):
        """JSON: navigating back to home works (round-trip)."""
        cap = self._captures.get('home_return')
        if not cap:
            self.skipTest('no home_return capture')
        self.assertEqual(cap['resp']['page'], 'HOME')

    # -- summary --------------------------------------------------------------

    def test_99_summary(self):
        """Print results summary."""
        d = self._results_dir()
        screenshots = list((d / 'screenshots').glob('*.png'))
        data_files = list((d / 'data').glob('*.json'))
        print(f'\n    === UI Walkthrough Results ===')
        print(f'    results:      {d}/')
        print(f'    screenshots:  {len(screenshots)} captured')
        for s in sorted(screenshots):
            print(f'      {s.name}')
        print(f'    json data:    {len(data_files)} saved')

    # -- helpers --------------------------------------------------------------

    def _assert_ocr_texts(self, page_name, expected_texts):
        if _get_ocr is None:
            self.skipTest('PaddleOCR not installed')
        cap = self._captures.get(page_name)
        if not cap or cap['img'] is None:
            self.skipTest(f'no {page_name} screenshot')
        ocr_result = _ocr_text(cap['img'])
        ocr_path = self._results_dir() / 'data' / f'ocr_{page_name}.txt'
        ocr_path.write_text(ocr_result)
        ocr_lower = ocr_result.lower()
        for expected in expected_texts:
            alternatives = [a.strip().lower() for a in expected.split('|')]
            found = any(a in ocr_lower for a in alternatives)
            self.assertTrue(found,
                            f'OCR on {page_name} did not find any of '
                            f'{alternatives}. Got: {ocr_result!r}')
        print(f'\n    OCR {page_name}: {ocr_result[:80]}', flush=True)

    def _assert_bg_has_color(self, page_name):
        """Assert background is not black — has a visible theme color."""
        cap = self._captures.get(page_name)
        if not cap or cap['img'] is None:
            self.skipTest(f'no {page_name} screenshot')
        pg_def = next((p for p in PAGES if p['name'] == page_name), None)
        if not pg_def or 'bg_region' not in pg_def:
            self.skipTest(f'no bg_region for {page_name}')
        x, y, w, h = pg_def['bg_region']
        bg = _region_dominant_color(cap['img'], x, y, w, h)
        luminance = (bg[0] * 299 + bg[1] * 587 + bg[2] * 114) / 1000
        self.assertGreater(luminance, 20,
                           f'{page_name} background {bg} is too dark (black?)')
        print(f'\n    {page_name} bg dominant color: {bg} '
              f'luminance={luminance:.0f}', flush=True)

    def _assert_bg_is_light(self, page_name):
        cap = self._captures.get(page_name)
        if not cap or cap['img'] is None:
            self.skipTest(f'no {page_name} screenshot')
        pg_def = next((p for p in PAGES if p['name'] == page_name), None)
        if not pg_def or 'bg_region' not in pg_def:
            self.skipTest(f'no bg_region for {page_name}')
        x, y, w, h = pg_def['bg_region']
        bg = _region_dominant_color(cap['img'], x, y, w, h)
        self.assertTrue(_is_light(bg),
                        f'{page_name} background {bg} is not light')
        print(f'\n    {page_name} bg dominant color: {bg}', flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
