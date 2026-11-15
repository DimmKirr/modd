#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: verify contextual hints appear on GLO tap."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import E2ETestCase, fwsc_path

FWSC_PATH = fwsc_path()


class HintsTest(E2ETestCase):
    """Upload modd.fwsc, press GLO, verify hints overlay renders."""

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

    def _tap_glo(self, page):
        """Quick GLO tap: hold_ms=10 to stay under firmware hold threshold."""
        panel = self.controls(page)
        panel.press_button('glo', hold_ms=10, delay_ms=100)

    def test_glo_tap_shows_hints_on_home(self):
        page = self.new_emu_page(mhz=10)
        try:
            self.load_firmware(page)
            page.wait_for_function('''() => {
                const c = document.getElementById('panel');
                const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
                let n = 0;
                for (let i = 0; i < p.length; i += 4)
                    if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
                return n > 500;
            }''', timeout=120_000)
            page.wait_for_timeout(5_000)

            self.screenshot(page, 'HOME-before-glo')
            self.screenshot_lcd(page, 'HOME-before-glo')

            self._tap_glo(page)
            page.wait_for_timeout(2_000)

            self.screenshot(page, 'HOME-hints-glo')
            self.screenshot_lcd(page, 'HOME-hints-glo')

            self._tap_glo(page)
            page.wait_for_timeout(2_000)

            self.screenshot(page, 'HOME-hints-off')
            self.screenshot_lcd(page, 'HOME-hints-off')

            # Navigate to Settings (HOME hold)
            panel = self.controls(page)
            panel.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(3_000)

            # Switch to MIDI tab (SELECT right twice: DISPLAY→MIDI)
            panel.rotate_encoder('select', steps=2, delay_ms=200)
            page.wait_for_timeout(1_000)

            self.screenshot(page, 'SETTINGS-MIDI-normal')
            self.screenshot_lcd(page, 'SETTINGS-MIDI-normal')

            self._tap_glo(page)
            page.wait_for_timeout(2_000)

            self.screenshot(page, 'SETTINGS-MIDI-zones-glo')
            self.screenshot_lcd(page, 'SETTINGS-MIDI-zones-glo')

        finally:
            self.write_log(page)
            page.close()


if __name__ == '__main__':
    unittest.main()
