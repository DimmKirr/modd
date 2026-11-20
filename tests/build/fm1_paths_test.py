#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""The one toolchain rule: open unless USE_OPEN_TOOLCHAIN is exactly 0 (tools/build/fm1_paths.py)."""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
sys.path.insert(0, str(ROOT / 'tests'))
import api_fixtures  # noqa: E402
import build_fixtures  # noqa: E402
import fm1_paths  # noqa: E402

OVERRIDES = ('MODD_API_FWSC', 'MODD_FWSC', 'MODD_API_DEFCONFIG', 'MODD_DEFCONFIG')


def _set(monkeypatch, value):
    if value is None:
        monkeypatch.delenv('USE_OPEN_TOOLCHAIN', raising=False)
    else:
        monkeypatch.setenv('USE_OPEN_TOOLCHAIN', value)


@pytest.mark.parametrize('value,is_open', [(None, True), ('', True), ('1', True), ('yes', True), ('0', False)])
def test_rule(monkeypatch, value, is_open):
    _set(monkeypatch, value)
    assert fm1_paths.open_toolchain() is is_open
    suffix = '-open' if is_open else ''
    assert fm1_paths.build_dir('modd') == ROOT / 'build' / f'modd{suffix}'
    assert fm1_paths.fwsc_path('modd') == ROOT / 'build' / f'modd{suffix}' / 'modd.fwsc'


@pytest.mark.parametrize('value', [None, '', '0', '1', 'yes'])
def test_fixtures_agree_with_the_resolver(monkeypatch, value):
    for k in OVERRIDES:
        monkeypatch.delenv(k, raising=False)
    _set(monkeypatch, value)
    assert build_fixtures.fwsc_path('modd_emu') == fm1_paths.fwsc_path('modd_emu')
    assert api_fixtures.api_fwsc('modd_emu') == fm1_paths.fwsc_path('modd_emu')


def test_build_py_has_no_rule_of_its_own():
    text = (ROOT / 'tools' / 'build' / 'build.py').read_text()
    assert 'USE_OPEN_TOOLCHAIN' not in text      # the variable is read only in fm1_paths.py
    assert 'open_toolchain()' in text
