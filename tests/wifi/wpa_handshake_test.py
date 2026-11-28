#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of firmware/src/wifi/mac/wpa.c: wpa_handshake_test.c plays the access
point. It checks the 802.11i PMK vector, message 1 arriving one, two and five times before
message 3, bytes left after the EAPOL frame (an FCS), and a bad MIC being refused."""
from pathlib import Path

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]


def test_4way_handshake_against_a_host_access_point():
    run_c_harness(ROOT / 'tests' / 'wifi' / 'wpa_handshake_test.c', includes=[ROOT / 'firmware' / 'src'],
                  extra_flags=['-Wno-unused-function'])
