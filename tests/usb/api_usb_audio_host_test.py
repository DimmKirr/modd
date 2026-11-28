#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: the USB audio host ring buffer, checked by FFT.

/api/usb/audio/ring/arm/{rate}/{depth} fakes UHOST_UAC_READY so the ISR feed
hook writes into the ring.  /api/usb/audio/ring returns the ring contents
(base64 PCM + metadata).  /api/usb/audio/ring/disarm restores idle state.
"""
import base64
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiTestCase

ARM = '/api/usb/audio/ring/arm/{rate}/{depth}'
DISARM = '/api/usb/audio/ring/disarm'
RING = '/api/usb/audio/ring'


def _fft_peak_hz(samples, rate):
    """Return the frequency of the dominant FFT bin, with parabolic interpolation."""
    import numpy as np
    n = len(samples)
    if n < 64:
        return 0.0
    w = np.hanning(n)
    spectrum = np.abs(np.fft.rfft(np.asarray(samples, dtype=float) * w))
    spectrum[0] = 0
    k = int(np.argmax(spectrum))
    if 0 < k < len(spectrum) - 1:
        alpha = float(spectrum[k - 1])
        beta = float(spectrum[k])
        gamma = float(spectrum[k + 1])
        denom = alpha - 2.0 * beta + gamma
        if abs(denom) > 1e-12:
            p = 0.5 * (alpha - gamma) / denom
            k = k + p
    return k * rate / n


def _decode_s16le(data_bytes, channels):
    n_samples = len(data_bytes) // (2 * channels)
    fmt = f'<{n_samples * channels}h'
    raw = struct.unpack(fmt, data_bytes[:n_samples * 2 * channels])
    return [raw[i * channels] for i in range(n_samples)]


def _decode_s24le(data_bytes, channels):
    bps = 3
    frame_size = bps * channels
    n_samples = len(data_bytes) // frame_size
    left = []
    for i in range(n_samples):
        off = i * frame_size
        b0, b1, b2 = data_bytes[off], data_bytes[off + 1], data_bytes[off + 2]
        val = b0 | (b1 << 8) | (b2 << 16)
        if val >= 0x800000:
            val -= 0x1000000
        left.append(val)
    return left


def _decode_s32le(data_bytes, channels):
    n_samples = len(data_bytes) // (4 * channels)
    raw = struct.unpack(f'<{n_samples * channels}i', data_bytes[:n_samples * 4 * channels])
    return [raw[i * channels] for i in range(n_samples)]


DECODERS = {'s16le': _decode_s16le, 's24le': _decode_s24le, 's32le': _decode_s32le}


def _decode(ring):
    data = base64.b64decode(ring['data'])
    return DECODERS[ring['format']](data, ring['channels'])


class TestUsbAudioHost16bit(ApiTestCase):
    """44.1 kHz 16-bit: native sample rate, no SRC."""
    DEFCONFIG = 'modd_emu'

    def setUp(self):
        super().setUp()
        self.api.get(ARM.format(rate=44100, depth=16), timeout=10)

    def tearDown(self):
        try:
            self.api.get(DISARM, timeout=30)
        except Exception:
            pass
        super().tearDown()

    def test_ring_metadata_16bit(self):
        ring = self.api.get(RING, timeout=10)
        self.assertEqual(ring['rate'], 44100)
        self.assertEqual(ring['format'], 's16le')
        self.assertEqual(ring['channels'], 2)
        self.assertEqual(ring['bit_depth'], 16)

    def test_ring_440hz_16bit(self):
        """Play A4, read ring, FFT must show 440 Hz +-5 Hz."""
        api = self.api
        self.go_home()
        api.pot(1023)
        api.wait_ms(200)
        t0 = api.key_down([69])['ms']
        try:
            api.wait_ms(500, since=t0)
            ring = api.get(RING, timeout=60)
        finally:
            api.key_up([69])
            api.wait_idle(first_sleep=0.1)
        self.assertGreater(ring['wr'], ring['rd'], 'ring empty after note')
        samples = _decode(ring)
        self.assertGreater(len(samples), 64, 'too few samples for FFT')
        peak = _fft_peak_hz(samples, ring['rate'])
        self.assertAlmostEqual(peak, 440.0, delta=2.0,
                               msg=f'peak {peak:.1f} Hz, expected 440 +-2')


class TestUsbAudioHost24bit(ApiTestCase):
    """48 kHz 24-bit: exercises 44.1->48 kHz SRC path."""
    DEFCONFIG = 'modd_emu'

    def setUp(self):
        super().setUp()
        self.api.get(ARM.format(rate=48000, depth=24), timeout=10)

    def tearDown(self):
        try:
            self.api.get(DISARM, timeout=30)
        except Exception:
            pass
        super().tearDown()

    def test_ring_metadata_24bit(self):
        ring = self.api.get(RING, timeout=10)
        self.assertEqual(ring['rate'], 48000)
        self.assertEqual(ring['format'], 's24le')
        self.assertEqual(ring['channels'], 2)
        self.assertEqual(ring['bit_depth'], 24)

    def test_src_440hz_24bit(self):
        """Play A4 at 44.1 kHz render, SRC to 48 kHz, FFT must show 440 Hz."""
        api = self.api
        self.go_home()
        api.pot(1023)
        api.wait_ms(200)
        t0 = api.key_down([69])['ms']
        try:
            api.wait_ms(500, since=t0)
            ring = api.get(RING, timeout=60)
        finally:
            api.key_up([69])
            api.wait_idle(first_sleep=0.1)
        self.assertGreater(ring['wr'], ring['rd'], 'ring empty after note')
        samples = _decode(ring)
        self.assertGreater(len(samples), 64, 'too few samples for FFT')
        peak = _fft_peak_hz(samples, ring['rate'])
        self.assertAlmostEqual(peak, 440.0, delta=2.0,
                               msg=f'peak {peak:.1f} Hz, expected 440 +-2')


class TestUsbDacSettingsMenu(ApiTestCase):
    """Settings menu: 'USB DAC' cycles OFF -> enabled modes and back."""
    DEFCONFIG = 'modd_emu'

    def _goto(self, name, sub=None):
        r = self.api.goto(name, sub)
        return self.api.wait_for(
            lambda: (p := self.api.page())['page'] == name and p,
            timeout=10, poll=0.5, what=f'page {name}')

    def _wait_data(self, pred, what, timeout=10):
        return self.api.wait_for(
            lambda: (d := self.api.page()['data']) and pred(d) and d,
            timeout=timeout, poll=0.5, what=what)

    def _item(self, data, name):
        items = {i['name']: i for i in data['items']}
        self.assertIn(name, items, f'{name} not among {list(items)}')
        return items[name]

    def test_usb_dac_cycles_modes(self):
        """HW tab: USB DAC starts at a mode, OCT+ cycles it, OFF is reachable."""
        api = self.api
        self._goto('MENU', 'hw')
        d = self._wait_data(lambda d: d['tab'] == 'HW', 'HW tab')
        item = self._item(d, 'USB DAC')
        names = [i['name'] for i in d['items']]
        before = item['index']

        api.enc('algorithm', names.index('USB DAC'))
        api.tap('oct+')
        d2 = self._wait_data(
            lambda d: self._item(d, 'USB DAC')['index'] != before,
            'USB DAC to change')
        after = self._item(d2, 'USB DAC')['index']
        self.assertNotEqual(before, after, 'OCT+ did not change USB DAC mode')

        api.tap('oct-')
        self._wait_data(
            lambda d: self._item(d, 'USB DAC')['index'] == before,
            'USB DAC back')

    def test_usb_dac_has_off_mode(self):
        """USB DAC value list includes OFF (index 0)."""
        api = self.api
        self._goto('MENU', 'hw')
        d = self._wait_data(lambda d: d['tab'] == 'HW', 'HW tab')
        item = self._item(d, 'USB DAC')
        self.assertIn('value', item)
        api.enc('algorithm', [i['name'] for i in d['items']].index('USB DAC'))
        while item['index'] > 0:
            api.tap('oct-')
            d = self._wait_data(
                lambda d: self._item(d, 'USB DAC')['index'] == 0,
                'USB DAC to OFF')
            item = self._item(d, 'USB DAC')
        self.assertEqual(item['value'], 'OFF')


if __name__ == '__main__':
    unittest.main()
