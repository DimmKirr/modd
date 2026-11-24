#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: the modd effects host (CONFIG_FX_HOST) with Melodee's SLOOP insert unit in the master slot.

(a) the output differs with the unit on and off; (b) a memory-less type (CRUSH) borrows 0 bytes, a type
with delay lines (FLANGER) borrows a block and gives it back after off; (c) with the arena refusing, the
slot says "refused", the audio stays dry and the firmware does not reset.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase, rms

FUZZ, CRUSH, FLANGER = 3, 4, 5
SLOOP_INSERT, OPT_EXGAIN = 0, 1      # unit numbers in FX_UNITS[]


def roughness(s):
    """Mean absolute first difference over rms: grows with high-frequency content."""
    d = sum(abs(s[i + 1] - s[i]) for i in range(len(s) - 1)) / (len(s) - 1)
    r = rms(s) * 32768
    return d / r if r else 0.0


class FxHostTest(ApiTestCase):
    def slot(self, path=''):
        return self.api.get('/api/fx/slot' + path)

    def wait_state(self, state):
        return self.api.wait_for(lambda: (lambda d: d if d['state'] == state else None)(self.slot()),
                                 timeout=20, poll=0.3, what=f'slot state {state}')

    def switch_off(self):
        self.slot('/off')
        self.api.wait_for(lambda: (lambda d: d['state'] == 'off' and d['arena_used'] == 0)(self.slot()),
                          timeout=20, poll=0.3, what='slot off, arena empty')

    def setUp(self):
        self.go_home()
        self.api.set_engine('ANALOG')
        self.api.pot(1023)
        self.api.get('/api/fx/refuse/0')
        self.switch_off()
        self.api.wait_ms(300)
        self.up0 = self.api.info()['uptime_ms']

    def tearDown(self):
        self.api.get('/api/fx/refuse/0')
        self.switch_off()
        self.assertAlive()
        self.assertGreaterEqual(self.api.info()['uptime_ms'], self.up0, 'uptime went backwards: the firmware reset')

    def test_a_capture_differs_with_the_unit_on(self):
        api = self.api
        dry = api.record_note('C4')
        self.save_wav('fx-dry.wav', dry)
        self.assertGreater(rms(dry), 0.01, 'no dry audio')
        r_dry, k_dry = rms(dry), roughness(dry)
        diffs = {}
        for name, t in (('crush', CRUSH), ('fuzz', FUZZ)):
            self.slot(f'/on/{t}/127')
            self.wait_state('placed')
            api.wait_ms(300)
            wet = api.record_note('C4')
            self.save_wav(f'fx-{name}.wav', wet)
            r_wet, k_wet = rms(wet), roughness(wet)
            diffs[name] = {'rms': r_wet, 'roughness': k_wet}
            print(f'\nfx on/off ({name}): rms dry {r_dry:.4f} wet {r_wet:.4f}; roughness dry {k_dry:.4f} wet {k_wet:.4f}')
            self.assertGreater(r_wet, 0.001, f'{name} output is silent')
            self.assertTrue(abs(r_wet - r_dry) > 0.03 * r_dry or abs(k_wet - k_dry) > 0.03 * k_dry,
                            f'the capture does not differ with {name} on')
            self.switch_off()
        self.save_json('fx-diff.json', {'dry': {'rms': r_dry, 'roughness': k_dry}, **diffs})
        self.switch_off()
        api.wait_ms(300)
        back = api.record_note('C4')
        self.assertAlmostEqual(rms(back), r_dry, delta=0.1 * r_dry, msg='dry level did not come back after off')
        self.assertAlmostEqual(roughness(back), k_dry, delta=0.1 * k_dry, msg='dry shape did not come back after off')

    def test_b_memory_less_type_borrows_nothing_and_flanger_returns_its_block(self):
        d = self.slot(f'/on/{CRUSH}/100')
        d = self.wait_state('placed')
        self.assertEqual((d['type_name'], d['bytes'], d['arena_used']), ('CRUSH', 0, 0))
        self.switch_off()

        self.slot(f'/on/{FLANGER}/100')
        d = self.wait_state('placed')
        self.assertEqual(d['type_name'], 'FLANGER')
        self.assertGreaterEqual(d['bytes'], 2 * 256 * 2, 'FLANGER borrowed less than its two lines')
        self.assertTrue(d['bytes'] <= d['arena_used'] < d['bytes'] + 8, 'the arena lent a block of the size asked (rounded to 8)')
        print(f'\nfx bytes: CRUSH 0, FLANGER {d["bytes"]} (arena used {d["arena_used"]})')
        self.api.wait_ms(300)
        self.switch_off()
        d = self.slot()
        self.assertEqual((d['state'], d['arena_used']), ('off', 0))

    def test_c_refused_slot_passes_dry_and_does_not_reset(self):
        api = self.api
        dry = api.record_note('C4')
        self.assertEqual(self.slot()['state'], 'off')
        api.get('/api/fx/refuse/1')
        self.slot(f'/on/{FLANGER}/127')
        d = self.wait_state('refused')
        self.assertEqual(d['arena_used'], 0)
        api.wait_ms(300)
        wet = api.record_note('C4')
        r_dry, r_wet = rms(dry), rms(wet)
        k_dry, k_wet = roughness(dry), roughness(wet)
        print(f'\nfx refused: rms dry {r_dry:.4f} slot {r_wet:.4f}; roughness {k_dry:.4f} {k_wet:.4f}')
        self.assertAlmostEqual(r_wet, r_dry, delta=0.1 * r_dry, msg='refused slot changed the level')
        self.assertAlmostEqual(k_wet, k_dry, delta=0.1 * k_dry, msg='refused slot changed the sound')
        self.assertEqual(self.slot()['state'], 'refused')

    def unit_on(self, unit, type_, amount):
        self.slot(f'/unit/{unit}/{type_}/{amount}')
        return self.wait_state('placed')

    def test_d_optimist_gain_unit_in_the_same_slot(self):
        """Optimist's EXGN (exgain.c) runs in the slot the Melodee unit used: louder at 127 (+7.5 dB),
        quieter at 40, and the slot goes Melodee unit -> Optimist unit -> off with the arena empty."""
        api = self.api
        dry = api.record_note('C4')
        r_dry = rms(dry)
        self.assertGreater(r_dry, 0.01, 'no dry audio')

        d = self.unit_on(SLOOP_INSERT, CRUSH, 100)
        self.assertEqual(d['unit'], 'SLOOP_INSERT')
        api.wait_ms(200)
        up0 = api.info()['uptime_ms']

        d = self.unit_on(OPT_EXGAIN, 0, 127)          # the running Melodee unit goes off first, then this one
        self.assertEqual((d['unit'], d['type_name'], d['bytes']), ('OPT_EXGAIN', 'GAIN', 0))
        api.wait_ms(500)
        loud = api.record_note('C4')
        self.save_wav('fx-exgain-127.wav', loud)
        r_loud = rms(loud)

        d = self.unit_on(OPT_EXGAIN, 0, 40)
        api.wait_ms(500)
        quiet = api.record_note('C4')
        r_quiet = rms(quiet)
        print(f'\nfx exgain: rms dry {r_dry:.4f}, amount 127 {r_loud:.4f}, amount 40 {r_quiet:.4f}')
        self.assertGreater(r_loud, 1.5 * r_dry, 'EXGN at 127 (+7.5 dB) did not raise the level')
        self.assertLess(r_quiet, 0.5 * r_dry, 'EXGN at 40 (about -36 dB) did not lower the level')

        self.switch_off()
        d = self.slot()
        self.assertEqual((d['state'], d['arena_used']), ('off', 0))
        api.wait_ms(300)
        back = api.record_note('C4')
        self.assertAlmostEqual(rms(back), r_dry, delta=0.1 * r_dry, msg='dry level did not come back after off')
        self.assertGreaterEqual(api.info()['uptime_ms'], up0, 'uptime went backwards: the firmware reset')

    def test_e_unit_that_is_not_there_is_refused(self):
        self.slot('/unit/7/0/100')
        self.wait_state('refused')
        self.assertEqual(self.slot()['arena_used'], 0)


if __name__ == '__main__':
    unittest.main()
