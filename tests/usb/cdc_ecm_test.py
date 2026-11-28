# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the pure part of CDC-ECM (modd_cdc_ecm.h).

Compiles tests/usb/cdc_ecm_test.c with the host ``cc`` and runs it: the
configuration descriptor parser (an AX88179A-shaped ECM configuration and
the shapes that must not match) and the notification reassembly. No
toolchain or hardware.
"""
from pathlib import Path

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]
ADAPTERS = ROOT / 'firmware' / 'contracts'
SOURCE = ROOT / 'tests' / 'usb' / 'cdc_ecm_test.c'


def test_cdc_ecm_parser_and_notifications():
    run_c_harness(SOURCE, includes=[ADAPTERS], extra_flags=['-Wno-unused-function'])
