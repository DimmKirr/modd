#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: walk all pages and settings tabs, screenshot each."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import E2ETestCase, fwsc_path

FWSC_PATH = fwsc_path()


class PageWalkTest(E2ETestCase):
    """Boot modd firmware, walk every reachable page and settings tab."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest(
                f'Firmware not built ({FWSC_PATH}). Run "task build" first.')
        super().setUpClass()

    def load_firmware(self, page):
        page.locator('#open').set_input_files(str(FWSC_PATH))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            '''() => {
                const t = document.getElementById("status")?.textContent || "";
                return t.includes("instructions/s") || t.includes("real time");
            }''',
            timeout=30_000)

    def _wait_lcd(self, page, timeout=120_000):
        page.wait_for_function('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > 500;
        }''', timeout=timeout)

    def test_page_walk(self):
        page = self.new_emu_page(mhz=10)
        try:
            self.load_firmware(page)
            self._wait_lcd(page)
            page.wait_for_timeout(5_000)

            panel = self.controls(page)

            # 1. HOME page
            self.screenshot(page, 'walk-01-HOME')
            self.screenshot_lcd(page, 'walk-01-HOME')

            # 2. Settings: HOME hold
            panel.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(3_000)
            self.screenshot(page, 'walk-02-SETTINGS-DISPLAY')
            self.screenshot_lcd(page, 'walk-02-SETTINGS-DISPLAY')

            # 3. MIDI tab (SELECT right x1: DISPLAY -> MIDI)
            panel.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(1_000)
            self.screenshot(page, 'walk-03-SETTINGS-MIDI')
            self.screenshot_lcd(page, 'walk-03-SETTINGS-MIDI')

            # 4. HW tab (SELECT right x1: MIDI -> HARDWARE)
            panel.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(1_000)
            self.screenshot(page, 'walk-04-SETTINGS-HW')
            self.screenshot_lcd(page, 'walk-04-SETTINGS-HW')

            # 5. NET tab (SELECT right x1: HARDWARE -> NETWORK)
            panel.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(1_000)
            self.screenshot(page, 'walk-05-SETTINGS-NET')
            self.screenshot_lcd(page, 'walk-05-SETTINGS-NET')

            # 6. Back to HOME
            panel.press_button('home', delay_ms=300)
            page.wait_for_timeout(2_000)
            self.screenshot(page, 'walk-06-HOME-return')
            self.screenshot_lcd(page, 'walk-06-HOME-return')

            # 7. Weather page (FX hold)
            panel.press_button('fx', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(3_000)
            self.screenshot(page, 'walk-07-WEATHER')
            self.screenshot_lcd(page, 'walk-07-WEATHER')

            # 8. Back to HOME
            panel.press_button('home', delay_ms=300)
            page.wait_for_timeout(2_000)

            self.assertNotIn('halted', page.locator('#status').text_content())
        finally:
            self.write_log(page)
            page.close()


if __name__ == '__main__':
    unittest.main()
