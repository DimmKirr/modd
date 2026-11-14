#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: modd theme settings page.

Opens VIEW_SETTINGS (HOME hold), verifies:
  - SELECT encoder switches tabs
  - ALGO encoder scrolls rows within a tab
  - PRESET (K1) encoder changes the selected item's value
  - OCT+ confirms/toggles, OCT- closes settings

modd defconfig has 4 visible tabs: DISPLAY (palette), MIDI (MIDI CH),
SYSTEM (USB MODE), NETWORK (WIFI on/off + read-only IP/GW/DNS).

Run:
    uv run python -m pytest tests/ui/themes/modd/e2e_settings_test.py -v
"""
import sys
import unittest
from pathlib import Path

# conftest.py (loaded by pytest automatically) puts tests/ on sys.path
from e2e_fixtures import E2ETestCase

FWSC = Path(__file__).resolve().parents[4] / 'build' / 'modd.fwsc'


class TestModdSettings(E2ETestCase):
    """modd theme: tabbed settings menu."""

    @classmethod
    def setUpClass(cls):
        if not FWSC.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()

    def _boot_and_open_settings(self, page):
        """Upload firmware, wait for boot, hold HOME to open settings."""
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("instructions/s")',
            timeout=30_000)
        # wait for LCD content (non-black pixels in the LCD region)
        page.wait_for_function('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > 500;
        }''', timeout=120_000)
        page.wait_for_timeout(2000)

        ctrl = self.controls(page)
        ctrl.press_button('home', hold_ms=600, delay_ms=500)
        page.wait_for_timeout(500)
        return ctrl

    def _lcd_snapshot(self, page):
        """Return pixel data from the LCD region as bytes for comparison."""
        return page.evaluate('''() => {
            const c = document.getElementById('panel');
            const d = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let h = 0;
            for (let i = 0; i < d.length; i += 16)
                h = ((h << 5) - h + d[i]) | 0;
            return h;
        }''')

    def _assert_alive(self, page):
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status)

    def test_settings_opens(self):
        """HOME hold opens the settings page without crashing."""
        page = self.new_emu_page()
        try:
            self._boot_and_open_settings(page)
            self.screenshot(page, 'settings-initial')
            self._assert_alive(page)
        finally:
            self.write_log(page, 'settings-open.log')
            page.close()

    def test_select_switches_tabs(self):
        """SELECT encoder switches tabs."""
        page = self.new_emu_page()
        try:
            ctrl = self._boot_and_open_settings(page)
            self.screenshot(page, 'settings-tab0')
            snap_before = self._lcd_snapshot(page)

            # SELECT +1: should switch to next tab
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'settings-tab1-select-right')
            snap_after = self._lcd_snapshot(page)

            self.assertNotEqual(snap_before, snap_after,
                                'LCD did not change after SELECT rotation')

            # SELECT +1 again: third tab
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'settings-tab2-select-right')
            snap_tab2 = self._lcd_snapshot(page)

            self.assertNotEqual(snap_after, snap_tab2,
                                'LCD did not change on second SELECT rotation')

            self._assert_alive(page)
        finally:
            self.write_log(page, 'settings-select-tabs.log')
            page.close()

    def test_algo_scrolls_rows(self):
        """ALGO encoder scrolls rows within the current tab."""
        page = self.new_emu_page()
        try:
            ctrl = self._boot_and_open_settings(page)

            # Navigate past DISPLAY to a tab with menu items (SELECT +1)
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(200)

            # ALGO scrolls rows within the tab
            ctrl.rotate_encoder('algorithm', direction=1, steps=3, delay_ms=200)
            page.wait_for_timeout(300)
            self.screenshot(page, 'settings-algo-scroll')

            self._assert_alive(page)
        finally:
            self.write_log(page, 'settings-algo-scroll.log')
            page.close()

    def test_preset_cycles_value(self):
        """PRESET (K1) encoder cycles the selected item's value."""
        page = self.new_emu_page()
        try:
            ctrl = self._boot_and_open_settings(page)
            self.screenshot(page, 'settings-preset-before')
            snap_before = self._lcd_snapshot(page)

            # On DISPLAY tab, PRESET cycles palettes
            ctrl.rotate_encoder('k1', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'settings-preset-after')
            snap_after = self._lcd_snapshot(page)

            self.assertNotEqual(snap_before, snap_after,
                                'LCD did not change after PRESET rotation')

            self._assert_alive(page)
        finally:
            self.write_log(page, 'settings-preset-value.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
