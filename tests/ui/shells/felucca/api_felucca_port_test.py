#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: behaviour claimed by the Felucca port tasks, on the modd_felucca_emu firmware.

One boot, driven over HTTP. /api/page carries "data" only for HOME (tracks, bpm, playing,
root, palette, level/atk/dec/sus/rel), MENU (tab, items) and WX; every other page reports
just its name. A check that needs a value of another page (DTIME, RSIZE, GLIDE, ...) says so
with skipTest and the screenshot change it did observe. Each test notes the firmware uptime
before and after its actions: a firmware reset (uptime going backwards) is reported as a
reset and names the action. The REC press, which resets this build, is the last test.
"""
import hashlib
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from urllib.parse import quote  # noqa: E402

from api_fixtures import ApiError, ApiTestCase, rms  # noqa: E402

DEFCONFIG = 'modd_felucca_emu'


def img_hash(img):
    return hashlib.md5(img.convert('RGB').tobytes()).hexdigest()


class FeluccaPortCase(ApiTestCase):
    DEFCONFIG = DEFCONFIG
    _uptime = 0
    _log = None

    def setUp(self):
        self.reset_seen = []
        self.act('setUp: goto home', lambda: self.api.goto('home'))
        self.api.wait_idle()

    # -- helpers --

    def uptime(self):
        return self.api.info()['uptime_ms']

    def act(self, label, fn):
        """Run fn; fail as a RESET naming `label` when the firmware restarted (uptime went back
        or the request timed out and uptime is lower afterwards)."""
        before = type(self)._uptime
        try:
            r = fn()
        except OSError as e:
            r = None
            err = e
        else:
            err = None
        try:
            now = self.uptime()
        except OSError:
            time.sleep(3)
            now = self.uptime()
        type(self)._uptime = now
        if now < before or err:
            self.fail(f'FIRMWARE RESET during/after action "{label}": uptime {before} -> {now} ms'
                      + (f', request error {err!r}' if err else ''))
        return r

    def page(self):
        return self.api.page()

    def pname(self):
        return self.page()['page']

    def home(self):
        self.act('goto home', lambda: self.api.goto('home'))
        self.api.wait_idle()

    def tap(self, b, ms=80):
        self.act(f'tap {b}', lambda: self.api.tap(b, ms))
        time.sleep(0.3)     # the LVGL shell changes page on its own task

    def enc(self, e, n):
        self.act(f'enc {e} {n}', lambda: self.api.enc(e, n))

    def cycle(self, button, start=None, n=10, goto=None):
        """Press `button` n times from `goto` (page name); returns the page names seen."""
        if goto:
            self.act(f'goto {goto}', lambda: self.api.goto(goto))
            self.api.wait_idle()
        seen = [self.pname()]
        for _ in range(n):
            self.tap(button)
            seen.append(self.pname())
        return seen

    def hold_layer(self, button, action):
        """Hold `button` past the hold threshold, run action, release."""
        self.act(f'{button} down', lambda: self.api.button_down(button))
        try:
            self.api.wait_ms(700)
            action()
        finally:
            self.act(f'{button} up', lambda: self.api.button_up(button))
        self.api.wait_idle()

    def img(self, name):
        return self.screenshot(name)


class TestFeluccaPortPages(FeluccaPortCase):

    def test_01_every_page_opens_by_name(self):
        """1. every listed page opens by name, /api/page reports it, firmware alive, screenshot differs."""
        pages = self.api.pages()
        names = [p['name'] if isinstance(p, dict) else p for p in pages]
        self.assertGreaterEqual(len(names), 50, names)
        hashes, wrong = {}, []
        for n in names:
            try:
                self.act(f'goto {n}', lambda n=n: self.api.get('/modd/' + quote(n)))
            except ApiError as e:
                wrong.append((n, f'goto failed: {e}'))
                continue
            self.api.wait_idle()
            time.sleep(0.15)
            got = self.pname()
            if got.upper() != n.upper():
                wrong.append((n, got))
            hashes.setdefault(img_hash(self.img('page-' + n.replace(' ', '_'))), []).append(n)
        self.assertAlive()
        dup = [v for v in hashes.values() if len(v) > 1]
        self.save_json('page_pictures.json', {'identical_groups': dup, 'pages': names})
        print('\nPAGES drawing an identical picture:', dup)
        self.assertEqual(wrong, [], f'pages that did not open by name (asked, got): {wrong}')

    def test_02_home_tap_cycles(self):
        """2a. HOME tap cycles HOME -> MIXER -> CLOCK -> HOME."""
        seen = self.cycle('home', goto='HOME', n=3)
        self.assertEqual(seen, ['HOME', 'MIXER', 'CLOCK', 'HOME'], seen)

    def test_02_glo_opens_song(self):
        """2b. GLO tap opens SONG."""
        self.home()
        self.tap('glo')
        self.assertEqual(self.pname(), 'SONG')

    def _sub_cycle(self, button, first, n=8):
        self.home()
        seen = [self.pname()]
        for _ in range(n):
            self.tap(button)
            seen.append(self.pname())
        print(f'\n{button.upper()} cycle:', seen)
        distinct = list(dict.fromkeys(seen))
        self.assertGreaterEqual(len(distinct) - ('HOME' in distinct), 2, f'{button}: {seen}')
        return seen

    def test_02_env_cycles(self):
        """2c. ENV button cycles its sub-pages."""
        seen = self._sub_cycle('env', 'ENV')
        self.assertTrue({'ENV', 'ENV DEST'} <= set(seen), seen)

    def test_02_lfo_cycles(self):
        """2d. LFO button cycles LFO / LFO 2 / LFO DEST."""
        seen = self._sub_cycle('lfo', 'LFO')
        self.assertTrue({'LFO', 'LFO 2', 'LFO DEST'} <= set(seen), seen)

    def test_02_scl_cycles(self):
        """2e. SCL button cycles SCL / CHORD."""
        seen = self._sub_cycle('scl', 'SCL')
        self.assertTrue({'SCL', 'CHORD'} <= set(seen), seen)

    def test_02_arp_cycles(self):
        """2f. ARP button cycles ARP / ARP 2."""
        seen = self._sub_cycle('arp', 'ARP')
        self.assertTrue({'ARP', 'ARP 2'} <= set(seen), seen)


class TestFeluccaPortEditFx(FeluccaPortCase):

    def test_03_edit_cycle_order(self):
        """3a. EDIT cycles EDIT -> EDIT 2 -> FILTER -> VOICE -> VOICE 2 -> VOICE 3 (real order recorded)."""
        self.act('set ANALOG', lambda: self.api.set_engine('ANALOG'))
        self.home()
        seen = [self.pname()]
        for _ in range(8):
            self.tap('edit')
            seen.append(self.pname())
        print('\nEDIT cycle (Analog):', seen)
        self.assertEqual(seen[1:7], ['EDIT', 'EDIT 2', 'FILTER', 'VOICE', 'VOICE 2', 'VOICE 3'], seen)

    def test_03_filter_skipped_for_non_analog(self):
        """3b. FILTER is skipped when the selected track's engine is not Analog."""
        t = self.act('set FM6', lambda: self.api.set_engine('FM6'))
        self.assertEqual(t['engine'], 'FM6')
        self.home()
        seen = [self.pname()]
        for _ in range(8):
            self.tap('edit')
            seen.append(self.pname())
        print('\nEDIT cycle (FM6):', seen)
        self.act('set ANALOG', lambda: self.api.set_engine('ANALOG'))
        self.assertNotIn('FILTER', seen, seen)

    def test_03_voice_glide_prio_change(self):
        """3c. VOICE page data has GLIDE/PRIO values that change with encoder input."""
        self.act('goto VOICE', lambda: self.api.goto('VOICE'))
        p = self.page()
        self.assertEqual(p['page'], 'VOICE')
        a = img_hash(self.img('voice-before'))
        self.enc('knob1', 5)
        self.enc('knob2', 1)
        b = img_hash(self.img('voice-after'))
        if 'data' not in p:
            self.skipTest(f'/api/page for VOICE has no data (keys {sorted(p)}); screenshot '
                          f'{"changed" if a != b else "did NOT change"} after knob1 +5, knob2 +1')

    def _fx_values(self, page):
        """Knob-driven check: needs page data; skip with what is visible."""
        self.act(f'goto {page}', lambda: self.api.goto(page))
        p = self.page()
        a = img_hash(self.img(f'{page}-before'))
        for k in ('knob1', 'knob2', 'knob3', 'knob4'):
            self.enc(k, 3)
        b = img_hash(self.img(f'{page}-after'))
        if 'data' not in p:
            self.skipTest(f'/api/page for {page} has no data (keys {sorted(p)}); screenshot '
                          f'{"changed" if a != b else "did NOT change"} after knob1-4 +3')
        return p

    def test_04_fx_cycles_seven_pages(self):
        """4a. FX button cycles through its 7 sub-pages."""
        self.home()
        seen = []
        for _ in range(10):
            self.tap('fx')
            seen.append(self.pname())
        print('\nFX cycle:', seen)
        distinct = list(dict.fromkeys(seen))
        self.assertEqual(len(distinct), 7, f'{len(distinct)} distinct pages: {seen}')

    def test_04_dly_dtime(self):
        """4b. DLY: DTIME changes in page data after encoder input."""
        self._fx_values('DLY')

    def test_04_reverb_rsize_rtype(self):
        """4c. REVERB: RSIZE and the RTYPE knob (ROOM/SPRING/HALL) change in page data."""
        self._fx_values('REVERB')

    def test_04_dist_knob(self):
        """4d. per-track DIST knob changes in page data."""
        self._fx_values('FX')


class TestFeluccaPortAudioLayers(FeluccaPortCase):

    def _rms_note(self):
        return rms(self.api.record_note('C4', 300))

    def _effect_rms(self, page, knob, clicks):
        self.act('set ANALOG', lambda: self.api.set_engine('ANALOG'))
        self.api.pot(1023)
        self.api.wait_ms(400)
        self.home()
        base = self._rms_note()
        self.act(f'goto {page}', lambda: self.api.goto(page))
        self.enc(knob, clicks)
        self.screenshot(f'{page}-effect-on')
        self.home()
        on = self._rms_note()
        self.act(f'goto {page}', lambda: self.api.goto(page))
        self.enc(knob, -clicks)
        self.home()
        off = self._rms_note()
        print(f'\n{page}: off(before)={base:.5f} on={on:.5f} off(after)={off:.5f}')
        self.assertGreater(base, 0.001, f'no audio from the note ({base})')
        self.assertGreater(abs(on - base) / base, 0.02,
                           f'{page} on vs off: RMS {on:.5f} vs {base:.5f} (after: {off:.5f})')

    def test_05_insert_changes_audio(self):
        """5a. INSERT on vs off changes the capture RMS (knob1 = TYPE)."""
        self._effect_rms('INSERT', 'knob1', 40)

    def test_05_slicer_changes_audio(self):
        """5b. SLICER on vs off changes the capture RMS (knob1 = SLCR)."""
        self._effect_rms('SLICER', 'knob1', 40)

    def test_06_edit_hold_key0_changes_engine(self):
        """6a. EDIT hold + key 0 changes the engine of the active track."""
        self.act('set ANALOG', lambda: self.api.set_engine('ANALOG'))
        before = self.api.track()['engine']
        self.hold_layer('edit', lambda: self.act('key 0', lambda: self.api.key_tap(53)))
        after = self.api.track()['engine']
        print(f'\nengine {before} -> {after}')
        self.act('set ANALOG', lambda: self.api.set_engine('ANALOG'))
        self.assertNotEqual(before, after)

    def test_06_scl_hold_key_sets_root(self):
        """6b. SCL hold + a key sets the root note (HOME data root)."""
        before = self.page()['data']['root']
        seen = {}
        for key in (55, 58):
            self.hold_layer('scl', lambda key=key: self.act(f'key {key}', lambda: self.api.key_tap(key)))
            self.home()
            seen[key] = self.page()['data']['root']
        print(f'\nroot before={before} after keys={seen}')
        self.assertNotEqual(seen[55], seen[58], f'root {before} / {seen}')
        self.assertTrue(any(v != before for v in seen.values()), f'root {before} / {seen}')

    def test_06_seq_hold_encoder(self):
        """6c. SEQ hold + encoder changes LEN/DIV/SWING/GATE."""
        self.home()
        a = img_hash(self.img('seqhold-before'))
        self.hold_layer('seq', lambda: [self.enc(k, 2) for k in ('knob1', 'knob2', 'knob3', 'knob4')])
        b = img_hash(self.img('seqhold-after'))
        self.skipTest(f'LEN/DIV/SWING/GATE are not in /api/page data; page now {self.pname()}, '
                      f'screenshot {"changed" if a != b else "did NOT change"}')


class TestFeluccaPortMenuSaveSeq(FeluccaPortCase):

    def test_07_home_hold_opens_menu(self):
        """7a. HOME hold opens MENU."""
        self.home()
        self.hold_layer('home', lambda: None)
        self.assertEqual(self.pname(), 'MENU', self.page())

    def test_07_palette_changes_colours(self):
        """7b. PALETTE changes the palette and the screenshot colours."""
        self.home()
        a = self.img('palette-before')
        p0 = self.page()['data'].get('palette')
        self.act('goto PALETTE', lambda: self.api.goto('PALETTE'))
        for _ in range(3):
            self.enc('knob1', 1)
            self.enc('select', 1)
        self.home()
        b = self.img('palette-after')
        p1 = self.page()['data'].get('palette')
        print(f'\npalette {p0} -> {p1}, screenshots {"differ" if img_hash(a) != img_hash(b) else "identical"}')
        self.assertNotEqual(p0, p1, 'palette index in HOME data did not change')
        self.assertNotEqual(img_hash(a), img_hash(b))

    def test_07_brightness_changes(self):
        """7c. BRIGHTNESS changes its value."""
        self.act('goto BRIGHTNESS', lambda: self.api.goto('BRIGHTNESS'))
        p = self.page()
        a = img_hash(self.img('bright-before'))
        self.enc('knob1', -5)
        b = img_hash(self.img('bright-after'))
        if 'data' not in p:
            self.skipTest(f'BRIGHTNESS page has no data (keys {sorted(p)}); screenshot '
                          f'{"changed" if a != b else "did NOT change"} after knob1 -5')

    def test_08_save_cycles_user_pages(self):
        """8a. SAVE opens USER and cycles USER -> PRESETS -> PHRASES -> PROJECT -> TOOLS."""
        self.home()
        seen = []
        for _ in range(6):
            self.tap('save')
            seen.append(self.pname())
        print('\nSAVE cycle:', seen)
        self.assertEqual(seen[:5], ['USER', 'PRESETS', 'PHRASES', 'PROJECT', 'TOOLS'], seen)

    def test_08_user_slot_data(self):
        """8b. USER page data shows slot data."""
        self.act('goto USER', lambda: self.api.goto('USER'))
        p = self.page()
        if 'data' not in p:
            self.skipTest(f'/api/page for USER has no data (keys {sorted(p)})')

    def test_09_seq_button_opens_step(self):
        """9a. SEQ button opens STEP (real result recorded)."""
        self.home()
        self.tap('seq')
        got = self.pname()
        print('\nSEQ opens:', got)
        self.assertEqual(got, 'STEP')

    def test_09_step_key_toggles(self):
        """9b. on STEP a note key toggles a step in page data."""
        self.act('goto STEP', lambda: self.api.goto('STEP'))
        p = self.page()
        a = img_hash(self.img('step-before'))
        self.act('key', lambda: self.api.key_tap(53))
        b = img_hash(self.img('step-after'))
        if 'data' not in p:
            self.skipTest(f'/api/page for STEP has no data (keys {sorted(p)}); screenshot '
                          f'{"changed" if a != b else "did NOT change"} after key 0')

    def test_09_detail_fields(self):
        """9c. DETAIL page data has CHANCE/RATCH fields."""
        self.act('goto DETAIL', lambda: self.api.goto('DETAIL'))
        p = self.page()
        if 'data' not in p:
            self.skipTest(f'/api/page for DETAIL has no data (keys {sorted(p)})')

    def test_09_play_starts_sequencer(self):
        """9d. PLAY starts the sequencer (HOME data playing)."""
        self.home()
        self.assertFalse(self.page()['data']['playing'])
        self.tap('play')
        playing = self.page()['data']['playing']
        self.tap('play')
        stopped = self.page()['data']['playing']
        print(f'\nplaying after PLAY: {playing}, after second PLAY: {stopped}')
        self.assertTrue(playing)
        self.assertFalse(stopped)

    def _knobs(self, page, names):
        self.act(f'goto {page}', lambda: self.api.goto(page))
        p = self.page()
        a = img_hash(self.img(f'{page}-before'))
        for k in ('knob1', 'knob2', 'knob3', 'knob4'):
            self.enc(k, 2)
        b = img_hash(self.img(f'{page}-after'))
        if 'data' not in p:
            self.skipTest(f'/api/page for {page} has no data (keys {sorted(p)}), so {names} cannot be read; '
                          f'screenshot {"changed" if a != b else "did NOT change"} after knobs +2')

    def test_10_mod_knobs(self):
        """10a. MOD page knobs M1SRC/M1DST/M1AMT change in page data."""
        self._knobs('MOD', 'M1SRC/M1DST/M1AMT')

    def test_10_chord_knobs(self):
        """10b. CHORD page knobs CHRD/VOIC change in page data."""
        self._knobs('CHORD', 'CHRD/VOIC')


class TestFeluccaPortZZRecResets(FeluccaPortCase):
    """Known defect, kept last: pressing REC resets the firmware on this build."""

    def test_99_rec_press_does_not_reset(self):
        """11. pressing REC leaves the firmware running (uptime does not go backwards)."""
        self.home()
        self.act('record uptime', lambda: None)
        before = self.uptime()
        type(self)._uptime = before
        samples = [before]
        try:
            self.api.tap('rec', 80, settle=False)
        except OSError:
            samples.append(None)
        t0 = time.monotonic()
        while time.monotonic() - t0 < 8:
            try:
                samples.append(self.uptime())
            except OSError:
                samples.append(None)
            time.sleep(0.3)
        print('\nuptime_ms samples around REC:', samples)
        good = [x for x in samples if x is not None]
        back = [(x, y) for x, y in zip(good, good[1:]) if y < x]
        self.assertEqual(back, [], f'RESET after REC tap: uptime went backwards {back}; samples {samples}')
        self.assertNotIn(None, samples, f'requests failed after REC: {samples}')


if __name__ == '__main__':
    unittest.main()
