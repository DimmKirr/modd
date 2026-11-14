# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the dock adapter's pure part (modd_dock.h).

Compiles tests/dock_test.c with the host ``cc`` and runs it: header checks,
CRC and relocation of a synthetic .dock image, no toolchain or hardware.
"""
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
ADAPTERS = ROOT / 'firmware' / 'adapters'
SOURCE = ROOT / 'tests' / 'dock' / 'dock_test.c'
MODULES = ROOT / 'firmware' / 'upstream' / 'fm1-dock' / 'docs' / 'modules'


def _run(args):
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as tmp:
        exe = tmp.name
    cmd = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
           '-O2', '-I', str(ADAPTERS), '-o', exe, str(SOURCE)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        Path(exe).unlink(missing_ok=True)
        pytest.fail(f'compilation failed:\n{result.stderr}')
    result = subprocess.run([exe, *args], capture_output=True, text=True, timeout=60)
    Path(exe).unlink(missing_ok=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'all passed' in result.stdout
    return result.stdout


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
