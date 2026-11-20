# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the WiFi power state machine (firmware/src/wifi/wifi_api.c, wifi_need.c,
firmware/src/net/net_need.c).

Compiles tests/wifi/wifi_power_test.c with the host ``cc``: the three real files, every layer below
wifi_api.c a stub that counts its calls. Covers wifi_poll() (off and down: nothing; need: init once;
off: disconnect, deinit once; on again: init again), wifi_scan/connect/disconnect while down, the
manual-join need (Felucca: no RPC, no SSID), and, built with CONFIG_DOCK, that the radio does not
start while a dock module runs. No toolchain or hardware.
"""
from pathlib import Path

import pytest

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'firmware'
SOURCE = ROOT / 'tests' / 'wifi' / 'wifi_power_test.c'


@pytest.mark.parametrize('defs', [[], ['TEST_DOCK']], ids=['plain', 'dock'])
def test_wifi_power_state_machine(defs):
    run_c_harness(SOURCE, includes=[FIRMWARE / 'src', FIRMWARE / 'adapters'], defines=defs,
                  extra_flags=['-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter'])
