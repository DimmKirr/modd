# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Hub enumeration policy guards (firmware/src/udisk/usb_hub.c).

Source-level checks, no hardware: the 2026-11-13 device log showed a flash
drive behind the dock's hub timing out its first SET_ADDRESS 33 ms after the
port reset and being dropped for good, so only the empty card reader was
left. A port must get more than one reset-and-address attempt.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HUB = ROOT / 'firmware' / 'src' / 'udisk' / 'usb_hub.c'


def _src():
    return HUB.read_text()


def test_port_address_is_retried():
    """A failed SET_ADDRESS re-resets the port and tries again, at least 3 times."""
    src = _src()
    m = re.search(r'#define HUB_ADDR_TRIES\s+(\d+)', src)
    assert m, 'HUB_ADDR_TRIES missing'
    assert int(m.group(1)) >= 3
    fail = src.index('hub: SET_ADDRESS fail')
    window = src[fail:fail + 400]
    assert 'HUB_ADDR_TRIES' in window and 'retry' in window.lower(), \
        'the SET_ADDRESS failure path must loop back to the port reset'


def test_port_address_recovery_grows():
    """The recovery delay before SET_ADDRESS is longer on a retry than the spec minimum."""
    src = _src()
    m = re.search(r'#define HUB_ADDR_RETRY_MS\s+(\d+)', src)
    assert m and int(m.group(1)) >= 100


def test_last_attempt_power_cycles_the_port():
    """A port whose device stays deaf is power-cycled before the final attempt, on hubs
    with per-port power switching (wHubCharacteristics bits 1:0 == 1)."""
    src = _src()
    i = src.index('hub: port power cycle')
    window = src[i - 400:i + 600]
    assert 'wHubCharacteristics & 3u) == 1u' in window
    assert 'HUB_FEAT_PORT_POWER' in window and 'HUB_PORT_OFF_MS' in window
