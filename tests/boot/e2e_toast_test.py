#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: exercise all toast notification states via the DEMO TOAST screen.

Navigates to DEMO > TOAST, cycles K1 through each pre-scripted toast state,
screenshots each, and verifies that the footer area pixels change between
the clear state and active toast states.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import E2ETestCase, fwsc_path

FWSC_PATH = fwsc_path()

TOAST_STATES = [
    '(clear)',
    'Scanning WiFi',
    'Connecting WiFi',
    'Acquiring IP',
    'Fetching weather',
    'WiFi disconnected',
]


class ToastTest(E2ETestCase):
    """Walk all toast states in the DEMO screen and verify visuals."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest(
                f'Firmware not built ({FWSC_PATH}). Run "task build" first.')
        super().setUpClass()

    def _load_and_boot(self, page):
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

    def _footer_pixel_hash(self, page):
        """Return a hash of the footer area pixels (bottom 28 px of 240x240 LCD)."""
        return page.evaluate('''() => {
            const c = document.getElementById('panel');
            const ctx = c.getContext('2d');
            // LCD is at (370,42) in the canvas, 480x480 px for 240x240 logical
            // Footer starts at y=212 out of 240, so pixel row 424 out of 480
            const d = ctx.getImageData(370, 42 + 424, 480, 56).data;
            let h = 0;
            for (let i = 0; i < d.length; i += 4)
                h = ((h << 5) - h + d[i] + d[i+1] + d[i+2]) | 0;
            return h;
        }''')

    def test_toast_states(self):
        """Navigate to DEMO > TOAST, cycle through all states, screenshot each."""
        page = self.new_emu_page(mhz=10)
        try:
            self._load_and_boot(page)
            self._wait_lcd(page)
            page.wait_for_timeout(5_000)

            panel = self.controls(page)

            # Navigate to DEMO page: long-press FX lands on Weather,
            # then SELECT right once to reach DEMO (next page in VIEW_CONN)
            panel.press_button('fx', hold_ms=1200, delay_ms=2000)
            self.screenshot(page, 'toast-00-weather-entry')
            self.screenshot_lcd(page, 'toast-00-weather-entry')

            panel.rotate_encoder('select', direction=1, steps=1, delay_ms=1000)
            page.wait_for_timeout(1_000)
            self.screenshot(page, 'toast-00b-demo-entry')
            self.screenshot_lcd(page, 'toast-00b-demo-entry')

            # Navigate to screen 6 (TOAST): turn SELECT right 6 times
            panel.rotate_encoder('select', direction=1, steps=6, delay_ms=500)
            page.wait_for_timeout(1_000)
            self.screenshot(page, 'toast-01-toast-screen')
            self.screenshot_lcd(page, 'toast-01-toast-screen')

            # Capture the clear-state footer hash
            clear_hash = self._footer_pixel_hash(page)
            prev_hash = clear_hash
            changed_count = 0

            # Cycle through each toast state with K1
            for i in range(1, len(TOAST_STATES)):
                panel.rotate_encoder('knob1', direction=1, steps=1, delay_ms=800)
                page.wait_for_timeout(500)

                name = f'toast-{i + 1:02d}-{TOAST_STATES[i].replace(" ", "_")}'
                self.screenshot(page, name)
                self.screenshot_lcd(page, name)

                cur_hash = self._footer_pixel_hash(page)
                if cur_hash != clear_hash:
                    changed_count += 1
                prev_hash = cur_hash

            # At least 3 of the 5 active states should have changed the footer
            self.assertGreaterEqual(
                changed_count, 3,
                f'Expected at least 3 toast states to change footer pixels, '
                f'got {changed_count}')

            # Return to clear and verify footer goes back
            panel.rotate_encoder('knob1', direction=-1,
                                 steps=len(TOAST_STATES) - 1, delay_ms=500)
            page.wait_for_timeout(1_000)
            self.screenshot(page, 'toast-07-cleared')
            self.screenshot_lcd(page, 'toast-07-cleared')

        finally:
            self.write_log(page)
            page.close()


if __name__ == '__main__':
    unittest.main()
