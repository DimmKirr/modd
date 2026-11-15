#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: capture scrollbar in demo list and settings menu."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import E2ETestCase, fwsc_path

FWSC_PATH = fwsc_path('modd_dev_mock_ui')


class ScrollbarTest(E2ETestCase):
    """Upload mock-ui firmware, screenshot scrollbar in demo list and settings."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest(
                f'Firmware not built ({FWSC_PATH}). '
                'Run "task build -- --defconfig modd_dev_mock_ui" first.')
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

    def test_scrollbar(self):
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

            panel = self.controls(page)

            # --- Settings: SYSTEM tab with 8 demo items ---
            panel.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(3_000)

            # Navigate to SYSTEM tab (SELECT right: DISPLAY -> MIDI -> SYSTEM)
            panel.rotate_encoder('select', direction=1, steps=2, delay_ms=200)
            page.wait_for_timeout(1_000)

            self.screenshot_lcd(page, 'SETTINGS-SYSTEM-top')

            # Scroll down through demo items
            panel.rotate_encoder('algorithm', direction=1, steps=4, delay_ms=200)
            page.wait_for_timeout(1_000)

            self.screenshot_lcd(page, 'SETTINGS-SYSTEM-scrolled')

            panel.rotate_encoder('algorithm', direction=1, steps=3, delay_ms=200)
            page.wait_for_timeout(1_000)

            self.screenshot_lcd(page, 'SETTINGS-SYSTEM-bottom')

            # --- Demo page LIST screen ---
            panel.press_button('oct-', delay_ms=300)
            page.wait_for_timeout(1_000)

            panel.press_button('fx', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(3_000)

            # SELECT right to LIST sub-screen
            panel.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(2_000)

            self.screenshot_lcd(page, 'DEMO-LIST-top')

            panel.rotate_encoder('knob1', direction=1, steps=8, delay_ms=200)
            page.wait_for_timeout(1_000)

            self.screenshot_lcd(page, 'DEMO-LIST-scrolled')

            panel.rotate_encoder('knob1', direction=1, steps=5, delay_ms=200)
            page.wait_for_timeout(1_000)

            self.screenshot_lcd(page, 'DEMO-LIST-bottom')

            self.write_log(page)
        finally:
            page.close()


if __name__ == '__main__':
    unittest.main()
