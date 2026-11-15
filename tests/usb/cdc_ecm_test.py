# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the pure part of CDC-ECM (modd_cdc_ecm.h).

Compiles tests/usb/cdc_ecm_test.c with the host ``cc`` and runs it: the
configuration descriptor parser (an AX88179A-shaped ECM configuration and
the shapes that must not match) and the notification reassembly. No
toolchain or hardware.
"""
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
ADAPTERS = ROOT / 'firmware' / 'adapters'
SOURCE = ROOT / 'tests' / 'usb' / 'cdc_ecm_test.c'


def test_cdc_ecm_parser_and_notifications():
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as tmp:
        exe = tmp.name
    cmd = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
           '-O2', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
           '-I', str(ADAPTERS), '-o', exe, str(SOURCE)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        # sanitizer runtimes are not on every host: the checks still run without them
        cmd = [c for c in cmd if not c.startswith('-fsanitize') and not c.startswith('-fno-sanitize')]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        Path(exe).unlink(missing_ok=True)
        pytest.fail(f'compilation failed:\n{result.stderr}')
    result = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    Path(exe).unlink(missing_ok=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'all passed' in result.stdout


def test_driver_uses_the_tested_code():
    """The firmware must call the tested functions, not keep a private copy."""
    hub = (ROOT / 'firmware' / 'src' / 'udisk' / 'usb_hub.c').read_text()
    ecm = (ROOT / 'firmware' / 'src' / 'net' / 'usbnet_ecm.c').read_text()
    assert 'cdc_ecm_parse_config(' in hub
    assert 'cdc_notify_feed(' in ecm
