# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of the Felucca LED animations (firmware/ports/felucca/leds/, firmware/src/ui/led_idle.c).

Compiles tests/ui/led_anim_test.c with the host ``cc``: the power-on sweep and the idle animation
drawn onto Melodee's LED arrays by sigma-delta, and the rule that starts and stops the idle animation.
The input HAL is a set of stubs. No toolchain or hardware.
"""
from pathlib import Path

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'firmware'
SOURCE = Path(__file__).with_name('led_anim_test.c')


def test_led_animations():
    run_c_harness(SOURCE, includes=[FIRMWARE / 'src', FIRMWARE / 'ports', FIRMWARE],
                  extra_flags=['-Wno-unused-function'])
