#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: boot the firmware in fm1-live and drive every panel control over HTTP.

Replaces the browser e2e_boot_test.py. The firmware's REST API answers only
once lwIP, the USB ethernet link and the RPC server are up, so "it answers"
is itself a deep boot check; the LCD, the log and the page are checked on top.
The control test presses each control through /api/input and reads the
result back from the firmware: the debounced key state of the panel scan, the
page or track it changed, and the master volume it set.
"""
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase, FirmwareApi


class BootTest(ApiTestCase):
    """The firmware boots, draws, logs and serves its API."""

    def test_firmware_boots(self):
        info = self.api.info()
        self.assertEqual(info['firmware'], 'modd')
        self.assertTrue(info['version'])
        self.assertGreater(info['ip'], 0, 'no IP address: the network link did not come up')

        page = self.api.page()
        self.assertEqual(page['page'], 'HOME', f'boot did not end on HOME: {page}')

        img = self.screenshot('HOME-none-initial')
        lit = self.lit_pixels(img)
        self.assertGreater(lit, 500, f'LCD nearly blank after boot ({lit} lit pixels)')

        log = self.api.log()
        self.assertEqual(log[0], 'modd boot', f'log does not start with the boot line: {log[:3]}')
        for line in ('lcd: init OK', 'input: scan OK', 'audio: init OK'):
            self.assertIn(line, log, f'{line!r} missing from the boot log')

        self.save_json('boot.json', {'info': info, 'page': page, 'lit_pixels': lit,
                                     'boot_s': getattr(self.emu, 'boot_s', None), 'log': log})
        self.assertAlive()


class PanelControlsTest(ApiTestCase):
    """Every button, encoder, note key and the master pot reach the firmware."""

    def test_panel_controls_reach_firmware(self):
        api = self.api
        self.go_home()
        api.wait_idle()

        # buttons: held through the scan -> debounced bit set; let go -> cleared
        for i, name in enumerate(FirmwareApi.BUTTONS):
            with self.subTest(button=name):
                api.button_down(name)
                st = api.wait_for(lambda: (s := api.input_state())['buttons'] & (1 << i) and s,
                                  timeout=10, poll=0.1, what=f'{name} down')
                self.assertEqual(st['buttons'], 1 << i, f'{name}: other buttons down too: {st}')
                api.button_up(name)
                st = api.wait_idle()
                self.assertEqual(st['buttons'], 0, f'{name} still down: {st}')
                self.go_home()          # a press may open a view; start each one from HOME

        # a long press opens a view: HOME held -> Settings, HOME tapped -> back
        api.hold('home', 700)
        self.assertEqual(api.page()['page'], 'MENU', 'HOME hold did not open Settings')
        tab = api.page()['data']['tab']
        api.enc('select', 1)
        self.assertNotEqual(api.page()['data']['tab'], tab, 'SELECT did not change the Settings tab')
        api.tap('home')
        self.assertEqual(api.page()['page'], 'HOME', 'HOME tap did not return HOME')

        # SELECT on HOME: modd-native picks the track; Felucca adjusts BPM
        is_felucca = 'felucca' in (os.environ.get('MODD_API_DEFCONFIG', '') or self.DEFCONFIG)
        if is_felucca:
            bpm_before = api.page()['data']['bpm']
            api.enc('select', 1)
            bpm_after = api.page()['data']['bpm']
            self.assertGreater(bpm_after, bpm_before, 'SELECT did not increase BPM')
            api.enc('select', -1)
            self.assertEqual(api.page()['data']['bpm'], bpm_before)
        else:
            sel = api.page()['data']['selected_track']
            step = 1 if sel < 3 else -1
            api.enc('select', step)
            self.assertEqual(api.page()['data']['selected_track'], sel + step)
            api.enc('select', -step)
            self.assertEqual(api.page()['data']['selected_track'], sel)

        # every other encoder: the detents are taken by the UI (idle = none left)
        for name in FirmwareApi.ENCODERS[1:]:
            with self.subTest(encoder=name):
                api.enc(name, 2)
                api.enc(name, -2)
        self.assertEqual(api.page()['page'], 'HOME')

        # hold EDIT while turning KNOB1: the combination arrives with EDIT down
        api.button_down('edit')
        api.wait_for(lambda: api.input_state()['buttons'] & (1 << 6), timeout=10, poll=0.1, what='EDIT down')
        api.enc('knob1', 1)
        st = api.input_state()
        self.assertTrue(st['buttons'] & (1 << 6), f'EDIT let go during the turn: {st}')
        api.button_up('edit')
        api.wait_idle()
        self.go_home()

        # note keys: the scan sees them, and they sound
        api.pot(1023)
        for note, bit in (('C4', 7), ('C#4', 8), ('F5', 24)):
            with self.subTest(note=note):
                api.key_down(note)
                st = api.wait_for(lambda: (s := api.input_state())['notes'] & (1 << bit) and s,
                                  timeout=10, poll=0.1, what=f'{note} down')
                self.assertEqual(st['notes'], 1 << bit)
                api.wait_ms(200)
                level = api.audio()
                self.assertGreater(level['rms'], 100, f'{note} held but silent: {level}')
                api.key_up(note)
                self.assertEqual(api.wait_idle()['notes'], 0)
        self.screenshot('HOME-note-noteOn')

        # master pot: the firmware's volume follows the value we hold it at (through the
        # main loop's smoothing filter, so wait for it to settle)
        api.pot(0)
        api.wait_for(lambda: api.audio()['master_q12'] == 0, timeout=10, poll=0.3,
                     what='master volume 0 after pot 0')
        api.pot(1023)
        api.wait_for(lambda: api.audio()['master_q12'] > 3900, timeout=10, poll=0.3,
                     what='master volume near full after pot 1023')
        api.pot_release()
        self.assertEqual(api.input_state()['pot'], -1)
        self.assertAlive()


if __name__ == '__main__':
    unittest.main()
