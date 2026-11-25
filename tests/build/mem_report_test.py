# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""tools/build/mem_report.py: the memory report build.py prints after every link and
`task build:size-check` prints for the last build."""
import struct
import pytest
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
import mem_report  # noqa: E402

# a trimmed GNU ld map in the layout of firmware/app.ld
MAP = """
Discarded input sections

 .text.unused   0x0000000000000000       0x10 build/modd.o

Memory Configuration

Name             Origin             Length             Attributes
XIP              0x0000000002001000 0x000000000008c000 xr
RAM              0x0000000001c08000 0x0000000000018000 xrw
POOL             0x0000000001c20000 0x0000000000054000 xrw
NOINIT           0x0000000001c7c020 0x0000000000003d30 rw
*default*        0x0000000000000000 0xffffffffffffffff

Linker script and memory map

.text           0x0000000002001000    0x40000
 .text          0x0000000002001000    0x40000 build/modd.o
.data           0x0000000001c08000       0x40 load address 0x0000000002041000
.bss            0x0000000001c08040    0x17000 load address 0x0000000002041040
                0x0000000001c08040                _bss_start = .
                0x0000000001c1f040                _bss_end = .
                0x0000000001c1f040                _resource_data_start = _bss_end
                0x0000000001c20000                _resource_data_end = (ORIGIN (RAM) + LENGTH (RAM))
.pool           0x0000000001c20000    0x20000
 .pool_net      0x0000000001c20000     0x8000 build/modd.o
.cpu1_stacks    0x0000000001c40000        0x0 load address 0x0000000002041040
                0x0000000001c40000                _resource_start = .
                0x0000000001c74000                _resource_end = (ORIGIN (POOL) + LENGTH (POOL))
                0x0000000000022c00                _resource_minimum = DEFINED (AUDIO_RESIDENT)?0xc000:0x22c00
.noinit         0x0000000001c7c020     0x1000
                0x0000000001c74100                _ustack_lo = 0x1c74100
                0x0000000001c7a000                _ustack_top = 0x1c7a000
"""

# app_emu.ld: the shadow framebuffer inside the arena, no minimum
EMU_MAP = MAP.replace(
    "                0x0000000000022c00                _resource_minimum = DEFINED (AUDIO_RESIDENT)?0xc000:0x22c00\n",
    ".framebuffer    0x0000000001c40000    0x1c200 load address 0x0000000002041040\n")


def _elf(objects):
    """A minimal 32-bit little-endian ELF holding only a symbol table of (name, addr, size, type)."""
    strtab = b'\0'
    syms = [b'\0' * 16]
    for name, addr, size, typ in objects:
        syms.append(struct.pack('<IIIBBH', len(strtab), addr, size, typ, 0, 1))
        strtab += name.encode() + b'\0'
    symdata = b''.join(syms)
    sym_off = 52
    str_off = sym_off + len(symdata)
    shoff = str_off + len(strtab)
    shoff += -shoff % 4
    header = b'\x7fELF\x01\x01\x01' + b'\0' * 9
    header += struct.pack('<HHIIIIIHHHHHH', 2, 0, 1, 0, 0, shoff, 0, 52, 0, 0, 40, 3, 0)
    body = header + symdata + strtab
    body += b'\0' * (shoff - len(body))
    sh = [b'\0' * 40,
          struct.pack('<10I', 0, 2, 0, 0, sym_off, len(symdata), 2, 0, 4, 16),   # SHT_SYMTAB, link 2
          struct.pack('<10I', 0, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0)]     # SHT_STRTAB
    return body + b''.join(sh)


def test_parse_map_reads_regions_sections_and_linker_symbols():
    m = mem_report.parse_map(MAP)
    assert m['regions']['RAM'] == (0x1c08000, 96 * 1024)
    assert '*default*' not in m['regions']
    names = [s[0] for s in m['sections']]
    assert names[:3] == ['.text', '.data', '.bss']
    assert '.text.unused' not in names           # discarded input sections are not output sections
    assert m['symbols']['_resource_minimum'] == 139 * 1024
    assert m['symbols']['_bss_end'] == 0x1c1f040


def test_regions_add_up_their_sections_and_warn_when_nearly_full():
    a = mem_report.analyse(mem_report.parse_map(MAP), body_size=0x41040)
    ram = next(r for r in a['regions'] if r['name'] == 'RAM')
    assert ram['used'] == 0x40 + 0x17000
    xip = next(r for r in a['regions'] if r['name'] == 'XIP')
    assert xip['used'] == 0x41040                # the flash image, not only .text
    assert any(w.startswith('RAM is 95% full') for w in a['warnings'])


def test_build_fails_when_ram_has_less_than_the_minimum_free(monkeypatch):
    m = mem_report.parse_map(MAP)
    ram = next(r for r in mem_report.analyse(m)['regions'] if r['name'] == 'RAM')
    free = ram['size'] - ram['used']
    assert mem_report.analyse(m)['errors'] == []
    monkeypatch.setattr(mem_report, 'RAM_MIN_FREE', free + 1)
    a = mem_report.analyse(m)
    assert len(a['errors']) == 1 and 'RAM has' in a['errors'][0]
    assert 'ERROR: RAM has' in mem_report.format_report(a)
    monkeypatch.setattr(mem_report, 'RAM_MIN_FREE', free)       # exactly the minimum is allowed
    assert mem_report.analyse(m)['errors'] == []


def test_audio_arena_is_checked_against_the_linker_minimum():
    a = mem_report.analyse(mem_report.parse_map(MAP))
    arena = next(b for b in a['budgets'] if b['name'] == 'audio arena')
    assert arena['size'] == 0x1c74000 - 0x1c40000
    assert arena['need'] == 139 * 1024
    text = mem_report.format_report(a)
    assert 'needs 139.0K, 69.0K to spare' in text


def test_emulator_map_shows_the_framebuffer_inside_the_arena():
    a = mem_report.analyse(mem_report.parse_map(EMU_MAP))
    arena = next(b for b in a['budgets'] if b['name'] == 'audio arena')
    assert arena['need'] is None
    assert arena['borrowed'] == 0x1c200
    assert 'shadow framebuffer 112.5K inside it, 95.5K left' in mem_report.format_report(a)


def _emu_map(fb_size):
    return EMU_MAP.replace('0x1c200', f'{fb_size:#x}')


def _arena_msgs(m):
    a = mem_report.analyse(mem_report.parse_map(m))
    return ([e for e in a['errors'] if 'audio arena' in e],
            [w for w in a['warnings'] if 'audio arena' in w], a)


def test_arena_without_a_minimum_fails_when_less_than_the_floor_is_left():
    arena = 0x34000
    errors, _w, a = _arena_msgs(_emu_map(arena - mem_report.ARENA_MIN_LEFT + 1))
    assert len(errors) == 1 and '208.0K' in errors[0] and 'floor' in errors[0]
    assert 'ERROR: audio arena' in mem_report.format_report(a)
    errors, _w, _a = _arena_msgs(_emu_map(arena - mem_report.ARENA_MIN_LEFT))   # exactly the floor is allowed
    assert errors == []


def test_config_floor_of_zero_gives_no_arena_error():
    """CONFIG_ARENA_MIN_LEFT=0 (the included Felucca stack): the arena may be filled to the last byte."""
    m = mem_report.parse_map(_emu_map(0x34000))                  # the framebuffer takes the whole arena
    assert [e for e in mem_report.analyse(m, arena_min_left=0)['errors'] if 'audio arena' in e] == []
    assert any('audio arena' in e for e in mem_report.analyse(m)['errors'])   # the default floor still stops it


def test_arena_without_a_minimum_is_quiet_with_room_to_spare():
    assert _arena_msgs(EMU_MAP)[:2] == ([], [])


def test_arena_without_a_minimum_warns_just_above_the_floor():
    errors, warnings, _a = _arena_msgs(_emu_map(0x34000 - mem_report.ARENA_MIN_LEFT - 1024))
    assert errors == [] and len(warnings) == 1 and 'floor' in warnings[0]


def test_arena_with_a_minimum_is_left_to_the_linker_assert():
    big_fb = MAP + \
        ".framebuffer    0x0000000001c40000    0x33000 load address 0x0000000002041040\n"
    errors, warnings, a = _arena_msgs(big_fb)
    assert errors == [] and warnings == []
    assert 'needs 139.0K, 69.0K to spare' in mem_report.format_report(a)


def test_elf_objects_feed_the_stacks_and_the_largest_objects():
    elf = _elf([('cv_primary_u', 0x1c20000, 59520, 1),
                ('fm1_main.ui_stack', 0x1c30000, 2560, 1),
                ('lvt_dbg_stack', 0x1c31000, 8, 1),          # a small variable, not a stack
                ('trk', 0x1c08100, 5408, 1),
                ('main', 0x2001000, 400, 2)])                # a function: not an object
    objects = mem_report.elf_objects(elf)
    assert sorted(o[2] for o in objects) == ['cv_primary_u', 'fm1_main.ui_stack', 'lvt_dbg_stack', 'trk']
    a = mem_report.analyse(mem_report.parse_map(MAP), objects)
    assert ('fm1_main.ui_stack', 2560) in a['stacks']
    assert all(name != 'lvt_dbg_stack' for name, _ in a['stacks'])
    assert ('CPU0 user', 0x1c7a000 - 0x1c74100) in a['stacks']
    assert a['top']['POOL'][0] == (59520, 'cv_primary_u')
    assert a['top']['RAM'] == [(5408, 'trk')]


def test_report_lines_stay_within_100_columns_and_keep_items_whole():
    objects = [(0x1c20000 + k * 0x100, 4096 - k, f'buffer_with_a_long_name_{k}') for k in range(12)]
    text = mem_report.format_report(mem_report.analyse(mem_report.parse_map(MAP), objects), n=12)
    lines = text.splitlines()
    assert all(len(line) <= 100 for line in lines)
    assert not any(line.rstrip().endswith('buffer_with_a_long_name_3') for line in lines)


def test_report_of_a_real_build_when_there_is_one():
    build = ROOT / 'build' / 'modd'
    if not (build / 'modd.map').exists():
        import pytest
        pytest.skip('no build/modd (task build)')
    text = mem_report.report(build)
    assert text.startswith('memory: ')
    for region in ('XIP', 'RAM', 'POOL', 'NOINIT'):
        assert f'\n  {region} ' in text


def test_report_names_the_components_and_files_that_fill_ram(tmp_path):
    """mem_files: statics in the unity-build object are matched to the file that defines them."""
    import mem_files
    (tmp_path / 'modd.map').write_text(MAP + """
 .bss.wifi_buf_pool
                0x0000000001c124d0     0x6580 /x/build/modd/modd.o
 .bss..L_MergedGlobals.7
                0x0000000001c18a50      0x200 /x/build/modd/modd.o
 .bss.no_such_symbol_anywhere
                0x0000000001c18c50      0x100 /x/build/modd/modd.o
""")
    comp, files, sizes = mem_files.attribute(tmp_path)
    assert sizes['RAM'] > 0
    assert comp['RAM']['src/wifi'] == 0x6580 and files['RAM']['src/wifi/wifi_rtos.c'] == 0x6580
    assert comp['RAM'][mem_files.MERGED] == 0x200
    assert comp['RAM'][mem_files.UNATTRIBUTED] == 0x100
    text = mem_files.format_files(tmp_path)
    assert 'RAM: src/wifi' in text and 'wifi_rtos.c' in text


# a trimmed lld map (open toolchain) of firmware/app.ld: no Memory Configuration table, hex without
# 0x, symbol addresses of script-assigned symbols are not their values (see _resource_data_end)
LLD_MAP = """     VMA      LMA     Size Align Out     In      Symbol
 2001000  2001000    40000     4 .text
 2001000  2001000      100     2         /x/build/modd-open/modd.o:(.text.main)
 2001000  2001000      100     1                 main
 1c08000  2041000       40     4 .data
 1c08040  2041040        0     1 _data_load = LOADADDR(.data)
 1c08040  1c08040    17000    32 .bss
 1c08040  1c08040      6580     4         /x/build/modd-open/modd.o:(.bss.wifi_buf_pool)
 1c10000  1c10000      200     4         /x/build/modd-open/modd.o:(.bss..L_MergedGlobals.7)
 1c1f040  1c1f040        0     1 _resource_data_start = _bss_end
 1c1f040  1c1f040        0     1 _resource_data_end = ORIGIN(RAM) + LENGTH(RAM)
 1c20000  1c20000    20000     4 .pool
 1c40000  1c40000        0     1         _resource_start = .
 1c7c020  1c7c020     1000     4 .noinit
       0        0     33d5     1 .debug_loc
"""
LLD_ELF = _elf([('_resource_start', 0x1c40000, 0, 0), ('_resource_end', 0x1c74000, 0, 0),
                ('_resource_data_start', 0x1c1f040, 0, 0), ('_resource_data_end', 0x1c20000, 0, 0),
                ('_ustack_lo', 0x1c74100, 0, 0), ('_ustack_top', 0x1c7a000, 0, 0),
                ('FLASH_BODY_MAX', 0x8c000, 0, 0),   # build.py's --defsym: the XIP length in app*.ld
                ('POOL_SIZE', 0x54000, 0, 0),         # kconf/memory.kconfig: the POOL region length
                ('ARENA_MIN', 0x22c00, 0, 0)])         # kconf/memory.kconfig: audio arena floor


def _lld_build(tmp_path, map_text=LLD_MAP, config=''):
    (tmp_path / 'gen').mkdir(exist_ok=True)
    (tmp_path / 'gen' / 'config.h').write_text(config)
    (tmp_path / 'modd.map').write_text(map_text)
    (tmp_path / 'modd.elf').write_bytes(LLD_ELF)
    return tmp_path


def test_lld_map_is_detected_by_its_header():
    assert mem_report.is_lld(LLD_MAP) and not mem_report.is_lld(MAP)
    with pytest.raises(ValueError):
        mem_report.parse_map(LLD_MAP)            # no regions: asks for them rather than guessing
    secs = mem_report.parse_lld_sections(LLD_MAP)
    assert [s[0] for s in secs] == ['.text', '.data', '.bss', '.pool', '.noinit', '.debug_loc']
    assert ('.bss', 0x1c08040, 0x17000) in secs  # input sections and symbols are not output sections


def test_ld_regions_come_from_the_memory_block_and_only_literals_are_accepted():
    syms = {'FLASH_BODY_MAX': 0x8c000, 'POOL_SIZE': 0x54000, 'ARENA_MIN': 0x22c00}
    r = mem_report.parse_ld_regions((ROOT / 'firmware' / 'app.ld').read_text(), syms)
    assert r['RAM'] == (0x1c08000, 96 * 1024) and r['XIP'] == (0x2001000, 0x8c000)
    assert r['POOL'] == (0x1c20000, 0x54000)
    with pytest.raises(ValueError):   # the XIP length is the build's --defsym: the ELF's symbol must supply it
        mem_report.parse_ld_regions((ROOT / 'firmware' / 'app.ld').read_text())
    assert mem_report.parse_ld_regions('MEMORY { A (rw) : ORIGIN = 0x10, LENGTH = 2K /* c */ }') == {'A': (16, 2048)}
    with pytest.raises(ValueError):
        mem_report.parse_ld_regions('MEMORY { A (rw) : ORIGIN = 0x10, LENGTH = SIZE }')
    with pytest.raises(ValueError):
        mem_report.parse_ld_regions('SECTIONS { }')


def test_lld_build_reports_like_gnu_and_symbols_come_from_the_elf(tmp_path):
    a = mem_report.analyse_build(_lld_build(tmp_path))
    ram = next(r for r in a['regions'] if r['name'] == 'RAM')
    assert ram['used'] == 0x40 + 0x17000 and ram['size'] == 96 * 1024
    arena = next(b for b in a['budgets'] if b['name'] == 'audio arena')
    assert arena['size'] == 0x1c74000 - 0x1c40000
    tail = next(b for b in a['budgets'] if b['name'] == 'RAM tail')
    assert tail['size'] == 0x1c20000 - 0x1c1f040     # the ELF value, not the map's address column
    assert ('CPU0 user', 0x1c7a000 - 0x1c74100) in a['stacks']
    assert any(w.startswith('RAM is 95% full') for w in a['warnings'])


def test_lld_build_fails_when_ram_has_less_than_the_minimum_free(tmp_path, monkeypatch):
    build = _lld_build(tmp_path)
    assert mem_report.analyse_build(build)['errors'] == []
    monkeypatch.setattr(mem_report, 'RAM_MIN_FREE', 96 * 1024 - 0x17040 + 1)
    assert 'RAM has' in mem_report.analyse_build(build)['errors'][0]


def test_lld_build_with_a_section_outside_every_region_is_an_error(tmp_path):
    build = _lld_build(tmp_path, LLD_MAP + " 1d00000  1d00000      10     4 .stray\n")
    with pytest.raises(ValueError, match='.stray'):
        mem_report.analyse_build(build)


def test_lld_build_names_the_components_that_fill_ram(tmp_path):
    import mem_files
    build = _lld_build(tmp_path)
    comp, files, _sizes = mem_files.attribute(build)
    assert comp['RAM']['src/wifi'] == 0x6580
    assert comp['RAM'][mem_files.MERGED] == 0x200
