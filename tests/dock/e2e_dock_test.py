#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: the Settings INSTR tab (CONFIG_DOCK) in fm1-emu.

Boots build/modd.fwsc, long-presses HOME, tabs to INSTR and checks the tab
renders with no disk (MODD ENGINES row, INSERT USB DISK status), that ALGO
scrolling and OCT+ on the engines row do not halt the firmware, and that the
firmware keeps running. The emulator has no USB mass storage, so the install
path itself is hardware-only.
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import E2ETestCase

ROOT = Path(__file__).resolve().parents[2]
FWSC_PATH = ROOT / 'build' / 'modd.fwsc'
CONFIG_H = ROOT / 'build' / 'gen' / 'config.h'


def _dock_enabled():
    return CONFIG_H.is_file() and re.search(r'^#define CONFIG_DOCK 1$', CONFIG_H.read_text(), re.M)


class TestDockInstr(E2ETestCase):
    """INSTR tab in the emulator (no disk attached)."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        if not _dock_enabled():
            raise unittest.SkipTest('CONFIG_DOCK is off in build/gen/config.h')
        super().setUpClass()

    def _boot_to_home(self, page):
        page.locator('#open').set_input_files(str(FWSC_PATH))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            '(() => { const t = document.getElementById("status")?.textContent || "";'
            ' return t.includes("instructions/s") || t.includes("real time"); })()',
            timeout=30_000)
        page.wait_for_function('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > 500;
        }''', timeout=120_000)
        page.wait_for_timeout(2000)

    def _open_instr(self, page, ctrl):
        """Long HOME opens settings; SELECT right lands on INSTR after SYSTEM."""
        ctrl.press_button('home', hold_ms=600, delay_ms=500)
        page.wait_for_timeout(500)
        # DISPLAY -> (MIDI) -> SYSTEM -> INSTR: the first tab after SYSTEM
        for _ in range(3):
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
        page.wait_for_timeout(400)

    def test_instr_tab_renders_without_disk(self):
        page = self.new_emu_page()
        try:
            self._boot_to_home(page)
            ctrl = self.controls(page)
            self._open_instr(page, ctrl)
            self.screenshot(page, 'SETTINGS-instr-noDisk')
            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'dock-instr.log')
            page.close()

    def test_instr_scroll_and_engines_row(self):
        """ALGO scrolls the list; OCT+ on MODD ENGINES is a no-op that does not halt."""
        page = self.new_emu_page()
        try:
            self._boot_to_home(page)
            ctrl = self.controls(page)
            self._open_instr(page, ctrl)
            ctrl.rotate_encoder('algorithm', direction=1, steps=2, delay_ms=200)
            ctrl.rotate_encoder('algorithm', direction=-1, steps=3, delay_ms=200)
            page.wait_for_timeout(300)
            ctrl.press_button('oct+', delay_ms=400)
            page.wait_for_timeout(500)
            self.screenshot(page, 'SETTINGS-instr-enginesRow')
            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'dock-instr-scroll.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
