#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: Felucca button-hold layer overlays.

Tests the layer system: while a page button (GLO/SCL/SEQ/EDIT) is held,
K1-K4 remap to layer-specific params, and keys are silenced or remapped.
Double-tap locks the layer open. Only runs on modd_felucca_emu.
"""
import os
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase, rms


def _is_felucca():
    dc = os.environ.get('MODD_API_DEFCONFIG', ApiTestCase.DEFCONFIG)
    return 'felucca' in dc


@unittest.skipUnless(_is_felucca(), 'Felucca layers only')
class TestFeluccaLayers(ApiTestCase):
    """Button-hold layer overlays on the Felucca stack."""
    DEFCONFIG = 'modd_felucca_emu'

    def _home_data(self):
        self.go_home()
        return self.api.page()['data']

    def _mixer_data(self):
        r = self.api.goto('mixer')
        return r.get('data', self.api.page()['data'])

    def _track_levels(self):
        """Return T1-T4 P_LEVEL from the MIXER page data."""
        d = self._mixer_data()
        return [t['level'] for t in d['tracks']]

    def _track_mutes(self):
        """Return T1-T4 P_MUTE from the MIXER page data."""
        d = self._mixer_data()
        return [t['mute'] for t in d['tracks']]

    # ── GLO layer: K1-K4 = T1-T4 LEVEL ──

    def test_glo_layer_levels(self):
        """Hold GLO, turn K1, verify T1 P_LEVEL changed."""
        self.go_home()
        levels_before = self._track_levels()
        self.go_home()

        self.api.button_down('glo')
        self.api.wait_ms(50)
        self.api.enc('knob1', 5)
        self.api.button_up('glo')
        self.api.wait_idle()

        levels_after = self._track_levels()
        self.assertGreater(levels_after[0], levels_before[0],
                           f'GLO+K1 should raise T1 level: before={levels_before[0]}, after={levels_after[0]}')
        self.go_home()

    def test_glo_layer_all_knobs(self):
        """Hold GLO, each knob adjusts a different track's level."""
        self.go_home()
        levels_before = self._track_levels()
        self.go_home()

        self.api.button_down('glo')
        self.api.wait_ms(50)
        for i, knob in enumerate(('knob1', 'knob2', 'knob3', 'knob4')):
            self.api.enc(knob, 3)
        self.api.button_up('glo')
        self.api.wait_idle()

        levels_after = self._track_levels()
        for i in range(4):
            with self.subTest(track=i):
                self.assertGreater(levels_after[i], levels_before[i],
                                   f'GLO+K{i+1} should raise T{i+1} level')
        self.go_home()

    # ── SCL layer: K1 = ROOT, keys set ROOT ──

    def test_scl_layer_root(self):
        """Hold SCL, turn K1, verify P_ROOT changed."""
        self.go_home()
        root_before = self._home_data()['root']

        self.api.button_down('scl')
        self.api.wait_ms(50)
        self.api.enc('knob1', 2)
        self.api.button_up('scl')
        self.api.wait_idle()

        root_after = self._home_data()['root']
        self.assertNotEqual(root_after, root_before,
                            f'SCL+K1 should change root: before={root_before}, after={root_after}')

    def test_scl_layer_key_sets_root(self):
        """Hold SCL, press a key, verify P_ROOT matches the note."""
        self.go_home()
        self.api.button_down('scl')
        self.api.wait_ms(50)
        # Press E4 (MIDI 64, E = chromatic 4)
        self.api.key_down('E4')
        self.api.wait_ms(50)
        self.api.key_up('E4')
        self.api.button_up('scl')
        self.api.wait_idle()

        root = self._home_data()['root']
        self.assertEqual(root, 4, f'SCL+key E4 should set root to 4 (E), got {root}')

    # ── SEQ layer: K1 = P_SLEN ──

    def test_seq_layer_params(self):
        """Hold SEQ, turn K1, verify P_SLEN changed."""
        self.go_home()
        # Read SEQ page data for the initial P_SLEN
        r = self.api.goto('seq')
        slen_before = self.api.page()['data']['len']

        self.go_home()
        self.api.button_down('seq')
        self.api.wait_ms(50)
        self.api.enc('knob1', 2)
        self.api.button_up('seq')
        self.api.wait_idle()

        r = self.api.goto('seq')
        slen_after = self.api.page()['data']['len']
        self.assertNotEqual(slen_after, slen_before,
                            f'SEQ+K1 should change SLEN: before={slen_before}, after={slen_after}')
        self.go_home()

    # ── EDIT layer: K1 = cycle engine ──

    def test_edit_layer_engine_cycle(self):
        """Hold EDIT, turn K1, verify engine changed."""
        self.go_home()
        eng_before = self.api.track('sel')['engine']

        self.api.button_down('edit')
        self.api.wait_ms(50)
        self.api.enc('knob1', 1)
        self.api.button_up('edit')
        self.api.wait_idle()

        eng_after = self.api.track('sel')['engine']
        self.assertNotEqual(eng_after, eng_before,
                            f'EDIT+K1 should cycle engine: before={eng_before}, after={eng_after}')
        self.go_home()

    # ── Double-tap lock ──

    def test_double_tap_lock(self):
        """Double-tap GLO to lock the layer, verify K1 still adjusts T1 level after release."""
        self.go_home()
        levels_before = self._track_levels()
        self.go_home()

        # First tap: press, wait, release (the firmware records tap time on release)
        self.api.button_down('glo')
        self.api.wait_ms(50)
        self.api.button_up('glo')
        self.api.wait_ms(50)
        # Second tap within 300ms — locks the layer
        self.api.button_down('glo')
        self.api.wait_ms(50)
        self.api.button_up('glo')
        self.api.wait_ms(100)

        # Both taps released, layer should be locked open — decrease level
        self.api.enc('knob1', -5)
        self.api.wait_idle()

        levels_after = self._track_levels()
        self.assertNotEqual(levels_after[0], levels_before[0],
                            f'Double-tap GLO lock: K1 should adjust T1 level after release: {levels_before[0]}')

        # Tap GLO once to unlock
        self.api.button_down('glo')
        self.api.wait_ms(50)
        self.api.button_up('glo')
        self.api.wait_idle()
        self.go_home()

    # ── Key silencing ──

    def test_key_silenced_during_layer(self):
        """While GLO layer is held, a key press does not trigger a new note."""
        self.go_home()
        self.api.pot(1023)
        self.api.wait_ms(200)

        # Hold GLO first, then press and release a key
        self.api.button_down('glo')
        self.api.wait_ms(200)
        self.api.key_tap('C4', ms=200)
        self.api.button_up('glo')
        self.api.wait_ms(500)

        # After the layer key (not a note_on), audio should be silent
        level_layer = self.api.audio()['rms']

        # Now press the same key WITHOUT any layer — should produce sound
        self.api.key_down('C4')
        self.api.wait_ms(200)
        level_normal = self.api.audio()['rms']
        self.api.key_up('C4')
        self.api.wait_idle()

        self.assertGreater(level_normal, level_layer,
                           f'Normal key should be louder than silenced: normal={level_normal}, layer={level_layer}')
        self.go_home()

    # ── GLO layer key: mute toggle ──

    def test_glo_layer_mute_toggle(self):
        """GLO held + black key toggles track mute."""
        self.go_home()
        mutes_before = self._track_mutes()
        self.go_home()

        # F#3 = MIDI 54 = T1 mute toggle
        self.api.button_down('glo')
        self.api.wait_ms(50)
        self.api.key_down(54)
        self.api.wait_ms(50)
        self.api.key_up(54)
        self.api.button_up('glo')
        self.api.wait_idle()

        mutes_after = self._track_mutes()
        self.assertNotEqual(mutes_after[0], mutes_before[0],
                            f'GLO+F#3 should toggle T1 mute: before={mutes_before[0]}, after={mutes_after[0]}')

        # Toggle back
        self.api.button_down('glo')
        self.api.wait_ms(50)
        self.api.key_down(54)
        self.api.wait_ms(50)
        self.api.key_up(54)
        self.api.button_up('glo')
        self.api.wait_idle()

        mutes_restored = self._track_mutes()
        self.assertEqual(mutes_restored[0], mutes_before[0],
                         'second GLO+F#3 should restore T1 mute')
        self.go_home()

    # ── SAVE hold = undo ──

    def test_save_hold_undo(self):
        """SAVE held past threshold undoes the last sound change."""
        self.go_home()
        self.api.set_engine('ANALOG')
        t0 = self.api.track('sel')
        preset_before = t0['preset']

        # Change preset
        self.api.enc('presets', 1)
        t1 = self.api.track('sel')
        self.assertNotEqual(t1['preset'], preset_before, 'preset should have changed')

        # Hold SAVE to undo
        self.api.hold('save', 700)
        t2 = self.api.track('sel')
        self.assertEqual(t2['preset'], preset_before,
                         f'SAVE hold should undo: expected preset {preset_before}, got {t2["preset"]}')
        self.go_home()

    # ── BPM LOCK ──

    def test_bpm_lock(self):
        """With BPM LOCK on, SELECT does nothing unless GLO is held."""
        self.go_home()
        bpm_start = self._home_data()['bpm']

        # Navigate to Settings > CONTROL, find BPM LOCK, set it ON
        self.api.goto('menu', 'CONTROL')
        page = self.api.page()
        items = page['data']['items']
        bpm_lock_idx = None
        for i, it in enumerate(items):
            if it['name'] == 'BPM LOCK':
                bpm_lock_idx = i
                break
        if bpm_lock_idx is None:
            self.skipTest('BPM LOCK menu item not found')

        # Navigate to the BPM LOCK row: algo encoder steps rows
        for _ in range(bpm_lock_idx):
            self.api.enc('algorithm', 1)
        # Set to ON with OCT+
        if items[bpm_lock_idx]['value'] != 'ON':
            self.api.tap('oct+')

        # Verify it's ON
        page = self.api.page()
        for it in page['data']['items']:
            if it['name'] == 'BPM LOCK':
                self.assertEqual(it['value'], 'ON', 'BPM LOCK should be ON')
                break

        # Go HOME, turn SELECT — BPM should NOT change
        self.go_home()
        bpm_before = self._home_data()['bpm']
        self.api.enc('select', 3)
        bpm_after = self._home_data()['bpm']
        self.assertEqual(bpm_after, bpm_before,
                         f'With BPM LOCK on, SELECT should not change BPM: {bpm_before} -> {bpm_after}')

        # Hold GLO + SELECT — BPM SHOULD change
        self.api.button_down('glo')
        self.api.wait_ms(50)
        self.api.enc('select', 3)
        self.api.button_up('glo')
        self.api.wait_idle()
        bpm_glo = self._home_data()['bpm']
        self.assertGreater(bpm_glo, bpm_before,
                           f'With GLO held, SELECT should change BPM: {bpm_before} -> {bpm_glo}')

        # Turn BPM LOCK off (cleanup)
        self.api.goto('menu', 'CONTROL')
        for _ in range(bpm_lock_idx):
            self.api.enc('algorithm', 1)
        self.api.tap('oct+')  # ON -> OFF (wraps)
        self.go_home()


if __name__ == '__main__':
    unittest.main()
