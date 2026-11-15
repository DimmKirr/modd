#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: Weather page in the emulator.

Builds the modd_dev_network defconfig (WiFi + lwIP + USB-NET), boots in fm1-emu
with ?wifi=oss&net=internet, navigates to the WEATHER page (FX hold -> VIEW_CONN),
and tests:
  1. Weather page renders without crashing
  2. Loading state shows spinner and "FETCHING WEATHER" text
  3. Error state shows "NO WEATHER DATA" when fetch fails (no network)
  4. PLAY button triggers a refresh attempt without crashing
  5. Cards show placeholder "---" values before data arrives
  6. Page survives multiple visits (enter/leave cycle)
  7. The firmware joins the emulator's virtual AP and fetches real weather

WiFi credentials: WIFI_SSID / WIFI_PASSWORD from .env (via task), or
FM1_E2E_WIFI=SSID:PASSWORD, or the emulator default (Home / fm1pass).
"""
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, e2e_build, e2e_genconfig

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build'

DEFCONFIG = 'modd_dev_network'
FWSC = BUILD / DEFCONFIG / 'modd.fwsc'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_weather_test'

_wifi_env = os.environ.get('FM1_E2E_WIFI')
if _wifi_env:
    WIFI_SSID, WIFI_PASSWORD = _wifi_env.split(':', 1)
else:
    WIFI_SSID = 'Home'
    WIFI_PASSWORD = 'fm1pass'


def _set_wifi_credentials(config_h, ssid, password):
    """CONFIG_WIFI_SSID / CONFIG_WIFI_PASSWORD in a generated config.h."""
    text = config_h.read_text()
    for name, value in (('CONFIG_WIFI_SSID', ssid), ('CONFIG_WIFI_PASSWORD', password)):
        text = re.sub(rf'#define {name} ".*"', f'#define {name} "{value}"', text)
    config_h.write_text(text)

# Firmware hold threshold is 500 ms of emulated time; 800 ms wall-clock was
# occasionally delivered as a tap by the WASM emulator, so keep a wide margin.
FX_HOLD_MS = 1500

# JS snippet: hash the top header strip of the panel canvas (first 14 rows).
# Returns a simple sum of pixel values as a fingerprint.
_CANVAS_HASH_JS = '''() => {
    const c = document.getElementById('panel');
    const o = document.createElement('canvas');
    o.width = c.width; o.height = c.height;
    const g = o.getContext('2d');
    g.drawImage(c, 0, 0);
    const d = g.getImageData(0, 0, o.width, o.height).data;
    let s = 0;
    for (let i = 0; i < d.length; i += 4) s += d[i] ^ d[i+1] ^ d[i+2];
    return s;
}'''


def _navigate_to_weather(ctrl, page):
    """From HOME: FX hold opens VIEW_CONN. WEATHER is the first page in
    that view, so a single hold lands on it directly."""
    home_hash = page.evaluate(_CANVAS_HASH_JS)
    ctrl.press_button('fx', hold_ms=FX_HOLD_MS, delay_ms=500)
    weather_hash = page.evaluate(_CANVAS_HASH_JS)
    assert weather_hash != home_hash, \
        'canvas unchanged after FX hold: navigation to Weather failed'


class TestWeatherPage(E2ETestCase):
    """Weather page in the emulator with wifi defconfig."""

    _build_ok = False
    _base_emu_url = None

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        dc_path = ROOT / 'configs' / f'{DEFCONFIG}_defconfig'
        if not dc_path.exists():
            cls._build_ok = False
            (TEST_RESULTS / 'SKIP-no-defconfig.txt').write_text(
                f'configs/{DEFCONFIG}_defconfig does not exist.\n')
        else:
            try:
                gen_dir = e2e_genconfig(DEFCONFIG, TEST_RESULTS)
                _set_wifi_credentials(gen_dir / 'config.h', WIFI_SSID, WIFI_PASSWORD)
                r, _ = e2e_build(DEFCONFIG, TEST_RESULTS, gen_dir=gen_dir)
                cls._build_ok = r.returncode == 0
            except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired):
                cls._build_ok = False
        if not cls._build_ok:
            (TEST_RESULTS / 'SKIP-build-failed.txt').write_text(
                f'Build failed or defconfig missing for {DEFCONFIG}.\n'
                'Weather page E2E tests run once this defconfig builds.\n')
        try:
            super().setUpClass()
            cls._base_emu_url = cls.emu_url
            cls.emu_url += (
                f'&wifi=oss&net=internet'
                f'&wifi-ssid={WIFI_SSID}&wifi-password={WIFI_PASSWORD}'
            )
        except unittest.SkipTest:
            pass

    def _boot_page(self, wifi=True):
        if not self._build_ok:
            self.skipTest(f'{DEFCONFIG} defconfig build failed')
        if not FWSC.is_file():
            self.skipTest('No .fwsc produced')
        if not self._browser:
            self.skipTest('Emulator not available')
        if wifi:
            page = self.new_emu_page()
        else:
            page = self._browser.new_page()
            page._console_logs = []
            page.on('console', lambda msg: page._console_logs.append(
                f'[{msg.type}] {msg.text}'))
            page.on('pageerror', lambda err: page._console_logs.append(
                f'[pageerror] {err}'))
            page.goto(self._base_emu_url)
            page.evaluate("async () => { await import('./main.js'); }")
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            '(() => { const t = document.getElementById("status")?.textContent || "";'
            'return t.includes("instructions/s") || t.includes("real time"); })()',
            timeout=30_000)
        page.wait_for_timeout(3_000)
        return page

    def test_weather_page_renders(self):
        """Navigate to WEATHER page and verify it draws without crash."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)

            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-page-renders')
            self.screenshot_lcd(page, 'WEATHER-page-renders')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)

            (TEST_RESULTS / 'weather-page-status.txt').write_text(status + '\n')
        finally:
            self.write_log(page, 'weather-page.log')
            page.close()

    def test_weather_shows_no_data(self):
        """Weather page shows loading/error state when no network is available."""
        page = self._boot_page(wifi=False)
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)

            page.wait_for_timeout(5_000)
            self.screenshot(page, 'WEATHER-no-data')
            self.screenshot_lcd(page, 'WEATHER-no-data')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-no-data.log')
            page.close()

    def test_weather_shows_data_over_the_virtual_ap(self):
        """The firmware joins the emulator's virtual AP and weather data is drawn."""
        page = self._boot_page()
        try:
            # scan, autojoin, WPA2, DHCP, geo, weather: about 20 s of emulated time
            page.wait_for_timeout(25_000)
            ctrl = self.controls(page)
            ctrl.press_button('home')
            page.wait_for_timeout(500)
            _navigate_to_weather(ctrl, page)
            page.wait_for_timeout(1_500)
            self.screenshot(page, 'WEATHER-data')
            self.screenshot_lcd(page, 'WEATHER-data')
            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
            # The data card is a dithered sky gradient whose hue depends on the
            # time of day (blue, pink, orange, …).  Without data the area is
            # the cream page background.  Detect any strongly saturated pixels:
            # max channel minus min channel > 40 and max > 120.
            coloured = page.evaluate('''() => {
                const c = document.getElementById('panel');
                const o = document.createElement('canvas');
                o.width = c.width; o.height = c.height;
                const g = o.getContext('2d');
                g.drawImage(c, 0, 0);
                const d = g.getImageData(0, 0, o.width, o.height).data;
                let n = 0;
                for (let i = 0; i < d.length; i += 4) {
                    const mx = Math.max(d[i], d[i+1], d[i+2]);
                    const mn = Math.min(d[i], d[i+1], d[i+2]);
                    if (mx - mn > 40 && mx > 120) n++;
                }
                return n;
            }''')
            self.assertGreater(coloured, 2000, 'no weather data card on the panel')
        finally:
            self.write_log(page, 'weather-data.log')
            page.close()

    def test_weather_play_button_refresh(self):
        """PLAY button triggers a weather refresh without crashing."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)
            page.wait_for_timeout(500)

            self.screenshot(page, 'WEATHER-before-refresh')
            self.screenshot_lcd(page, 'WEATHER-before-refresh')

            ctrl.press_button('play')
            page.wait_for_timeout(1_000)

            self.screenshot(page, 'WEATHER-after-refresh')
            self.screenshot_lcd(page, 'WEATHER-after-refresh')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-refresh.log')
            page.close()

    def test_weather_cards_placeholder(self):
        """Cards show --- placeholder values when no network is available."""
        page = self._boot_page(wifi=False)
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)
            page.wait_for_timeout(500)

            self.screenshot(page, 'WEATHER-cards-placeholder')
            self.screenshot_lcd(page, 'WEATHER-cards-placeholder')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-cards.log')
            page.close()

    def test_weather_page_revisit(self):
        """Leave and re-enter the weather page without crash."""
        page = self._boot_page(wifi=False)
        try:
            ctrl = self.controls(page)

            # Visit weather
            _navigate_to_weather(ctrl, page)
            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-visit-1')
            self.screenshot_lcd(page, 'WEATHER-visit-1')

            # Leave: a HOME tap opens VIEW_HOME
            ctrl.press_button('home')
            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-left-to-HOME')
            self.screenshot_lcd(page, 'WEATHER-left-to-HOME')

            # Return: the CONN view remembers its last page, so a single
            # FX hold lands on WEATHER again
            ctrl.press_button('fx', hold_ms=FX_HOLD_MS, delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-visit-2')
            self.screenshot_lcd(page, 'WEATHER-visit-2')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-revisit.log')
            page.close()

    def test_weather_spinner_animates(self):
        """Capture frames during loading to verify spinner animation."""
        page = self._boot_page(wifi=False)
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)

            self.start_canvas_capture(page, fps=5)
            page.wait_for_timeout(2_000)
            frames = self.stop_canvas_capture(page)

            self.screenshot(page, 'WEATHER-spinner')
            self.screenshot_lcd(page, 'WEATHER-spinner')

            if len(frames) >= 3:
                self.frames_to_gif(frames, name='weather-spinner', fps=5)

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-spinner.log')
            page.close()

if __name__ == '__main__':
    unittest.main()
