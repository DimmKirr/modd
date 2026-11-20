#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""UI walkthrough of the LVGL theme: every page, screenshots, OCR text, colours, page data.

Two phases:
  Phase 1: go to every page over the REST API (/modd/<page>), save the screenshot
           (screenshots/NN_<name>.png) and the page's /api/page reply (data/NN_<name>.json).
  Phase 2: RapidOCR on the screenshots (data/ocr_<name>.txt), colour checks of the LVGL
           theme in the default palette (CLAY: cream background, ink text, the selected
           track's colour), structure of the page data.

The LVGL theme has the modd theme's pages, settings tabs included. It builds a page in its UI task, so phase 1 waits until /api/page names the
page (and the tab, for /modd/menu/<tab>); WX is captured once it holds a weather report.
OCR and colour helpers: tests/ui/lcd_checks.py.

Boots fm1-live with modd_emu (tests/api_fixtures.py). Results:
tests/results/<datetimeISO>-lvgl_ui_walkthrough_test/.

Usage:
    task modd:test:modd_emu:api
    uv run --group ui-test python -m pytest tests/ui/themes/lvgl/api/lvgl_ui_walkthrough_test.py -v
"""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'ui'))
from api_fixtures import ApiTestCase
from lcd_checks import Image, _get_ocr, _ocr_text, _region_dominant_color, _color_distance


def _rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


# CLAY, the default palette (build/<defconfig>/gen/lvgl/ui_palettes.h): BG, SURF, TEXT, tracks
CLAY_BG, CLAY_SURF, CLAY_TEXT = _rgb565(0xef3b), _rgb565(0xdeda), _rgb565(0x2124)
CLAY_TRK = [_rgb565(c) for c in (0x9143, 0x69e0, 0x02a8, 0x41b4)]


def _near_pixels(img, colour, x, y, w, h, tol=40):
    crop = img.convert('RGB').crop((x, y, x + w, y + h))
    return sum(1 for p in crop.getdata() if _color_distance(p, colour) < tol)


# -- page definitions ---------------------------------------------------------

PAGES = [
    {'route': '/modd/home',         'name': 'home',             'page': 'HOME',
     'expect_text': ['modd', 'BPM']},
    {'route': '/modd/menu',         'name': 'settings',         'page': 'MENU', 'tab': 'DISPLAY',
     'expect_text': ['Settings', 'DISPLAY']},
    {'route': '/modd/menu/display', 'name': 'settings_display', 'page': 'MENU', 'tab': 'DISPLAY',
     'expect_text': ['THEME', 'CLAY', 'LIGHTS']},
    {'route': '/modd/menu/midi',    'name': 'settings_midi',    'page': 'MENU', 'tab': 'MIDI',
     'expect_text': ['MIDI CH']},
    {'route': '/modd/menu/hw',      'name': 'settings_hw',      'page': 'MENU', 'tab': 'HW',
     'expect_text': ['HW', 'ETHERNET']},
    {'route': '/modd/menu/net',     'name': 'settings_net',     'page': 'MENU', 'tab': 'NET',
     'expect_text': ['NET', 'GATEWAY']},
    {'route': '/modd/wx',           'name': 'weather',          'page': 'WX',
     'expect_text': ['Weather', 'TEMP', 'New York']},
    {'route': '/modd/demo',         'name': 'demo',             'page': 'DEMO',
     'expect_text': ['DEMO', 'CARDS']},
    {'route': '/modd/files',        'name': 'files',            'page': 'FILES',
     'expect_text': ['Files', 'USB Disk']},
    {'route': '/modd/disk',         'name': 'disk',             'page': 'DISK',
     'expect_text': ['Disk', 'Status']},
    {'route': '/modd/log',          'name': 'log',              'page': 'LOG',
     'expect_text': ['Log', 'BUF']},
    {'route': '/modd/home',         'name': 'home_return',      'page': 'HOME',
     'expect_text': ['modd']},
]


# -- test class ---------------------------------------------------------------

class LvglUIWalkthrough(ApiTestCase):
    """Full UI walkthrough of the LVGL theme with OCR text and colour assertions."""

    _captures = {}

    @classmethod
    def _results_dir(cls):
        d = cls.results_dir()
        (d / 'screenshots').mkdir(parents=True, exist_ok=True)
        (d / 'data').mkdir(exist_ok=True)
        return d

    @classmethod
    def setUpClass(cls):
        if Image is None:
            raise unittest.SkipTest('Pillow not installed')
        super().setUpClass()

    # -- Phase 1: capture -----------------------------------------------------

    def test_00_capture_all_pages(self):
        """Phase 1: go to every page, capture screenshots + JSON."""
        api = self.api
        for i, pg in enumerate(PAGES):
            with self.subTest(page=pg['name']):
                resp = api.get(pg['route'])
                self.assertTrue(resp.get('ok'), f"{pg['route']} returned ok=false")
                self.assertEqual(resp.get('page'), pg['page'])
                # built by the UI task on its next frame; WX once a report has arrived
                if pg['page'] == 'WX':
                    page = api.wait_for(lambda: (p := api.page())['page'] == 'WX' and p['data']['valid'] and p,
                                        timeout=60, poll=2.0, what='WX with a weather report')
                else:
                    tab = pg.get('tab')
                    page = api.wait_for(lambda: (p := api.page())['page'] == pg['page'] and
                                        (not tab or p['data'].get('tab') == tab) and p,
                                        timeout=15, poll=0.5, what=f"page {pg['page']} {tab or ''}")
                    api.wait_ms(300)            # the frame after the data: drawn

                json_path = self._results_dir() / 'data' / f'{i:02d}_{pg["name"]}.json'
                json_path.write_text(json.dumps({'goto': resp, 'page': page}, indent=2))

                png_path = self._results_dir() / 'screenshots' / f'{i:02d}_{pg["name"]}.png'
                img = api.screenshot()
                img.save(png_path)
                self.__class__._captures[pg['name']] = {'img': img, 'resp': page, 'png': png_path}
                print(f'\n    [{i:02d}] {pg["name"]}: page={page["page"]} screenshot=OK', flush=True)
        self.assertAlive()

    # -- Phase 2: OCR text assertions -----------------------------------------

    def test_01_ocr_home(self):
        """OCR: HOME shows the modd title and the tempo."""
        self._assert_ocr_page('home')

    def test_02_ocr_settings(self):
        """OCR: Settings opens on DISPLAY."""
        self._assert_ocr_page('settings')

    def test_03_ocr_settings_tabs(self):
        """OCR: each settings tab shows its rows (THEME and LIGHTS, MIDI CH, ETHERNET, GATEWAY)."""
        for name in ('settings_display', 'settings_midi', 'settings_hw', 'settings_net'):
            with self.subTest(tab=name):
                self._assert_ocr_page(name)

    def test_04_ocr_weather(self):
        """OCR: WX shows its title, the cards and the city."""
        self._assert_ocr_page('weather')

    def test_05_ocr_demo(self):
        """OCR: DEMO opens on CARDS."""
        self._assert_ocr_page('demo')

    def test_06_ocr_files(self):
        """OCR: FILES shows its title and the disk state (no disk in fm1-live)."""
        self._assert_ocr_page('files')

    def test_07_ocr_disk(self):
        """OCR: DISK shows its title and the Status row."""
        self._assert_ocr_page('disk')

    def test_08_ocr_log(self):
        """OCR: LOG shows its title and its cards."""
        self._assert_ocr_page('log')

    # -- Phase 2: colour assertions -------------------------------------------

    def test_10_color_palette_background_and_text(self):
        """Colour: CLAY's cream background under the header, ink title text, SURF footer."""
        img = self._img('home')
        bar = _region_dominant_color(img, 0, 0, 240, 22)
        self.assertLess(_color_distance(bar, CLAY_BG), 30, f'header {bar} is not CLAY BG {CLAY_BG}')
        self.assertGreater(_near_pixels(img, CLAY_TEXT, 0, 0, 120, 24), 30, 'no ink title text in the header')
        foot = _region_dominant_color(img, 0, 214, 240, 26)
        self.assertLess(_color_distance(foot, CLAY_SURF), 30, f'footer {foot} is not CLAY SURF {CLAY_SURF}')

    def test_11_color_home_selected_track_colour(self):
        """Colour: the selected track is drawn in its colour: card value and gauge."""
        sel = self._data('home')['selected_track']
        n = _near_pixels(self._img('home'), CLAY_TRK[sel], 0, 26, 240, 50)
        self.assertGreater(n, 40, f'no track-{sel + 1} colour in the cards ({n} px)')

    def test_12_color_settings_selected_row_raised(self):
        """Colour: the selected settings row is raised in SURF."""
        n = _near_pixels(self._img('settings_midi'), CLAY_SURF, 0, 50, 240, 22, tol=12)
        self.assertGreater(n, 1500, f'the selected row is not raised ({n} px)')

    def test_14_color_weather_sky_card(self):
        """Colour: with a report, WX draws the sky in the track's colour."""
        n = self.coloured_pixels(self._img('weather'))
        self.assertGreater(n, 2000, f'no coloured sky on WX ({n} px)')

    def test_16_color_pages_differ(self):
        """Colour: every page draws a different picture."""
        names = ['home', 'settings_midi', 'settings_hw', 'settings_net', 'weather', 'demo',
                 'files', 'disk', 'log']
        imgs = [self._img(n).convert('RGB').tobytes() for n in names]
        self.assertEqual(len(set(imgs)), len(names), 'two pages drew the same picture')

    # -- Phase 2: structural JSON assertions ----------------------------------

    def test_20_home_has_4_tracks(self):
        """JSON: HOME returns exactly 4 tracks and the palette."""
        d = self._data('home')
        tracks = d.get('tracks', [])
        self.assertEqual(len(tracks), 4)
        for t in tracks:
            self.assertIn('engine', t)
            self.assertIn('preset', t)
        self.assertEqual(d.get('palette_name'), 'CLAY')

    def test_21_settings_net_has_network_fields(self):
        """JSON: the NET tab returns link, ip, connected."""
        d = self._data('settings_net')
        for k in ('tab', 'link', 'ip', 'ip_str', 'connected'):
            self.assertIn(k, d)

    def test_22_weather_has_report(self):
        """JSON: WX returns a valid report with the configured city."""
        d = self._data('weather')
        self.assertTrue(d['valid'])
        self.assertEqual(d['city'], 'New York')
        for k in ('temp_c', 'humidity', 'wind_kmh', 'wmo_code', 'condition', 'fetching'):
            self.assertIn(k, d)

    def test_23_settings_tabs_have_items(self):
        """JSON: the MIDI and HW tabs return their items."""
        for name, item in (('settings_midi', 'MIDI CH'), ('settings_hw', 'DISK')):
            with self.subTest(tab=name):
                items = self._data(name).get('items', [])
                self.assertIn(item, [i['name'] for i in items])
                for i in items:
                    self.assertIn('value', i)

    def test_24_roundtrip_home(self):
        """JSON: going back to HOME works (round trip)."""
        cap = self._captures.get('home_return')
        if not cap:
            self.skipTest('no home_return capture')
        self.assertEqual(cap['resp']['page'], 'HOME')

    # -- summary --------------------------------------------------------------

    def test_99_summary(self):
        """Print results summary."""
        d = self._results_dir()
        screenshots = sorted((d / 'screenshots').glob('*.png'))
        data_files = list((d / 'data').glob('*'))
        print('\n    === LVGL UI Walkthrough Results ===')
        print(f'    results:      {d}/')
        print(f'    screenshots:  {len(screenshots)} captured')
        for s in screenshots:
            print(f'      {s.name}')
        print(f'    data:         {len(data_files)} files (page JSON, OCR text)')

    # -- helpers --------------------------------------------------------------

    def _img(self, name):
        cap = self._captures.get(name)
        if not cap or cap['img'] is None:
            self.skipTest(f'no {name} screenshot')
        return cap['img']

    def _data(self, name):
        cap = self._captures.get(name)
        if not cap:
            self.skipTest(f'no {name} capture')
        return cap['resp'].get('data', {})

    def _assert_ocr_page(self, name):
        if _get_ocr is None:
            self.skipTest('rapidocr not installed (uv sync --group ui-test)')
        pg = next(p for p in PAGES if p['name'] == name)
        text = _ocr_text(self._img(name))
        (self._results_dir() / 'data' / f'ocr_{name}.txt').write_text(text)
        low = text.lower()
        for expected in pg['expect_text']:
            alternatives = [a.strip().lower() for a in expected.split('|')]
            self.assertTrue(any(a in low for a in alternatives),
                            f'OCR on {name} did not find any of {alternatives}. Got: {text!r}')
        print(f'\n    OCR {name}: {text[:80]}', flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
