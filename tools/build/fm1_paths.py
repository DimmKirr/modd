#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Where things live on this machine: the fm1-emu checkout, fm1-live, and a defconfig's build output.

One definition for the tools and the tests. The open pi32v2 toolchain is the default: only
USE_OPEN_TOOLCHAIN=0 selects JieLi. The Taskfiles also select JieLi when OPEN_TOOLCHAIN_FLASH_OK is 0,
but they export the result as USE_OPEN_TOOLCHAIN=0 or 1, so Python reads that one variable."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EMU_ROOT = Path(os.environ.get('FM1_EMU_ROOT', '/Users/dmitry/dev/modd-fm/fm1-emu/.worktrees/master'))
FM1_LIVE = Path(os.environ.get('FM1_LIVE', EMU_ROOT / 'target' / 'release' / 'fm1-live'))


def open_toolchain():
    """True unless USE_OPEN_TOOLCHAIN is exactly '0' (JieLi): unset, empty, '1' or anything else is open
    (build/<defconfig>-open/). Read at call time."""
    return os.environ.get('USE_OPEN_TOOLCHAIN') != '0'


def build_dir(defconfig):
    return ROOT / 'build' / (defconfig + ('-open' if open_toolchain() else ''))


def fwsc_path(defconfig):
    return build_dir(defconfig) / 'modd.fwsc'
