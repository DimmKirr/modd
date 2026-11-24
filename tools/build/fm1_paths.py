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


def flash_size(fwsc):
    """The flash chip size (bytes) of the build that produced `fwsc`: CONFIG_FLASH_CHIP_SIZE in its
    gen/config.h (kconf/flash.kconfig; 1 MiB on an FM-1). 1 MiB when the build has no config.h."""
    import re
    cfg = Path(fwsc).resolve().parent / 'gen' / 'config.h'
    try:
        m = re.search(r'^#define CONFIG_FLASH_CHIP_SIZE (\w+)$', cfg.read_text(), re.M)
    except OSError:
        return 1 << 20
    return int(m.group(1), 0) if m else 1 << 20


def flash_size_args(fwsc, flags=()):
    """fm1-live's `--flash-size` for the build of `fwsc`: [] for the FM-1's 1 MiB (its default) or when
    `flags` already name a size, else ['--flash-size', '8M']. Every tool that boots a package uses this,
    so nobody has to remember the flag."""
    size = flash_size(fwsc)
    if size == 1 << 20 or '--flash-size' in flags:
        return []
    return ['--flash-size', f'{size >> 20}M']
