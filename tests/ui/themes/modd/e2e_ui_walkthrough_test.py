#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: walk every UI page in the modd theme using the generated nav map.

Loads the firmware into fm1-emu, then visits every page by pressing the
button mapped to its view (or encoder-cycling within multi-page views).
Takes a screenshot of each page. Uses gen_ui_map.load_nav_map() so the
walkthrough stays in sync with the source.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'codegen'))

from e2e_fixtures import E2ETestCase, fwsc_path, defconfig_name, ROOT
from gen_ui_map import load_nav_map, load_defconfig, control_path

FWSC_PATH = fwsc_path()
DEFCONFIG = defconfig_name()
THEME = 'modd'


class UIWalkthroughTest(E2ETestCase):
    """Walk every page in the modd theme and screenshot each one."""

    nav_map = None

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest(
                f'Firmware not built ({FWSC_PATH}). Run "task build" first.')
        super().setUpClass()
        cls.nav_map = load_nav_map(THEME, root=ROOT)

    def _load_and_boot(self, page):
        """Upload firmware and wait for boot."""
        page.locator('#open').set_input_files(str(FWSC_PATH))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function('''() => {
            const t = document.getElementById("status")?.textContent || "";
            return t.includes("instructions/s") || t.includes("real time");
        }''', timeout=30_000)
        page.wait_for_function('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > 500;
        }''', timeout=120_000)
        page.wait_for_timeout(5_000)

    @staticmethod
    def _execute_step(panel, page, step):
        """Execute a single control_path step on the emulator."""
        if step['action'] == 'hold':
            panel.press_button(step['button'], hold_ms=900, delay_ms=500)
            page.wait_for_timeout(2_000)
        elif step['action'] == 'press':
            panel.press_button(step['button'], delay_ms=500)
            page.wait_for_timeout(2_000)
        elif step['action'] == 'cycle':
            panel.rotate_encoder(step['encoder'], direction=step['direction'],
                                 delay_ms=500)
            page.wait_for_timeout(1_000)

    def _screenshot_tooltip(self, panel, page, label):
        """Tap GLO to show tooltip overlay, screenshot, tap again to dismiss."""
        panel.press_button('glo', delay_ms=300)
        page.wait_for_timeout(1_000)
        self.screenshot(page, f'{label}-tooltip')
        self.screenshot_lcd(page, f'{label}-tooltip')
        panel.press_button('glo', delay_ms=300)
        page.wait_for_timeout(500)

    def test_walkthrough_all_pages(self):
        page = self.new_emu_page(mhz=10)
        visited = []
        try:
            self._load_and_boot(page)
            panel = self.controls(page)
            config = load_defconfig(DEFCONFIG, root=ROOT)
            steps = control_path(self.nav_map, config=config)

            # Firmware boots to HOME; capture it before any navigation.
            boot_view = steps[0]['view'] if steps else None
            if boot_view and steps[0]['action'] == 'press':
                label = f'{steps[0]["view"]}-{steps[0]["page"]}'
                self.screenshot(page, label)
                self.screenshot_lcd(page, label)
                self._screenshot_tooltip(panel, page, label)
                visited.append(steps[0]['page'])
                steps = steps[1:]

            for step in steps:
                self._execute_step(panel, page, step)
                label = f'{step["view"]}-{step["page"]}'
                self.screenshot(page, label)
                self.screenshot_lcd(page, label)
                self._screenshot_tooltip(panel, page, label)
                visited.append(step['page'])

            self.assertGreater(len(visited), 0, 'No pages visited')

            expected = [s['page'] for s in
                        control_path(self.nav_map, config=config)]
            for name in expected:
                self.assertIn(name, visited,
                              f'Page {name} was not visited during walkthrough')

        finally:
            self.write_log(page)
            page.close()

        print(f'\nWalkthrough visited {len(visited)} pages: {", ".join(visited)}')


if __name__ == '__main__':
    unittest.main()
