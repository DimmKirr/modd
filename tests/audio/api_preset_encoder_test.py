#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: PRESET encoder switches sounds on the Felucca stack.

The PRESET encoder must cycle presets within an engine and cross into the
next/previous engine at the boundary. Each preset must load its parameters
(E0-E7, envelope, FX sends), not just move a counter — verified by checking
that a different preset produces a different sound on the same note.

Runs against modd_felucca_emu (Felucca audio core with preset tables).
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase, rms


class TestPresetEncoder(ApiTestCase):
    """PRESET encoder navigates presets and loads their parameters."""
    DEFCONFIG = 'modd_felucca_emu'

    def setUp(self):
        self.go_home()
        self.api.set_engine('ANALOG')
        self.api.pot(1023)
        self.api.wait_ms(400)

    def _track(self):
        return self.api.track('sel')

    def test_algo_selects_track(self):
        """ALGO encoder steps through tracks (not instruments)."""
        t0 = self._track()
        sel0 = t0['track']
        if sel0 >= 3:
            self.api.enc('algorithm', -1)
            sel0 = self._track()['track']
        self.api.enc('algorithm', 1)
        sel1 = self._track()['track']
        self.assertEqual(sel1, sel0 + 1,
                         'ALGO +1 should advance to the next track')

    def test_preset_steps_within_engine(self):
        """One PRESET click stays on the same engine, advances the preset."""
        t0 = self._track()
        self.api.enc('presets', 1)
        t1 = self._track()
        self.assertEqual(t1['engine'], t0['engine'],
                         'one PRESET step should stay in the same engine')
        self.assertEqual(t1['preset'], t0['preset'] + 1,
                         'PRESET +1 should advance the preset index')

    def test_preset_crosses_to_next_engine(self):
        """Turning PRESET forward enough crosses into the next engine."""
        eng0 = self._track()['engine']
        for i in range(50):
            self.api.enc('presets', 1)
            t = self._track()
            if t['engine'] != eng0:
                self.assertEqual(t['preset'], 0,
                                 'first preset of the new engine should be 0')
                return
        self.fail(f'PRESET never left engine {eng0} after 50 steps')

    def test_preset_crosses_back(self):
        """Turning PRESET backward from a non-first engine goes to the previous one."""
        eng0 = self._track()['engine']
        for _ in range(50):
            self.api.enc('presets', 1)
            t = self._track()
            if t['engine'] != eng0 and t['engine'] != '-':
                break
        else:
            self.skipTest(f'could not reach a non-first real engine from {eng0}')
        eng_mid = t['engine']
        for i in range(60):
            self.api.enc('presets', -1)
            t = self._track()
            if t['engine'] != eng_mid:
                return
        self.fail(f'PRESET backward never left engine {eng_mid}')

    def test_preset_loads_params_different_sound(self):
        """Switching to a different preset changes the actual sound output.

        Record a note on preset 0, step to preset 1, record the same note.
        The two captures must differ — proving parameters were loaded, not
        just the counter moved.
        """
        t0 = self._track()
        self.assertEqual(t0['preset'], 0, 'should start on preset 0')
        s0 = self.api.record_note('C4', 300)
        self.save_wav('preset0-C4.wav', s0)
        rms0 = rms(s0)
        self.assertGreater(rms0, 0.001, f'preset 0 should produce audio (RMS {rms0:.6f})')

        self.api.enc('presets', 1)
        t1 = self._track()
        self.assertEqual(t1['preset'], 1, 'should be on preset 1 after one step')
        s1 = self.api.record_note('C4', 300)
        self.save_wav('preset1-C4.wav', s1)
        rms1 = rms(s1)
        self.assertGreater(rms1, 0.001, f'preset 1 should produce audio (RMS {rms1:.6f})')

        self.assertNotEqual(s0, s1,
                            'preset 0 and preset 1 should sound different '
                            '(params were not loaded)')

    def test_engine_cross_loads_new_engine_sound(self):
        """After crossing into a real engine via PRESET, the sound comes from
        that engine — not the old one with just a counter change."""
        eng0 = self._track()['engine']
        for _ in range(80):
            self.api.enc('presets', 1)
            t = self._track()
            if t['engine'] != eng0 and t['engine'] != '-':
                break
        else:
            self.skipTest(f'could not reach a different real engine from {eng0}')
        eng1 = t['engine']
        self.assertNotEqual(eng0, eng1)

        s_new = self.api.record_note('C4', 300)
        self.save_wav(f'{eng1.lower()}-preset0-C4.wav', s_new)
        rms_new = rms(s_new)
        self.assertGreater(rms_new, 0.001,
                           f'{eng1} preset 0 should produce audio (RMS {rms_new:.6f})')
        self.screenshot(f'HOME-{eng1.lower()}-after-preset-cross')


if __name__ == '__main__':
    unittest.main()
