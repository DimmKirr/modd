#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: one memory arena (CONFIG_ARENA_MELODEE, Melodee resources.c) serves two upstream request styles and the fx host.

A Melodee-style request (resource_get(RES_ENGINE0 + part)) for part 0 and a SLOOP-style request (eng_arena_of)
for part 1 both get blocks in the one area, not overlapping, and `used` is their sum; releasing brings it back;
an oversize request is refused without a reset; the fx host's FLANGER block comes from the same area at the same time.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase

FLANGER = 5
RES_FX0 = 5


class ArenaTest(ApiTestCase):
    def arena(self, path=''):
        return self.api.get('/api/arena' + path)

    def release_all(self):
        for part in range(4):
            self.arena(f'/release/{part}')
        self.api.get('/api/fx/slot/off')
        self.api.wait_for(lambda: (lambda d: d['state'] == 'off' and d['arena_used'] == 0)(self.api.get('/api/fx/slot')),
                          timeout=20, poll=0.3, what='fx slot off')

    def setUp(self):
        self.go_home()
        self.release_all()
        self.up0 = self.api.info()['uptime_ms']

    def tearDown(self):
        self.release_all()
        self.assertAlive()
        self.assertGreaterEqual(self.api.info()['uptime_ms'], self.up0, 'uptime went backwards: the firmware reset')

    @staticmethod
    def span(b):
        return b['addr'], b['addr'] + b['size']

    def assertDisjoint(self, blocks):
        spans = sorted(self.span(b) for b in blocks)
        for (a0, a1), (b0, b1) in zip(spans, spans[1:]):
            self.assertLessEqual(a1, b0, f'blocks overlap: {spans}')

    def test_a_two_styles_one_area(self):
        d = self.arena()
        self.assertEqual((d['used'], d['blocks']), (0, []))
        cap, fail0 = d['capacity'], d['failures']
        self.assertGreater(cap, 100_000)

        m = self.arena('/probe/melodee/0/20000')
        self.assertTrue(m['ok'])
        self.assertNotEqual(m['addr'], 0)
        s = self.arena('/probe/sloop/1/30000')
        self.assertTrue(s['ok'])
        self.assertNotEqual(s['addr'], 0)
        self.assertEqual(sorted(b['id'] for b in s['blocks']), [0, 1])
        self.assertEqual(s['used'], sum(b['size'] for b in s['blocks']))
        self.assertEqual(s['used'], 20000 + 30000)
        self.assertDisjoint(s['blocks'])
        print('\narena blocks:', [(b['id'], hex(b['addr']), b['size']) for b in s['blocks']],
              f"used {s['used']} / capacity {cap}")

        self.arena('/release/0')
        d = self.arena()
        self.assertEqual(d['used'], 30000)
        self.arena('/release/1')
        self.assertEqual(self.arena()['used'], 0)

    def test_b_oversize_is_refused_without_reset(self):
        d0 = self.arena()
        up = self.api.info()['uptime_ms']
        d = self.arena('/probe/melodee/2/4000000')
        self.assertFalse(d['ok'])
        self.assertEqual(d['addr'], 0)
        self.assertGreater(d['failures'], d0['failures'])
        d = self.arena('/probe/sloop/3/4000000')
        self.assertFalse(d['ok'])
        self.assertEqual(d['used'], 0)
        self.assertGreaterEqual(self.api.info()['uptime_ms'], up)
        # a request that fits still works afterwards
        self.assertTrue(self.arena('/probe/melodee/2/1000')['ok'])

    def test_c_fx_host_block_comes_from_the_same_area(self):
        self.assertTrue(self.arena('/probe/melodee/0/20000')['ok'])
        self.assertTrue(self.arena('/probe/sloop/1/30000')['ok'])
        self.api.get(f'/api/fx/slot/on/{FLANGER}/100')
        fx = self.api.wait_for(lambda: (lambda d: d if d['state'] == 'placed' else None)(self.api.get('/api/fx/slot')),
                               timeout=20, poll=0.3, what='fx placed')
        d = self.arena()
        ids = sorted(b['id'] for b in d['blocks'])
        self.assertEqual(ids, [0, 1, RES_FX0], d)
        self.assertDisjoint(d['blocks'])
        fxb = [b for b in d['blocks'] if b['id'] == RES_FX0][0]
        self.assertEqual(fxb['size'], (fx['bytes'] + 3) & ~3)
        self.assertEqual(d['used'], 20000 + 30000 + fxb['size'])
        print(f"\narena with fx host: {[(b['id'], hex(b['addr']), b['size']) for b in d['blocks']]} used {d['used']}")
        self.api.get('/api/fx/slot/off')
        self.api.wait_for(lambda: (lambda x: x['state'] == 'off' and x['arena_used'] == 50000)(self.api.get('/api/fx/slot')),
                          timeout=20, poll=0.3, what='fx off, its block back')

    def test_d_refuse_switch_still_works(self):
        self.api.get('/api/fx/refuse/1')
        try:
            self.api.get(f'/api/fx/slot/on/{FLANGER}/100')
            self.api.wait_for(lambda: (lambda d: d if d['state'] == 'refused' else None)(self.api.get('/api/fx/slot')),
                              timeout=20, poll=0.3, what='refused')
            self.assertEqual(self.arena()['used'], 0)
        finally:
            self.api.get('/api/fx/refuse/0')


if __name__ == '__main__':
    unittest.main()
