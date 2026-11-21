#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Where things live on this machine: the fm1-emu checkout, fm1-live, and a defconfig's build output.

One definition for the tools and the tests. JieLi is the default: only USE_OPEN_TOOLCHAIN=1 selects
the open toolchain. The Taskfiles set USE_OPEN_TOOLCHAIN=1 for emulator defconfigs automatically."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EMU_ROOT = Path(os.environ.get('FM1_EMU_ROOT', '/Users/dmitry/dev/modd-fm/fm1-emu/.worktrees/master'))
FM1_LIVE = Path(os.environ.get('FM1_LIVE', EMU_ROOT / 'target' / 'release' / 'fm1-live'))


def open_toolchain():
    """True only when USE_OPEN_TOOLCHAIN is exactly '1': unset or any other value selects JieLi
    (build/<defconfig>/). The Taskfiles set it to '1' for emulator defconfigs."""
    return os.environ.get('USE_OPEN_TOOLCHAIN') == '1'


def build_dir(defconfig):
    return ROOT / 'build' / (defconfig + ('-open' if open_toolchain() else ''))


def fwsc_path(defconfig):
    return build_dir(defconfig) / 'modd.fwsc'
