#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: the modd theme (firmware/src/ui/themes/modd) driven over HTTP in fm1-live.

These tests check the page list and order, the page data of the shared controllers
(firmware/src/ui/page_data.c, rpc/api_pages.c), and every button and knob of the control map through /api/input, reading the effect back from
/api/page. The modd theme builds a page in its own UI task, so after /modd/<page> or an
input the tests wait until /api/page shows it.

Build: task modd:build:modd_emu. Run: task modd:test:modd_emu:api.
"""
import hashlib
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from api_fixtures import ApiError, ApiTestCase, rms

# the pages in registration order (ui/themes/modd/pages.c)
PAGES = ['HOME', 'MENU', 'WX', 'DEMO', 'FILES', 'DISK', 'LOG']


class ModdApiCase(ApiTestCase):

    def goto(self, name, sub=None):
        """Jump by name; wait until the UI task has built the page."""
        r = self.api.goto(name, sub)
        self.assertTrue(r['ok'])
        self.assertEqual(r['page'], name)
        return self.wait_page(name)

    def wait_page(self, name, timeout=15):
        return self.api.wait_for(lambda: (p := self.api.page())['page'] == name and p,
                                 timeout=timeout, poll=0.5, what=f'page {name}')

    def wait_data(self, pred, what, timeout=10):
        """The page data once `pred(data)` holds."""
        return self.api.wait_for(lambda: (d := self.api.page()['data']) and pred(d) and d,
                                 timeout=timeout, poll=0.5, what=what)

    def item(self, data, name):
        items = {i['name']: i for i in data['items']}
        self.assertIn(name, items, f'{name} not among {list(items)}')
        return items[name]


class PagesTest(ModdApiCase):
    """Every page is listed, reachable by name and drawn."""

    def test_01_pages_listed(self):
        pages = self.api.pages()
        self.assertEqual([p['name'] for p in pages], PAGES)
        self.assertEqual(self.api.page()['page'], 'HOME', 'boot did not end on HOME')

    def test_02_goto_each_page(self):
        hashes, data = {}, {}
        for name in PAGES:
            with self.subTest(page=name):
                data[name] = self.goto(name)
                img = self.screenshot(f'{name}-goto')
                hashes[name] = hashlib.sha1(img.tobytes()).hexdigest()
        self.save_json('pages.json', {'pages': self.api.pages(), 'page_data': data})
        self.assertEqual(len(set(hashes.values())), len(PAGES), f'two pages drew the same picture: {hashes}')
        self.goto('HOME')
        self.assertAlive()

    def test_03_unknown_page_and_sub(self):
        with self.assertRaisesRegex(ApiError, 'HTTP 404'):
            self.api.goto('nosuch')
        with self.assertRaisesRegex(ApiError, 'HTTP 404'):
            self.api.goto('home', 'nosuch')
        with self.assertRaisesRegex(ApiError, 'HTTP 404'):
            self.api.goto('menu', 'nosuch')
        self.assertAlive()


class PageDataTest(ModdApiCase):
    """/api/page carries the shared controllers' data, the same shape on every theme."""

    def test_home_data(self):
        d = self.goto('HOME')['data']
        self.assertEqual(len(d['tracks']), 4)
        self.assertIn(d['selected_track'], range(4))
        self.assertGreater(d['bpm'], 0)
        for k in ('playing', 'recording', 'oct', 'root', 'scale', 'level', 'atk', 'dec', 'sus', 'rel'):
            self.assertIn(k, d)
        for t in d['tracks']:
            self.assertTrue(t['engine'])
        self.assertIn(d['palette'], range(10))
        self.assertTrue(d['palette_name'])

    def test_menu_tabs(self):
        """/modd/menu/<tab> shows that tab."""
        self.goto('MENU', 'display')
        self.wait_data(lambda d: d['tab'] == 'DISPLAY', 'DISPLAY tab')
        self.goto('MENU', 'midi')
        midi = self.wait_data(lambda d: d['tab'] == 'MIDI', 'MIDI tab')
        self.item(midi, 'MIDI CH')
        self.goto('MENU', 'hw')
        hw = self.wait_data(lambda d: d['tab'] == 'HW', 'HW tab')
        for name in ('DISK', 'ETHERNET'):
            self.item(hw, name)
        self.goto('MENU', 'net')
        net = self.wait_data(lambda d: d['tab'] == 'NET', 'NET tab')
        self.assertTrue(net['connected'], f'no address: {net}')
        # the link fm1-live offers: --usb ethernet (the default), or WiFi alone (--wifi oss)
        flags = os.environ.get('FM1_API_EMU_FLAGS', '').split() or list(self.EMU_FLAGS)
        self.assertEqual(net['link'], 'ETH' if 'ethernet' in flags else 'WIFI')
        self.assertEqual(net['ip_str'].count('.'), 3)
        self.screenshot('MENU-net')
        self.assertAlive()


class PanelTest(ModdApiCase):
    """The modd theme's control map: holds, taps, SELECT, ALGO, OCT+/-."""

    def test_01_holds_open_pages(self):
        """HOME hold: settings; FX hold: WX, again DEMO; GLO hold: LOG and back; HOME tap: HOME."""
        api = self.api
        self.goto('HOME')
        api.hold('fx')
        self.wait_page('WX')
        api.hold('fx')
        self.wait_page('DEMO')
        api.hold('home')
        self.wait_page('MENU')
        api.hold('glo')
        self.wait_page('LOG')
        api.hold('glo')                          # GLO held again: back to where it came from
        self.wait_page('MENU')
        api.tap('home')
        self.wait_page('HOME')
        self.assertAlive()

    def test_01b_save_hold_opens_files_then_disk(self):
        """SAVE held: FILES (the USB disk's list); held again: DISK; OCT- on FILES goes HOME."""
        api = self.api
        self.goto('HOME')
        api.hold('save')
        self.wait_page('FILES')
        self.screenshot('FILES-no-disk')
        api.hold('save')
        self.wait_page('DISK')
        self.screenshot('DISK-no-disk')
        api.hold('save')                         # the view's pages in turn: back to FILES
        self.wait_page('FILES')
        api.tap('oct-')                          # at the top of the disk: the page closes
        self.wait_page('HOME')
        self.assertAlive()

    def test_02_select_cycles_pages(self):
        """SELECT on a page that does not take it: the next page of the view (CTL_SELECT_PAGE)."""
        api = self.api
        self.goto('WX')
        api.enc('select', 1)
        self.wait_page('DEMO')
        self.goto('FILES')                       # no disk: SELECT is not the list's
        api.enc('select', 1)
        self.wait_page('DISK')
        api.enc('select', 1)
        self.wait_page('FILES')
        self.goto('HOME')
        self.assertAlive()

    def test_03_home_select_picks_track(self):
        """SELECT on HOME: the track, stopping at T1 and T4 (no wrap)."""
        api = self.api
        self.goto('HOME')
        api.enc('select', -5)
        self.wait_data(lambda d: d['selected_track'] == 0, 'track 1')
        api.enc('select', 1)
        self.wait_data(lambda d: d['selected_track'] == 1, 'track 2')
        api.enc('select', 10)
        self.wait_data(lambda d: d['selected_track'] == 3, 'track 4')
        self.screenshot('HOME-track4')
        api.enc('select', -3)
        self.wait_data(lambda d: d['selected_track'] == 0, 'track 1 again')
        self.assertAlive()

    def test_04_settings_select_tab_oct_value(self):
        """Settings: SELECT changes the tab, OCT+ / OCT- step the selected row's value."""
        api = self.api
        self.goto('MENU', 'midi')
        d = self.wait_data(lambda d: d['tab'] == 'MIDI', 'MIDI tab')
        before = self.item(d, 'MIDI CH')['index']
        try:
            api.tap('oct+')                      # row 0 is MIDI CH
            api.tap('oct+')
            after = self.wait_data(lambda d: self.item(d, 'MIDI CH')['index'] == before + 2,
                                   'MIDI CH + 2')
            self.screenshot('MENU-midi-ch-up2')
            self.save_json('menu-midi-ch.json', {'before': before, 'after': self.item(after, 'MIDI CH')})
        finally:
            api.tap('oct-')
            api.tap('oct-')
        self.wait_data(lambda d: self.item(d, 'MIDI CH')['index'] == before, 'MIDI CH back')
        api.enc('select', -1)                    # the tab before MIDI, then back
        self.wait_data(lambda d: d['tab'] != 'MIDI', 'another tab')
        api.enc('select', 1)
        self.wait_data(lambda d: d['tab'] == 'MIDI', 'MIDI tab again')
        self.assertAlive()

    def test_05_settings_algo_row_flips_a_switch(self):
        """HW tab: ALGO moves to DISK, OCT+ flips it, OCT- flips it back."""
        api = self.api
        self.goto('MENU', 'hw')
        d = self.wait_data(lambda d: d['tab'] == 'HW', 'HW tab')
        names = [i['name'] for i in d['items']]
        before = self.item(d, 'DISK')['index']
        api.enc('algorithm', names.index('DISK'))
        api.tap('oct+')
        self.wait_data(lambda d: self.item(d, 'DISK')['index'] != before, 'DISK to flip')
        self.screenshot('MENU-hw-disk-flipped')
        api.tap('oct-')
        self.wait_data(lambda d: self.item(d, 'DISK')['index'] == before, 'DISK back')
        self.assertAlive()

    def test_06_theme_palette(self):
        """DISPLAY > THEME: OCT+ steps the palette (HOME's data has it), OCT- steps it back."""
        api = self.api
        before = self.goto('HOME')['data']['palette']
        img0 = self.screenshot('HOME-palette-before')
        self.goto('MENU', 'display')
        self.wait_data(lambda d: d['tab'] == 'DISPLAY', 'DISPLAY tab')
        api.tap('oct+')                          # row 0 is THEME
        self.screenshot('MENU-display-theme-next')
        after = self.goto('HOME')['data']['palette']
        img1 = self.screenshot('HOME-palette-after')
        self.assertEqual(after, (before + 1) % 10)
        self.assertNotEqual(img0.tobytes(), img1.tobytes(), 'the palette changed, the screen did not')
        self.goto('MENU', 'display')
        self.wait_data(lambda d: d['tab'] == 'DISPLAY', 'DISPLAY tab')
        api.tap('oct-')
        self.assertEqual(self.goto('HOME')['data']['palette'], before)
        self.assertAlive()

    def test_07_glo_tap_toggles_hints(self):
        """GLO tap: the page's controls over the body; again: gone. GLO hold still opens LOG."""
        api = self.api
        self.goto('HOME')
        api.wait_ms(300)
        img0 = self.screenshot('HOME-hints-off')
        api.tap('glo')
        api.wait_ms(300)
        img1 = self.screenshot('HOME-hints-on')
        self.assertNotEqual(img0.tobytes(), img1.tobytes(), 'GLO tap showed nothing')
        self.assertEqual(self.api.page()['page'], 'HOME', 'GLO tap left the page')
        api.tap('glo')
        api.wait_ms(300)
        img2 = self.screenshot('HOME-hints-off-again')
        self.assertEqual(img0.tobytes(), img2.tobytes(), 'the hints stayed after a second GLO tap')
        self.assertAlive()

    def test_08_demo_screens_and_knobs(self):
        """DEMO: SELECT steps its seven screens, then on to the next page; K1 moves the list."""
        api = self.api
        self.goto('DEMO')
        shots = [self.screenshot('DEMO-0')]
        for i in range(1, 7):
            api.enc('select', 1)
            api.wait_ms(200)
            self.assertEqual(api.page()['page'], 'DEMO', f'screen {i}: left DEMO')
            shots.append(self.screenshot(f'DEMO-{i}'))
        self.assertEqual(len({s.tobytes() for s in shots}), 7, 'two DEMO screens look the same')
        api.enc('select', -5)                    # back to LIST
        api.wait_ms(200)
        a = self.screenshot('DEMO-list')
        api.enc('knob1', 3)
        api.wait_ms(200)
        b = self.screenshot('DEMO-list-k1')
        self.assertNotEqual(a.tobytes(), b.tobytes(), 'K1 did not move the list')
        api.enc('select', 6)                     # past TOAST: the view's next page
        self.wait_page('WX')
        self.assertAlive()


class AudioTest(ModdApiCase):
    """With audio in the build (modd_emu), the note keys
    on HOME play the selected track."""

    def test_home_keys_play(self):
        api = self.api
        self.goto('HOME')
        api.set_engine('ANALOG')
        api.pot(1023)
        api.wait_ms(300)
        silence = api.capture()
        self.assertLess(rms(silence), 0.001, f'output not silent at rest (RMS {rms(silence):.5f})')
        held = api.record_note('C4')
        self.save_wav('home-c4-held.wav', held)
        self.assertGreater(rms(held), 0.01, f'no output while C4 is held on HOME (RMS {rms(held):.5f})')
        self.save_json('home-keys.json', {'rms_silence': rms(silence), 'rms_held': rms(held)})
        self.assertAlive()

if __name__ == '__main__':
    unittest.main()
