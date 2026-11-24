#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: the Felucca stack included from upstream (CONFIG_AUDIO_FELUCCA_STACK_INCLUDED, modd_felucca_emu).

The build runs upstream's params, mod, voice, slicer, fx (mixer, effects) and audio interrupt. A note
plays, and turning the track's INSERT on changes what the capture holds (modd's stub zeroed P_IMIX and
never ran it).
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase, rms


class FeluccaStackTest(ApiTestCase):
    DEFCONFIG = 'modd_felucca_emu'

    def _note_rms(self, note='C4'):
        s = self.api.record_note(note)
        return rms(s), s

    def test_note_plays(self):
        """A note on each engine the stack's registry holds here sounds through upstream's mixer."""
        api = self.api
        self.go_home()
        api.pot(1023)
        results = {}
        for eng in ('ANALOG', 'NOISE', 'FM6'):
            api.set_engine(eng)
            api.wait_ms(400)
            level, samples = self._note_rms()
            self.save_wav(f'stack-{eng}.wav', samples)
            results[eng] = level
            self.assertEqual(api.track()['engine'], eng)
            self.assertGreater(level, 0.01, f'{eng}: no output while C4 is held (RMS {level:.5f})')
        self.save_json('stack-engines.json', results)
        self.assertAlive()

    def test_insert_changes_the_capture(self):
        """INSERT TYPE off -> HARD (the clipper) changes the capture; back to off restores it."""
        api = self.api
        self.go_home()
        api.set_engine('ANALOG')
        api.pot(1023)
        api.wait_ms(400)
        api.goto('INSERT')
        self.assertEqual(api.page()['data']['cards'][0]['value'], 0)
        self.go_home()                                        # the note keys play on HOME
        dry, dry_s = self._note_rms()
        self.save_wav('insert-off.wav', dry_s)
        api.goto('INSERT')
        api.enc('knob1', 2)                                   # TYPE: OFF, SOFT, HARD
        self.assertEqual(api.page()['data']['cards'][0]['value'], 2)
        self.go_home()
        wet, wet_s = self._note_rms()
        self.save_wav('insert-hard.wav', wet_s)
        api.goto('INSERT')
        api.enc('knob1', -2)
        self.go_home()
        back, back_s = self._note_rms()
        self.save_json('insert.json', {'off': dry, 'hard': wet, 'off_again': back})
        self.assertGreater(dry, 0.01, 'no output with INSERT off')
        self.assertGreater(abs(wet - dry) / dry, 0.05,
                           f'INSERT HARD did not change the capture (RMS {dry:.4f} -> {wet:.4f})')
        self.assertLess(abs(back - dry) / dry, 0.02,
                        f'INSERT back to OFF did not restore it (RMS {dry:.4f} -> {back:.4f})')
        self.assertAlive()


if __name__ == '__main__':
    unittest.main()
