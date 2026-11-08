#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Unit tests for gen_ui_map.py: param_slot_t and fparams_def_t parsing.

Verifies that the codegen parser extracts correct knob labels from both
the new declarative param_slot_t arrays and the legacy fparams_def_t +
fparams_knob() pattern, so themes with different control abstractions
produce the same controls table.
"""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'codegen'))

from gen_ui_map import parse_page_controls, parse_page_details


@pytest.fixture()
def felucca_dir():
    d = ROOT / 'firmware' / 'src' / 'ui' / 'themes' / 'felucca'
    if not d.is_dir():
        pytest.skip('felucca theme not found')
    return d


class TestParamSlotParsing:
    """parse_page_controls extracts labels from param_slot_t arrays."""

    def test_env_slots_parsed(self, felucca_dir):
        controls = parse_page_controls(felucca_dir)
        env = controls.get('FPAGE_ENV')
        assert env is not None, 'FPAGE_ENV not found in controls'
        knobs = env['knobs']
        assert knobs[2] == 'ATK', f'K1 should be ATK, got {knobs.get(2)}'
        assert knobs[3] == 'DEC', f'K2 should be DEC, got {knobs.get(3)}'
        assert knobs[4] == 'SUS', f'K3 should be SUS, got {knobs.get(4)}'
        assert knobs[5] == 'REL', f'K4 should be REL, got {knobs.get(5)}'

    def test_lfo_slots_parsed(self, felucca_dir):
        controls = parse_page_controls(felucca_dir)
        lfo = controls.get('FPAGE_LFO')
        assert lfo is not None
        knobs = lfo['knobs']
        assert knobs[2] == 'LFO'
        assert knobs[3] == 'RATE'
        assert knobs[4] == 'SHAPE'
        assert knobs[5] == 'DELAY'

    def test_fx_slots_parsed(self, felucca_dir):
        controls = parse_page_controls(felucca_dir)
        fx = controls.get('FPAGE_FX')
        assert fx is not None
        knobs = fx['knobs']
        assert knobs[2] == 'DST'
        assert knobs[3] == 'CHO'
        assert knobs[4] == 'DLY'
        assert knobs[5] == 'REV'

    def test_scl_slots_parsed(self, felucca_dir):
        controls = parse_page_controls(felucca_dir)
        scl = controls.get('FPAGE_SCL')
        assert scl is not None
        knobs = scl['knobs']
        assert knobs[2] == 'ROOT'
        assert knobs[3] == 'SCL'
        assert knobs[4] == 'OCT'
        assert knobs[5] == 'TRN'

    def test_arp_slots_parsed(self, felucca_dir):
        controls = parse_page_controls(felucca_dir)
        arp = controls.get('FPAGE_ARP')
        assert arp is not None
        knobs = arp['knobs']
        assert knobs[2] == 'MODE'
        assert knobs[3] == 'TIME'
        assert knobs[4] == 'RANGE'
        assert knobs[5] == 'GATE'

    def test_voice_slots_parsed(self, felucca_dir):
        controls = parse_page_controls(felucca_dir)
        voice = controls.get('FPAGE_VOICE')
        assert voice is not None
        knobs = voice['knobs']
        assert knobs[2] == 'VCE'
        assert knobs[3] == 'HOLD'
        assert knobs[4] == 'GLD'
        assert knobs[5] == 'UNI'

    def test_mixer_slots_parsed_with_algo(self, felucca_dir):
        controls = parse_page_controls(felucca_dir)
        mixer = controls.get('FPAGE_MIXER')
        assert mixer is not None, 'FPAGE_MIXER not found in controls'
        knobs = mixer['knobs']
        assert knobs[1] == 'T1 LVL', f'ALGO should be T1 LVL, got {knobs.get(1)}'
        assert knobs[2] == 'T2 LVL', f'K1 should be T2 LVL, got {knobs.get(2)}'
        assert knobs[3] == 'T3 LVL', f'K2 should be T3 LVL, got {knobs.get(3)}'
        assert knobs[4] == 'T4 LVL', f'K3 should be T4 LVL, got {knobs.get(4)}'

    def test_edit_keeps_imperative_handler(self, felucca_dir):
        """EDIT page uses a custom .knob handler, not param_slot_t."""
        controls = parse_page_controls(felucca_dir)
        edit = controls.get('FPAGE_EDIT')
        assert edit is not None
        knobs = edit['knobs']
        assert 2 in knobs or 3 in knobs, 'EDIT should have imperative knob bindings'

    def test_all_param_pages_have_four_knobs(self, felucca_dir):
        """Every param page (ENV-VOICE) declares exactly 4 knob bindings on K1-K4."""
        controls = parse_page_controls(felucca_dir)
        for page in ('FPAGE_ENV', 'FPAGE_LFO', 'FPAGE_FX', 'FPAGE_SCL',
                     'FPAGE_ARP', 'FPAGE_VOICE'):
            c = controls.get(page)
            assert c is not None, f'{page} missing from controls'
            k14 = {k: v for k, v in c['knobs'].items() if 2 <= k <= 5}
            assert len(k14) == 4, f'{page} should have 4 knobs on K1-K4, got {k14}'

    def test_page_details_match_controls(self, felucca_dir):
        """Every page with knob_map should also appear in page_details."""
        controls = parse_page_controls(felucca_dir)
        details = parse_page_details(felucca_dir)
        for sym in controls:
            assert sym in details, f'{sym} has controls but no page_details entry'
