# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""The SPI-flash layout: hardware values are pinned, the emulator's are consistent.

kconf/flash.kconfig holds the free numbers, firmware/hal/fm1_flash_map.h (C) and
tools/build/flashmap.py (package builder, linker) derive the rest the same way.
"""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from kconfiglib import Kconfig

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
import flashmap


def layout(emu):
    k = Kconfig(str(ROOT / 'Kconfig'), warn=False)
    if emu:
        k.syms['EMU'].set_value('y')
    v = {n: int(k.syms['FLASH_' + n].str_value, 0) for n in ('CHIP_SIZE', 'APP_END')}
    return flashmap.Layout(v['CHIP_SIZE'], v['APP_END'])


def test_hardware_layout_is_pinned():
    """The shipped FM-1 layout: boot stub, loader and every installed unit rely on these numbers."""
    hw = layout(emu=False)
    assert (hw.chip, hw.app_end) == (0x100000, 0x93000)
    assert (hw.body_max, hw.mark_sec, hw.mark_off) == (0x8C000, 0x91000, 0x91FE0)
    assert (hw.app_slot, hw.vm_len, hw.shift) == (0x8DFBC, 352256, 0)
    assert (hw.data_lo, hw.data_hi, hw.ota_lo, hw.bank_ext_lo, hw.glob_lo, hw.keymac) == \
        (0x97000, 0xE0000, 0xE0000, 0xE5000, 0xFC000, 0xFF000)
    assert (hw.btif, hw.usr, hw.flash_hdr_size, hw.smp_user_lo) == (0xE9000, 0xEA000, 0xFF000, 0xA0000)
    hw.check()


def test_emulator_layout_is_an_8_mib_chip_with_the_stores_shifted_up():
    emu, hw = layout(emu=True), layout(emu=False)
    emu.check()
    assert emu.chip == 0x800000 and emu.app_end == 0x600000
    assert (emu.mark_sec, emu.body_max) == (0x5FE000, 0x5F9000)
    # every store above the app area keeps its hardware size and order, shifted by the same amount
    assert emu.shift == 0x600000 - 0x93000
    for name in ('data_lo', 'data_hi', 'ota_lo', 'bank_ext_lo', 'glob_lo', 'keymac', 'btif', 'usr', 'smp_user_lo'):
        assert getattr(emu, name) - getattr(hw, name) == emu.shift, name
    assert emu.data_hi - emu.data_lo == hw.data_hi - hw.data_lo == 0x49000
    # regions in order, none overlapping, all inside the chip
    assert flashmap.APP_LO < emu.mark_sec < emu.app_end < emu.data_lo < emu.data_hi <= emu.ota_lo \
        < emu.bank_ext_lo < emu.glob_lo < emu.keymac < emu.chip
    # three sample slots, inside the main store
    slots_end = emu.smp_user_lo + flashmap.SMP_USER_N * flashmap.SMP_USER_SIZE
    assert flashmap.SMP_USER_N == 3 and emu.data_lo <= emu.smp_user_lo and slots_end <= emu.data_hi
    assert emu.flash_hdr_size == emu.chip - 0x1000


def test_bad_layouts_are_refused():
    with pytest.raises(SystemExit):   # not sector aligned
        flashmap.Layout(0x100000, 0x93800).check()
    with pytest.raises(SystemExit):   # the stores above the app area run past the chip
        flashmap.Layout(0x100000, 0xA3000).check()
    with pytest.raises(SystemExit):   # a chip the SPI address cannot reach
        flashmap.Layout(0x2000000, 0x600000).check()


@pytest.mark.skipif(not shutil.which('cc'), reason='no host C compiler')
@pytest.mark.parametrize('emu', [False, True])
def test_c_header_agrees_with_python(tmp_path, emu):
    """fm1_flash_map.h (the stub, the loader and the app) derives what flashmap.py derives."""
    lay = layout(emu)
    src = tmp_path / 'm.c'
    src.write_text('#include <stdio.h>\n#include "fm1_flash_map.h"\nint main(void){printf("%u %u %u %u %u %u\\n",'
                   'FLASH_BODY_MAX, FLASH_MARK_SEC, FLASH_MARK_OFF, FLASH_DATA_LO, FLASH_SMP_USER(1), '
                   'FLASH_GLOB_LO);return 0;}\n')
    exe = tmp_path / 'm'
    subprocess.run(['cc', *lay.defines(), '-I' + str(ROOT / 'firmware' / 'hal'), str(src), '-o', str(exe)], check=True)
    got = [int(x) for x in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.split()]
    assert got == [lay.body_max, lay.mark_sec, lay.mark_off, lay.data_lo,
                   lay.smp_user_lo + flashmap.SMP_USER_SIZE, lay.glob_lo]


def test_config_h_round_trip():
    lay = flashmap.from_config_h('#define CONFIG_FLASH_CHIP_SIZE 0x800000\n#define CONFIG_FLASH_APP_END 0x600000\n')
    assert lay == layout(emu=True)
    with pytest.raises(SystemExit):
        flashmap.from_config_h('#define CONFIG_HAL_SYS_MELODEE 1\n')


def test_the_emulator_gets_its_chip_size_from_the_build(tmp_path):
    import fm1_paths
    (tmp_path / 'gen').mkdir()
    fwsc = tmp_path / 'modd.fwsc'
    assert fm1_paths.flash_size_args(fwsc) == []                       # no config.h: an FM-1
    (tmp_path / 'gen' / 'config.h').write_text('#define CONFIG_FLASH_CHIP_SIZE 0x800000\n')
    assert fm1_paths.flash_size_args(fwsc) == ['--flash-size', '8M']
    assert fm1_paths.flash_size_args(fwsc, ['--flash-size', '16M']) == []
    (tmp_path / 'gen' / 'config.h').write_text('#define CONFIG_FLASH_CHIP_SIZE 0x100000\n')
    assert fm1_paths.flash_size_args(fwsc) == []
