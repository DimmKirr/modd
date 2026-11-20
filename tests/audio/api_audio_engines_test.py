#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: engine audio output, master volume and level consistency.

Replaces the browser e2e_audio_engines_test.py (felucca build, SELECT encoder to
pick the engine, WebAudio tap to record). Here the modd_emu firmware is
driven over HTTP: the engine of the selected track is set with
/api/track/sel/engine/<name> (the modd theme has no engine picker on the panel:
its SELECT picks the track), notes are played on the note keys and the master
pot through /api/input, and the firmware's own output is recorded with
/api/audio/capture. WAV files are saved in
tests/results/<timestamp>-api_audio_engines_test/.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase, note_hz, rms, tone_power

NOTES = ['C4', 'E4', 'G4']
CHORD = ['C4', 'E4', 'G4']


class EngineAudioCase(ApiTestCase):
    """Helpers: pick an engine, set the master pot, record a chord or notes."""

    def setUp(self):
        self.go_home()

    def use_engine(self, name):
        t = self.api.set_engine(name)
        self.assertEqual(t['engine'], name, f'engine not switched: {t}')
        return t

    def set_volume(self, pot):
        self.api.pot(pot)
        self.api.wait_ms(400)       # the main loop smooths the pot

    def record_chord(self, label):
        s = self.api.record_chord(CHORD)
        self.save_wav(f'{label}.wav', s)
        return rms(s)

    def engine_produces_audio(self, name, pitched=True):
        self.use_engine(name)
        self.set_volume(1023)
        silence = self.api.capture()
        self.save_wav(f'{name.lower()}-silence.wav', silence)
        results = {'silence_rms': rms(silence)}
        for note in NOTES:
            s = self.api.record_note(note)
            self.save_wav(f'{name.lower()}-{note}.wav', s)
            f0 = note_hz(note)
            results[note] = {'rms': rms(s), 'f0': tone_power(s, f0),
                             'f0_plus_2st': tone_power(s, f0 * 2 ** (2 / 12))}
        self.screenshot(f'HOME-{name.lower()}-noteOn')
        self.save_json(f'{name.lower()}.json', results)
        self.assertLess(results['silence_rms'], 0.001, f'{name}: not silent before the notes')
        for note in NOTES:
            r = results[note]
            self.assertGreater(r['rms'], 0.001, f'No audio from {name} on {note} (RMS {r["rms"]:.6f})')
            if pitched:
                self.assertGreater(r['f0'], 4 * r['f0_plus_2st'],
                                   f'{name} {note}: not at its pitch ({r["f0"]:.4f} at f0, '
                                   f'{r["f0_plus_2st"]:.4f} two semitones up)')
        self.assertAlive()


class TestNoiseEngine(EngineAudioCase):
    def test_noise_engine_produces_audio(self):
        """C4, E4, G4 on the Noise engine all produce audio."""
        self.engine_produces_audio('NOISE', pitched=False)


class TestAnalogEngine(EngineAudioCase):
    def test_analog_engine_produces_audio(self):
        """C4, E4, G4 on the Analog engine produce audio at their pitch."""
        self.engine_produces_audio('ANALOG')


class TestFM6Engine(EngineAudioCase):
    def test_fm6_engine_produces_audio(self):
        """C4, E4, G4 on the FM6 engine produce audio at their pitch."""
        self.engine_produces_audio('FM6')


class TestMasterVolume(EngineAudioCase):
    def test_master_volume_controls_output(self):
        """Record a chord at pot 0, 10, 512, 1023: silent at 0, louder as it opens."""
        self.use_engine('ANALOG')
        levels = {}
        for pot in (0, 10, 512, 1023):
            self.set_volume(pot)
            levels[pot] = self.record_chord(f'volume-{pot}')
        self.save_json('volume.json', levels)
        self.assertLess(levels[0], 0.001, f'Volume at 0 should be silent (RMS {levels[0]:.6f})')
        self.assertGreater(levels[1023], 0.001, f'Volume at max should sound (RMS {levels[1023]:.6f})')
        self.assertGreater(levels[1023], levels[512], f'Full > half: {levels}')
        self.assertGreater(levels[512], levels[10], f'Half > low: {levels}')


class TestAudioLevelNormalization(EngineAudioCase):
    """Engines at the same settings produce similar levels; the master scales them."""

    ENGINES = ['ANALOG', 'NOISE', 'FM6']

    def levels_at(self, pot):
        self.set_volume(pot)
        out = {}
        for name in self.ENGINES:
            self.use_engine(name)
            out[name] = self.record_chord(f'norm-{name.lower()}-vol{pot}')
            self.screenshot(f'HOME-{name.lower()}-vol{pot}-noteOn')
        return out

    def test_engines_not_silent(self):
        """Every engine produces audible output at 50% master volume."""
        levels = self.levels_at(512)
        self.save_json('norm-silence-check.json', levels)
        for name, level in levels.items():
            self.assertGreater(level, 0.001, f'{name} produced no audio at 50% (RMS {level:.6f})')

    def test_engine_levels_within_12db(self):
        """RMS levels across engines are within 12 dB (4x) of Analog."""
        levels = self.levels_at(512)
        ref = levels['ANALOG']
        ratios = {f'{n}_vs_ANALOG': levels[n] / max(ref, 1e-9) for n in levels if n != 'ANALOG'}
        self.save_json('norm-levels.json', {'rms': levels, 'ratios': ratios})
        self.assertGreater(ref, 0.001, 'Reference engine ANALOG is silent')
        for key, ratio in ratios.items():
            self.assertLess(ratio, 4.0, f'{key}: {ratio:.2f}x too loud')
            self.assertGreater(ratio, 0.25, f'{key}: {ratio:.2f}x too quiet')

    def test_master_volume_scales_output(self):
        """Master at 50% gives less signal than at ~100%."""
        self.use_engine('ANALOG')
        self.set_volume(512)
        half = self.record_chord('norm-analog-vol512-scaling')
        self.set_volume(1000)
        full = self.record_chord('norm-analog-vol1000-scaling')
        self.save_json('norm-volume-scaling.json', {'rms_half': half, 'rms_full': full,
                                                    'ratio': full / max(half, 1e-9)})
        self.assertGreater(half, 0.001)
        self.assertGreater(full, 0.001)
        self.assertGreater(full, half, f'Full {full:.6f} should be louder than half {half:.6f}')


class TestEngineSelectVolumeConsistency(EngineAudioCase):
    """Switching the engine away and back leaves the level where it was (within 3 dB)."""

    def test_preset_round_trip_same_level(self):
        self.set_volume(307)
        self.use_engine('ANALOG')
        initial = self.record_chord('preset-analog-initial')
        self.assertGreater(initial, 0.001, f'No audio at the start (RMS {initial:.6f})')
        self.use_engine('NOISE')
        noise = self.record_chord('preset-noise')
        self.use_engine('ANALOG')
        back = self.record_chord('preset-analog-back')
        self.assertGreater(back, 0.001, f'No audio after switching back (RMS {back:.6f})')
        ratio = back / initial
        self.save_json('preset-round-trip.json', {'rms_initial': initial, 'rms_noise': noise,
                                                  'rms_back': back, 'ratio_back_vs_initial': ratio})
        self.assertGreater(ratio, 0.707, f'Back on ANALOG too quiet: {ratio:.2f}x')
        self.assertLess(ratio, 1.414, f'Back on ANALOG too loud: {ratio:.2f}x')

    def test_no_bleed_after_engine_switch(self):
        """A note held on one engine does not keep sounding after a switch."""
        self.set_volume(1023)
        self.use_engine('ANALOG')
        self.api.key_down('C4')
        self.api.wait_ms(300)
        self.use_engine('FM6')                  # modd_engine_select silences the track's voices
        self.api.key_up('C4')
        self.api.wait_idle()
        self.api.wait_ms(300)
        s = self.api.capture()
        self.save_wav('bleed-after-switch.wav', s)
        self.assertLess(rms(s), 0.001, f'output after an engine switch with no key held (RMS {rms(s):.5f})')


class TestEngineSelectDisplay(EngineAudioCase):
    """The LCD follows engine and track changes at once."""

    def test_engine_name_updates_on_select(self):
        self.use_engine('ANALOG')
        before = self.screenshot('HOME-engine-analog')
        t = self.use_engine('NOISE')
        after = self.screenshot('HOME-engine-noise')
        self.assertNotEqual(before.tobytes(), after.tobytes(), 'LCD did not change after an engine switch')
        home = self.api.page()['data']
        self.assertEqual(home['tracks'][t['track']]['engine'], 'NOISE', f'HOME data: {home}')

    def test_select_encoder_changes_track(self):
        """SELECT on HOME (the panel encoder) picks the next track; the LCD follows."""
        sel = self.api.page()['data']['selected_track']
        step = 1 if sel < 3 else -1
        before = self.screenshot('HOME-select-initial')
        self.api.enc('select', step)
        after = self.screenshot('HOME-select-rotated')
        self.assertEqual(self.api.page()['data']['selected_track'], sel + step)
        self.assertNotEqual(before.tobytes(), after.tobytes(), 'LCD did not change after SELECT')


if __name__ == '__main__':
    unittest.main()
