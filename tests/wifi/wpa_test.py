#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""WPA2-PSK implementation tests."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
WPA_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_wpa.c'


def test_wpa_has_4way_handshake():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'eapol' in text.lower()
    assert 'ptk' in text.lower() or 'pairwise' in text.lower()


def test_wpa_has_aes():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'aes' in text.lower()


def test_wpa_has_ccmp():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'ccmp' in text.lower() or 'ccm' in text.lower()


def test_wpa_has_pmk_derivation():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'pbkdf2' in text.lower() or 'pmk' in text.lower()


def test_pbkdf2_yields_during_inner_loop():
    """PBKDF2 inner loop must yield periodically for scheduler cooperation."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_wpa.c').read_text()
    fn_start = src.index('wifi_pbkdf2_sha1(')
    fn_end = src.index('\n}', fn_start) + 2
    fn = src[fn_start:fn_end]
    assert 'yield()' in fn, \
        'wifi_pbkdf2_sha1 must yield inside the iteration loop'


def test_4way_handshake_against_a_host_access_point():
    """tests/wifi/wpa_handshake_test.c plays the AP against wifi_wpa.c on the
    host: the 802.11i PMK vector, message 1 arriving one, two and five times
    before message 3, and bytes left after the EAPOL frame (an FCS)."""
    import subprocess
    import tempfile

    source = ROOT / 'tests' / 'wifi' / 'wpa_handshake_test.c'
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as tmp:
        exe = tmp.name
    cmd = ['cc', '-std=c99', '-Wall', '-Wno-unused-function', '-O2',
           '-I', str(ROOT / 'firmware' / 'src'), '-o', exe, str(source)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, f'compilation failed:\n{r.stderr}'
        r = subprocess.run([exe], capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stdout + r.stderr
        assert 'all passed' in r.stdout
    finally:
        Path(exe).unlink(missing_ok=True)

