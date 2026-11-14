#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Unit tests for tools/codegen/gen_weather_art.py.

Replaces the old 1-bit glyph tests: the condition artwork is now indexed
pixel-art sprites with a shared palette and a per-condition sky gradient.
"""
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools' / 'codegen' / 'gen_weather_art.py'

EXPECTED_SCENES = [
    'SUN', 'PARTIAL_CLOUD', 'CLOUD', 'DRIZZLE',
    'RAIN', 'SNOW', 'THUNDER', 'FOG', 'UNKNOWN',
]


@pytest.fixture(scope='module')
def out():
    r = subprocess.run([sys.executable, str(SCRIPT), 'header'],
                       capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, f'generator failed: {r.stderr}'
    return r.stdout


def test_script_runs(out):
    assert out


def test_all_scene_names_present(out):
    for name in EXPECTED_SCENES:
        assert f'WTHR_ART_{name}' in out, f'missing scene {name}'


def test_enum_count(out):
    assert f'WTHR_ART_COUNT = {len(EXPECTED_SCENES)}' in out


def test_sprites_are_4bpp_and_correctly_sized(out):
    w = int(re.search(r'#define WTHR_ART_W (\d+)', out).group(1))
    h = int(re.search(r'#define WTHR_ART_H (\d+)', out).group(1))
    stride = int(re.search(r'#define WTHR_ART_STRIDE (\d+)', out).group(1))
    assert stride == w // 2, 'stride must be two pixels per byte'
    for name in EXPECTED_SCENES:
        m = re.search(rf'wthr_art_{name.lower()}\[(\d+)\]', out)
        assert m, f'{name} sprite array missing'
        assert int(m.group(1)) == stride * h, f'{name} sprite is the wrong size'


def test_palette_fits_in_a_nibble(out):
    m = re.search(r'wthr_pal\[(\d+)\]', out)
    assert m and int(m.group(1)) <= 16, 'palette must index in 4 bits'


def test_index_zero_is_transparent(out):
    assert re.search(r'0x0000,\s*/\*\s*0 TRANSPARENT', out)


def test_every_scene_has_a_sky_pair(out):
    block = out[out.index('wthr_sky'):]
    block = block[:block.index('};')]
    assert len(re.findall(r'\{ 0x[0-9A-F]{4}, 0x[0-9A-F]{4} \}', block)) == len(EXPECTED_SCENES)


def test_lookup_covers_the_wmo_codes(out):
    assert 'int weather_art_for_code(uint8_t wmo_code)' in out
    for code in (0, 2, 3, 45, 51, 61, 71, 95):
        assert re.search(rf'case\s+{code}: return WTHR_ART_', out), f'WMO {code} unmapped'
    assert 'default:  return WTHR_ART_UNKNOWN;' in out


def test_has_include_guard(out):
    assert '#ifndef WEATHER_ART_H' in out
    assert '#define WEATHER_ART_H' in out
    assert '#endif' in out


def test_checked_in_header_matches_generator(out):
    hdr = ROOT / 'firmware' / 'src' / 'ui' / 'themes' / 'felucca' / 'weather_art.h'
    assert hdr.exists(), 'weather_art.h is not checked in'
    drop = lambda s: '\n'.join(l for l in s.splitlines() if not l.startswith('/* Generated'))
    assert drop(hdr.read_text()) == drop(out), 'weather_art.h is stale; re-run the generator'
