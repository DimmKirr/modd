# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the network needs (firmware/src/net/net_need.c, wifi/need.c).

Compiles tests/net/net_need_test.c with the host ``cc`` and runs it: net_need() counted
onto wifi_need() / eth_need() by link policy, the WIFI and USB NET settings as permissions.
The radio and the link state are stubs. No toolchain or hardware.
"""
from pathlib import Path

import pytest

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'firmware'
SOURCE = ROOT / 'tests' / 'net' / 'net_need_test.c'


@pytest.mark.parametrize('defs', [[], ['TEST_NO_RPC']], ids=['rpc', 'no_rpc'])
def test_net_need_counts_and_link_policy(defs):
    run_c_harness(SOURCE, includes=[FIRMWARE / 'src'], defines=defs,
                  extra_flags=['-Wno-unused-function', '-Wno-unused-variable'])
