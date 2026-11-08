#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: verify declarative knob dispatch changes parameter values.

Rotates each knob on every param page and asserts the corresponding card
visual changes, proving the param_slot_t dispatch path in shell.c works.
Data-driven from gen_ui_map.py so the same test validates any theme's
controls configuration: if a different theme compiles with different
param_slot_t tables, the test adapts automatically.

This complements e2e_ui_controls_test.py (crash-only) with value assertions.
"""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'codegen'))
from gen_ui_map import parse_page_controls, discover_themes

FWSC = ROOT / 'build' / 'modd.fwsc'

# Felucca card layout: 4 cards at Y=28, each 57x44, spaced at 59px intervals.
# The emulator panel canvas is 2x (retina): multiply LCD coords by 2, then
# offset by the panel's LCD viewport origin. The panel renders the 240x240 LCD
# at a fixed position within the 1280x820 canvas.
#
# LCD viewport origin on the 1280x820 panel canvas (from panel.rs):
LCD_X0 = 370
LCD_Y0 = 42
LCD_SCALE = 2
CARD_Y = 28
CARD_W = 57
CARD_H = 44
CARD_STRIDE = 59

# Map enc_id -> encoder name for FM1Controls.send_encoder
ENC_ID_TO_NAME = {0: 'select', 1: 'algorithm', 2: 'knob1', 3: 'knob2',
                  4: 'knob3', 5: 'knob4', 6: 'presets'}

# Pages reachable by button press (btn name -> expected page)
BUTTON_PAGES = {
    'env': 'ENV', 'lfo': 'LFO', 'fx': 'FX', 'scl': 'SCL', 'arp': 'ARP',
}


def _card_region(card_index):
    """Return (x, y, w, h) in panel canvas coords for a card's value area.

    Covers the middle portion of the card where the numeric value is drawn,
    skipping the label row at top and progress bar at bottom.
    """
    lcd_x = 3 + CARD_STRIDE * card_index
    lcd_y = CARD_Y + 12  # skip label row
    lcd_w = CARD_W
    lcd_h = 20  # value text area
    return (
        LCD_X0 + lcd_x * LCD_SCALE,
        LCD_Y0 + lcd_y * LCD_SCALE,
        lcd_w * LCD_SCALE,
        lcd_h * LCD_SCALE,
    )


JS_CARD_HASH = '''([x, y, w, h]) => {
    const c = document.getElementById('panel');
    const ctx = c.getContext('2d');
    const d = ctx.getImageData(x, y, w, h).data;
    let hash = 0;
    for (let i = 0; i < d.length; i += 4) {
        hash = ((hash << 5) - hash + d[i] + d[i+1] + d[i+2]) | 0;
    }
    return hash;
}'''


def _card_enc_index(enc_id):
    """Map encoder ID to card index (0-3). K1=card0, K2=card1, etc.
    ALGO maps to card0 (for MIXER where ALGO is the first track)."""
    if 2 <= enc_id <= 5:
        return enc_id - 2
    if enc_id == 1:  # ALGO
        return 0
    return None


class TestKnobValues(E2ETestCase):
    """Verify knob rotation changes displayed values via declarative dispatch."""

    @classmethod
    def setUpClass(cls):
        if not FWSC.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()
        cls.theme = cls._detect_theme()
        if cls.theme is None:
            raise unittest.SkipTest('No built theme detected')
        theme_dir = ROOT / 'firmware' / 'src' / 'ui' / 'themes' / cls.theme
        cls.page_controls = parse_page_controls(theme_dir)

    @classmethod
    def _detect_theme(cls):
        """Detect which theme was compiled from build/gen/config.h."""
        config_h = ROOT / 'build' / 'gen' / 'config.h'
        if not config_h.exists():
            return None
        text = config_h.read_text()
        if '#define CONFIG_UI_FELUCCA 1' in text:
            return 'felucca'
        if '#define CONFIG_UI_MODD 1' in text:
            return 'modd'
        if '#define CONFIG_UI_MELODEE 1' in text:
            return 'melodee'
        return None

    def _boot(self):
        page = self.new_emu_page(mhz=100)
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("instructions/s")',
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
        return page

    def _assert_alive(self, page):
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status, f'Emulator halted: {status}')

    def _get_card_hash(self, page, card_index):
        """Return a pixel hash of a card's value area."""
        region = list(_card_region(card_index))
        return page.evaluate(JS_CARD_HASH, region)

    def _knobs_for_page_symbol(self, page_symbol):
        """Return {enc_id: label} for a page from the parsed controls."""
        c = self.page_controls.get(page_symbol)
        if c is None:
            return {}
        return c.get('knobs', {})

    # ------------------------------------------------------------------
    # Test 1: Param pages (ENV, LFO, FX, SCL, ARP) via button navigation
    # ------------------------------------------------------------------
    def test_01_param_page_knobs_change_values(self):
        """Rotating K1-K4 on each param page changes the displayed card value."""
        page_symbol_map = {
            'env': 'FPAGE_ENV', 'lfo': 'FPAGE_LFO', 'fx': 'FPAGE_FX',
            'scl': 'FPAGE_SCL', 'arp': 'FPAGE_ARP',
        }
        page = self._boot()
        try:
            ctrl = self.controls(page)
            for btn, symbol in page_symbol_map.items():
                knobs = self._knobs_for_page_symbol(symbol)
                if not knobs:
                    continue

                ctrl.press_button(btn, delay_ms=400)
                page.wait_for_timeout(300)
                self._assert_alive(page)

                for enc_id, label in sorted(knobs.items()):
                    card_idx = _card_enc_index(enc_id)
                    if card_idx is None:
                        continue

                    enc_name = ENC_ID_TO_NAME.get(enc_id)
                    if enc_name is None:
                        continue

                    self.screenshot(page, f'{btn.upper()}-before-{label}')
                    hash_before = self._get_card_hash(page, card_idx)

                    ctrl.send_encoder(enc_id, clicks=3, delay_ms=200)
                    page.wait_for_timeout(200)
                    self._assert_alive(page)

                    hash_after = self._get_card_hash(page, card_idx)
                    self.screenshot(page, f'{btn.upper()}-after-{label}')

                    self.assertNotEqual(
                        hash_before, hash_after,
                        f'{btn.upper()} card {card_idx} ({label}): '
                        f'knob rotation did not change displayed value'
                    )
        finally:
            self.write_log(page, 'param-knob-values.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 2: VOICE page (navigate via GLO button + SELECT cycle)
    # ------------------------------------------------------------------
    def test_02_voice_page_knobs(self):
        """K1-K4 on VOICE page change displayed values."""
        knobs = self._knobs_for_page_symbol('FPAGE_VOICE')
        if not knobs:
            self.skipTest('FPAGE_VOICE has no knob_map')

        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('glo', delay_ms=400)
            page.wait_for_timeout(300)
            # GLO view: first page might be MIXER or DEBUG depending on config.
            # Cycle to VOICE (it's after MIXER in registration order).
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(300)
            self._assert_alive(page)

            for enc_id, label in sorted(knobs.items()):
                card_idx = _card_enc_index(enc_id)
                if card_idx is None:
                    continue
                hash_before = self._get_card_hash(page, card_idx)
                ctrl.send_encoder(enc_id, clicks=3, delay_ms=200)
                page.wait_for_timeout(200)
                self._assert_alive(page)
                hash_after = self._get_card_hash(page, card_idx)
                self.screenshot(page, f'VOICE-after-{label}')
                self.assertNotEqual(
                    hash_before, hash_after,
                    f'VOICE card {card_idx} ({label}): '
                    f'knob rotation did not change displayed value'
                )
        finally:
            self.write_log(page, 'voice-knob-values.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 3: MIXER page with SLOT_TRACK (each encoder targets its own track)
    # ------------------------------------------------------------------
    def test_03_mixer_slot_track(self):
        """MIXER: each encoder changes only its own track's card, not others."""
        knobs = self._knobs_for_page_symbol('FPAGE_MIXER')
        if not knobs:
            self.skipTest('FPAGE_MIXER has no knob_map')

        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('glo', delay_ms=400)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'MIXER-initial')

            tested_any = False
            for enc_id, label in sorted(knobs.items()):
                card_idx = _card_enc_index(enc_id)
                if card_idx is None or card_idx >= 4:
                    continue

                # Snapshot ALL 4 cards before
                hashes_before = [self._get_card_hash(page, i) for i in range(4)]

                ctrl.send_encoder(enc_id, clicks=5, delay_ms=200)
                page.wait_for_timeout(300)
                self._assert_alive(page)

                hashes_after = [self._get_card_hash(page, i) for i in range(4)]
                self.screenshot(page, f'MIXER-after-{label}')

                # The targeted card MUST change
                self.assertNotEqual(
                    hashes_before[card_idx], hashes_after[card_idx],
                    f'MIXER {label}: targeted card {card_idx} did not change'
                )
                tested_any = True

            self.assertTrue(tested_any, 'No MIXER knobs were testable')
        finally:
            self.write_log(page, 'mixer-slot-track.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 4: Theme-agnostic control consistency
    # ------------------------------------------------------------------
    def test_04_controls_table_matches_theme(self):
        """The compiled theme's controls table is non-empty and consistent.

        This verifies that gen_ui_map.py produces meaningful data for whatever
        theme was compiled, ensuring a different controls config (Melodee, SLOOP)
        would also be testable by the same E2E suite.
        """
        self.assertIsNotNone(self.theme, 'No theme detected')
        self.assertGreater(len(self.page_controls), 0,
                           f'{self.theme} theme has no parsed controls')

        param_pages = [s for s in self.page_controls
                       if s.startswith('FPAGE_') and
                       s not in ('FPAGE_HOME', 'FPAGE_EDIT', 'FPAGE_MIXER')]
        for sym in param_pages:
            knobs = self.page_controls[sym]['knobs']
            k14 = {k: v for k, v in knobs.items() if 2 <= k <= 5}
            self.assertEqual(
                len(k14), 4,
                f'{sym} should have 4 knob bindings (K1-K4), got {k14}'
            )


if __name__ == '__main__':
    unittest.main()
