# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled test of when the USB disk is used (firmware/src/udisk/udisk.c).

Compiles tests/usb/udisk_need_test.c with the host ``cc`` and runs it: the
DISK setting as a permission, the counted need that pages and copies take,
and the unmount in ``udisk_poll()``. FatFs and the USB host are stubs. No
toolchain or hardware.
"""
from pathlib import Path

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'firmware'
SOURCE = ROOT / 'tests' / 'usb' / 'udisk_need_test.c'


def test_udisk_setting_and_need():
    run_c_harness(SOURCE, includes=[FIRMWARE / 'adapters', FIRMWARE / 'src'],
                  extra_flags=['-Wno-unused-function', '-Wno-unused-variable'])
