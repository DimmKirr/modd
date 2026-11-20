#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: the firmware's audio output, recorded by the firmware itself.

Replaces the browser e2e_audio_test.py, which recorded through a WebAudio tap
in the page (and tested that tap with a JS oscillator). Here the firmware
keeps its last 8192 output samples (CONFIG_RPC_AUDIO_TAP, after the master
volume, as the DAC gets them) and /api/audio/capture returns them; notes are
played on the panel's note keys through /api/input. WAV files are saved in
tests/results/<timestamp>-api_audio_test/.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase, note_hz, rms, tone_power

SCALE = ['C4', 'D4', 'E4', 'F4', 'G4']


class AudioCaptureTest(ApiTestCase):
    """The capture follows the output: silent at rest, sound while a key is held, silent after."""

    def test_capture_follows_the_output(self):
        api = self.api
        self.go_home()
        api.set_engine('ANALOG')
        api.pot(1023)
        api.wait_ms(300)

        silence = api.capture()
        self.save_wav('silence.wav', silence)
        self.assertLess(rms(silence), 0.001, f'output not silent at rest (RMS {rms(silence):.5f})')

        held = api.record_note('C4')
        self.save_wav('c4-held.wav', held)
        self.assertGreater(rms(held), 0.01, f'no output while C4 is held (RMS {rms(held):.5f})')

        api.wait_ms(1500)                       # past the release tail (P_REL)
        after = api.capture()
        self.save_wav('after-release.wav', after)
        self.assertLess(tone_power(after, note_hz('C4')), 0.001,
                        'C4 still sounding 1.5 s after the key was let go')
        self.save_json('capture.json', {'rms_silence': rms(silence), 'rms_held': rms(held),
                                        'c4_after_release': tone_power(after, note_hz('C4'))})


class AnalogProducesAudioTest(ApiTestCase):
    """Play C4..G4 on the Analog engine: each note sounds, at its own pitch."""

    def test_analog_produces_audio(self):
        api = self.api
        self.go_home()
        api.set_engine('ANALOG')
        self.assertEqual(api.track()['engine'], 'ANALOG')
        api.pot(1023)
        api.wait_ms(300)
        results = {}
        for note in SCALE:
            s = api.record_note(note)
            self.save_wav(f'analog-{note}.wav', s)
            f0 = note_hz(note)
            results[note] = {'rms': rms(s), 'f0': tone_power(s, f0),
                             'f0_plus_2st': tone_power(s, f0 * 2 ** (2 / 12))}
        self.screenshot('HOME-analog-noteOn')
        self.save_json('analog.json', results)
        for note, r in results.items():
            self.assertGreater(r['rms'], 0.001, f'{note}: no audio (RMS {r["rms"]:.5f})')
            self.assertGreater(r['f0'], 4 * r['f0_plus_2st'],
                               f'{note}: not at its pitch ({r["f0"]:.4f} at f0, '
                               f'{r["f0_plus_2st"]:.4f} two semitones up)')
        self.assertAlive()


if __name__ == '__main__':
    unittest.main()
