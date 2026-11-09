#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: modd theme debug log page.

GLO tap opens the debug page from HOME, GLO tap again returns.
Captures screenshots for visual QA.

Run:
    uv run python -m pytest firmware/src/ui/themes/modd/tests/e2e_debug_test.py -v
"""
import unittest
from pathlib import Path

from e2e_fixtures import E2ETestCase

FWSC = Path(__file__).resolve().parents[6] / 'build' / 'modd.fwsc'


class TestModdDebugPage(E2ETestCase):
    """modd theme: debug log page via GLO toggle."""

    @classmethod
    def setUpClass(cls):
        if not FWSC.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()

    def _boot(self, page):
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("real time")',
            timeout=120_000)
        page.wait_for_function('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > 500;
        }''', timeout=120_000)
        page.wait_for_timeout(2000)
        return self.controls(page)

    def test_debug_page_opens(self):
        """GLO tap opens the debug page, screenshot for visual QA."""
        page = self.new_emu_page()
        try:
            ctrl = self._boot(page)

            snap_home = self._lcd_snapshot(page)
            self.screenshot(page, 'debug-01-home-before-glo')

            ctrl.press_button('glo', hold_ms=30, delay_ms=300)
            page.wait_for_timeout(1000)
            snap_glo = self._lcd_snapshot(page)
            self.screenshot(page, 'debug-02-page-open')

            self.assertNotEqual(snap_home, snap_glo,
                                'LCD did not change after GLO tap')

            ctrl.press_button('glo', hold_ms=30, delay_ms=300)
            page.wait_for_timeout(1000)
            snap_back = self._lcd_snapshot(page)
            self.screenshot(page, 'debug-03-returned-to-home')

            self.assertNotEqual(snap_glo, snap_back,
                                'LCD did not change after second GLO tap')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'debug-page.log')
            page.close()

    def _lcd_snapshot(self, page):
        return page.evaluate('''() => {
            const c = document.getElementById('panel');
            const d = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let h = 0;
            for (let i = 0; i < d.length; i += 16)
                h = ((h << 5) - h + d[i]) | 0;
            return h;
        }''')
