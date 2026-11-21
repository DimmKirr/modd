# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the 64-bit division helpers LVGL needs.

``firmware/src/ui/themes/modd/rt64.c`` supplies ``__udivdi3`` and its relatives, which
the pi32v2 compiler calls for a 64-bit ``/`` or ``%`` (LVGL's printf and its image
transforms need them; modd links no compiler runtime). This compiles
``rt64_test.c`` with the host ``cc`` and compares the helpers with the host's own
division over edge values and random operands. No toolchain or hardware.
"""
from pathlib import Path

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[4]
FIRMWARE = ROOT / 'firmware'
SOURCE = Path(__file__).with_name('rt64_test.c')


def test_rt64_division_matches_host():
    run_c_harness(SOURCE, includes=[FIRMWARE / 'src'])
