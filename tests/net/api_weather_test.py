#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""API: the Weather page fetches and shows real data over the emulator's network.

Replaces the online half of the browser e2e_weather_test.py. fm1-live gives the
firmware a USB ethernet adapter and a NAT to the internet (--usb ethernet
--net internet); the modd_emu build has CONFIG_WEATHER with a fixed
location, so it asks api.open-meteo.com directly. The page is reached and
left over HTTP (/modd/wx and the HOME button through /api/input); the data
comes from the page's own state (/api/page), the proof of the open-meteo
request from the firmware log (/api/log), the picture from /api/screenshot.

"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from api_fixtures import ApiError, ApiTestCase


class WeatherCase(ApiTestCase):
    def wait_for_log(self, mark, line, what, timeout=30, prefix=False):
        """Until `line` is logged after `mark`; the failure lists what was logged instead."""
        seen = []

        def logged():
            seen[:] = self.api.log_since(mark)
            return any(l.startswith(line) if prefix else l == line for l in seen)
        try:
            self.api.wait_for(logged, timeout=timeout, poll=2.0, what=what)
        except ApiError:
            self.fail(f'no {what}; logged since: {[l for l in seen if not l.startswith("rpc:")]}')


class TestWeatherPage(WeatherCase):
    """Weather page with the network up."""


    def weather(self, timeout=60):
        """Open WX and wait until it holds valid data; returns the page data."""
        r = self.api.goto('wx')
        self.assertEqual(r['page'], 'WX')
        return self.api.wait_for(lambda: (d := self.api.page()['data'])['valid'] and d,
                                 timeout=timeout, poll=2.0, what='valid weather data')

    def test_weather_page_renders(self):
        """WX opens and draws without a crash."""
        r = self.api.goto('wx')
        self.assertEqual(r['page'], 'WX')
        self.assertIn('valid', r['data'])
        self.assertIn('fetching', r['data'])
        img = self.screenshot('WEATHER-page-renders')
        self.assertGreater(self.lit_pixels(img), 500, 'Weather page is blank')
        self.assertAlive()

    def test_weather_shows_data_over_the_network(self):
        """The firmware fetches real weather from open-meteo and draws the data card."""
        self.api.goto('home')
        mark = self.api.log_mark()
        d = self.weather()
        for key in ('temp_c', 'humidity', 'condition', 'wind_kmh', 'city', 'country'):
            self.assertIn(key, d)
        self.assertEqual(d['city'], 'New York', 'not the configured fixed location')
        self.assertLessEqual(d['humidity'], 100)
        self.assertTrue(d['condition'], 'no condition name')

        # lines logged since the mark, not the whole 400-line ring that other lines evict from
        log = [l for l in self.api.log_since(mark) if 'weather' in l]

        img = self.screenshot('WEATHER-data')
        coloured = self.coloured_pixels(img)
        self.save_json('weather-data.json', {'data': d, 'coloured_pixels': coloured,
                                             'log': log})
        self.assertGreater(coloured, 2000, f'no weather data card on the LCD ({coloured} coloured pixels)')
        self.assertAlive()

    def test_weather_play_button_refresh(self):
        """PLAY on WX starts a new fetch, which ends with fresh data."""
        before = self.weather()
        self.screenshot('WEATHER-before-refresh')
        mark = self.api.log_mark()
        self.api.tap('play')
        self.assertEqual(self.api.page()['page'], 'WX', 'PLAY left the Weather page')
        self.wait_for_log(mark, 'weather: fetch start', 'a new weather fetch after PLAY')
        after = self.api.wait_for(
            lambda: (d := self.api.page()['data'])['valid'] and d['updated_ms'] > before['updated_ms'] and d,
            timeout=60, poll=2.0, what='fresh weather data after PLAY')
        self.screenshot('WEATHER-after-refresh')
        self.save_json('weather-refresh.json', {'before': before, 'after': after})
        self.assertAlive()

    def test_weather_page_revisit(self):
        """Leave WX with the HOME button and come back: the page and its data survive."""
        self.weather()
        self.screenshot('WEATHER-visit-1')
        self.api.tap('home')
        self.assertEqual(self.api.page()['page'], 'HOME', 'HOME tap did not leave the Weather page')
        self.screenshot('WEATHER-left-to-HOME')
        d = self.weather(timeout=30)
        self.assertTrue(d['valid'])
        self.screenshot('WEATHER-visit-2')
        self.assertAlive()


_ocr = None


def ocr_text(img):
    """Text on the LCD (RapidOCR, one thread, upscaled 3x); None without RapidOCR."""
    global _ocr
    try:
        from rapidocr_onnxruntime import RapidOCR
        import numpy as np
    except ImportError:
        return None
    if _ocr is None:
        _ocr = RapidOCR(intra_op_num_threads=1, inter_op_num_threads=1)
    big = img.resize((img.width * 3, img.height * 3))
    result, _ = _ocr(np.array(big))
    return ' '.join(line[1] for line in result or [])


if __name__ == '__main__':
    unittest.main()
