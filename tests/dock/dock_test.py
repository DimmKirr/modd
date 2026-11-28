# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the dock adapter's pure part (modd_dock.h).

Compiles tests/dock/dock_test.c with the host ``cc`` and runs it: header checks,
CRC and relocation of a synthetic .dock image, no toolchain or hardware.
"""
from pathlib import Path

import pytest

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]
ADAPTERS = ROOT / 'firmware' / 'contracts'
SOURCE = ROOT / 'tests' / 'dock' / 'dock_test.c'
MODULES = ROOT / 'firmware' / 'upstream' / 'fm1-dock' / 'docs' / 'modules'


def _run(args):
    return run_c_harness(SOURCE, includes=[ADAPTERS], args=args).stdout


def test_dock_header_crc_relocation():
    """Synthetic image: header checks, CRC, relocation there and back."""
    _run([])


def test_dock_real_modules_verify_and_relocate():
    """Every prebuilt .dock fm1-dock ships passes the checks, relocates to the
    modd deploy area and back without a CRC change."""
    files = sorted(MODULES.glob('*.dock'))
    if not files:
        pytest.skip('fm1-dock not vendored (task vendor:ensure)')
    out = _run([str(f) for f in files])
    assert out.count('\nok ') + out.startswith('ok ') == len(files)
