#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: one effect page, read and set over REST, on two builds whose audio paths differ.

modd_emu: the modd effects host (CONFIG_FX_HOST, one master slot). modd_felucca_emu: the upstream stack included
unchanged, whose INSERT is driven by upstream's track parameters. The test body is the same for both: GET /api/page
lists the cards, GET /api/page/INSERT/set/<slot>/<value> sets one (no key, no encoder), the capture changes with a
note playing, and setting it back restores it.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiError, ApiTestCase, rms


class FxPageMixin:
    SETTINGS = ()        # (slot, value) pairs that make the effect clearly audible, in order
    OFF = ()             # (slot, value) pairs that put it back
    CARD_KEYS = {'label', 'value', 'text', 'min', 'max'}

    def cards(self):
        page = self.api.page()
        self.assertEqual(page['page'], 'INSERT')
        return page['data']['cards']

    def apply(self, pairs):
        for slot, value in pairs:
            r = self.api.get(f'/api/page/INSERT/set/{slot}/{value}')
            self.assertEqual(r['value'], value, r)
        self.api.wait_ms(300)

    def note_rms(self):
        s = self.api.record_note('C4')
        return rms(s), s

    def test_effect_page_over_api(self):
        api = self.api
        self.go_home()
        api.set_engine('ANALOG')
        api.pot(1023)
        api.wait_ms(400)
        api.goto('INSERT')
        api.wait_ms(300)
        before_cards = self.cards()
        self.save_json('fx-page-cards.json', before_cards)
        self.assertGreaterEqual(len(before_cards), 2)
        for c in before_cards:
            self.assertEqual(set(c), self.CARD_KEYS)
            self.assertLessEqual(c['min'], c['value'])
            self.assertLessEqual(c['value'], c['max'])
        self.assertEqual(before_cards[0]['label'], self.FIRST_LABEL)
        self.assertEqual(before_cards[0]['text'], 'OFF')

        self.go_home()                                       # the note keys play on HOME
        dry, dry_s = self.note_rms()
        self.save_wav('fx-page-dry.wav', dry_s)
        self.assertGreater(dry, 0.01, 'no output with the effect off')

        self.apply(self.SETTINGS)
        api.goto('INSERT')
        api.wait_ms(300)
        cards = self.cards()
        for slot, value in self.SETTINGS:
            self.assertEqual(cards[slot]['value'], value, f'card {slot} reads back {cards[slot]}')
        self.assertNotEqual(cards[0]['text'], 'OFF')
        self.go_home()
        wet, wet_s = self.note_rms()
        self.save_wav('fx-page-wet.wav', wet_s)

        self.apply(self.OFF)
        api.goto('INSERT')
        api.wait_ms(300)
        self.assertEqual([c['value'] for c in self.cards()][:1], [0])
        self.assertEqual(self.cards()[0]['text'], 'OFF')
        self.go_home()
        back, back_s = self.note_rms()
        self.save_json('fx-page.json', {'dry': dry, 'wet': wet, 'restored': back})
        print(f'\nfx page {self.DEFCONFIG}: rms dry {dry:.4f}, effect {wet:.4f}, restored {back:.4f}')
        self.assertGreater(abs(wet - dry) / dry, 0.05, f'the effect did not change the capture ({dry:.4f} -> {wet:.4f})')
        self.assertLess(abs(back - dry) / dry, 0.03, f'off did not restore it ({dry:.4f} -> {back:.4f})')

    def test_set_errors(self):
        """A value or slot outside the cards, and a page with no settable cards, are errors; nothing changes."""
        api = self.api
        self.go_home()
        api.goto('INSERT')
        api.wait_ms(300)
        before = self.cards()
        for path, code in ((f'/api/page/INSERT/set/0/{before[0]["max"] + 1}', '400'),
                           ('/api/page/INSERT/set/7/0', '400'),
                           ('/api/page/HOME/set/0/0', '404')):
            with self.assertRaises(ApiError, msg=path) as cm:
                api.get(path)
            self.assertIn(f'HTTP {code}', str(cm.exception), path)
        self.assertEqual(self.cards(), before)
        self.assertAlive()


class FxPageHostTest(FxPageMixin, ApiTestCase):
    """Path 1: the modd effects host. TYPE 0 OFF, then the units' types in one list: SOFT 1, HARD 2, FOLD 3, FUZZ 4."""
    DEFCONFIG = 'modd_emu'
    FIRST_LABEL = 'TYPE'
    SETTINGS = ((1, 127), (0, 4))
    OFF = ((0, 0),)


class FxPageStackTest(FxPageMixin, ApiTestCase):
    """Path 2: upstream's INSERT through its track parameters. TYPE OFF, SOFT, HARD (2) ..."""
    DEFCONFIG = 'modd_felucca_emu'
    FIRST_LABEL = 'INSRT'
    SETTINGS = ((0, 2),)
    OFF = ((0, 0),)


if __name__ == '__main__':
    unittest.main()
