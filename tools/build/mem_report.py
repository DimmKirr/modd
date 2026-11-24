#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Memory report of a linked build: every region, what fills it, the budgets the linker script
sets, the stacks, and the largest objects.

    uv run python tools/build/mem_report.py build/modd [-n 8]

build.py prints it after every link; `task build:size-check` (run by `task flash`) prints it for
the last build of a defconfig. It reads build/<defconfig>/modd.map (regions, output sections,
linker symbols; for an lld map of the open toolchain the regions come from the linker script's
MEMORY block and the symbols from modd.elf), modd.elf (object sizes) and body.bin (the flash image; modd.fwsc is a fixed-size
package, its size says nothing). It only reports: the linker script's region sizes and ASSERTs
are what fail a build.

Budgets (firmware/app*.ld):
  audio arena   _resource_start.._resource_end, at least _resource_minimum (139 KiB; 48 KiB
                with AUDIO_RESIDENT). app_emu.ld has no minimum; a shadow framebuffer
                (CONFIG_DEBUG_FRAMEBUFFER) placed inside the arena is counted against it
                (app_emu.ld keeps it in its own region, FBUF).
  tails         RAM code end to STATUS (_resource_low_*), .bss end to RAM end
                (_resource_data_*), NOINIT end to the boot records (_resource_retained_*).
  stacks        CPU0 user and system stacks (_ustack_*, _sstack_*: fixed, outside the regions),
                the scheduler's task stacks (objects named *_stack), CPU1's (.cpu1_stacks).
Warnings: a region at 90 % or more, a budget with less than 4 KiB to spare.
"""
import os
import argparse
import re
import struct
import sys
import textwrap
from pathlib import Path

WARN_PCT = 90
WARN_MARGIN = 4096

_REGION = re.compile(r'^(\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)')
_SECTION = re.compile(r'^(\.[\w.]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)')
_SECTION_NAME = re.compile(r'^(\.[\w.]+)\s*$')
_ADDR_SIZE = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)')
_SYMBOL = re.compile(r'^\s+0x([0-9a-f]+)\s+([A-Za-z_]\w*)(\s*=.*)?$')

# A build fails when the RAM region (.data, .bss) has less than this free: the stacks and
# the next feature's statics have no room left. Override per build with MODD_RAM_MIN_FREE (bytes).
RAM_MIN_FREE = int(os.environ.get('MODD_RAM_MIN_FREE', '1024'))

# Guard for linker scripts without _resource_minimum (app_emu.ld), not a measured audio budget.
# The floor of a build is CONFIG_ARENA_MIN_LEFT (kconf/audio.kconfig; 0 when nothing in the build
# borrows from the arena); this is the value when no config is given.
ARENA_MIN_LEFT = 16 * 1024

# what each region holds, for the report's last column
_ROLE = {
    'XIP': 'code, constants, load images',
    'RAMTEXT': 'code run from RAM',
    'RAM': '.data, .bss',
    'POOL': 'buffers, task stacks; free space: audio arena',
    'DOCK': 'dock module RAM; free space: audio arena',
    'NOINIT': 'kept across resets',
    'CACHE': 'audio resource bank',
}


# lld (open toolchain) maps: a column header instead of GNU's tables, hex without 0x. The
# output sections sit in the Out column (one space after Align, name starts with a dot).
_LLD_HEADER = re.compile(r'^\s*VMA\s+LMA\s+Size\s+Align\s+Out\s+In\s+Symbol\s*$')
_LLD_SECTION = re.compile(r'^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+ (\.[\w.]+)\s*$')
_LLD_INPUT = re.compile(r'^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+ {5,}(.+?):\((\S+)\)\s*$')
_LD_REGION = re.compile(r'(\w+)\s*(?:\([^)]*\))?\s*:\s*ORIGIN\s*=\s*([^,]+?)\s*,\s*LENGTH\s*=\s*([^\s/}]+)')
_LD_NUMBER = re.compile(r'^(0x[0-9a-fA-F]+|\d+)([KM]?)$')


def is_lld(text):
    """True for an lld map (the open toolchain), by its column header."""
    return any(_LLD_HEADER.match(l) for l in text.splitlines()[:3])


def parse_lld_sections(text):
    """[(name, addr, size)] of the output sections of an lld map."""
    out = []
    for line in text.splitlines():
        m = _LLD_SECTION.match(line)
        if m:
            out.append((m.group(3), int(m.group(1), 16), int(m.group(2), 16)))
    return out


def parse_lld_inputs(text):
    """[(section, addr, size, object)] of the placed input sections of an lld map."""
    out = []
    for line in text.splitlines():
        m = _LLD_INPUT.match(line)
        if m and int(m.group(1), 16) and int(m.group(2), 16):
            out.append((m.group(4), int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    return out


def _ld_number(text, symbols=None):
    text = text.strip()
    if symbols and text in symbols:     # a value the build passes with --defsym (FLASH_BODY_MAX)
        return symbols[text]
    m = _LD_NUMBER.match(text)
    if not m:
        raise ValueError(f'linker script region value {text.strip()!r} is not a plain number')
    return int(m.group(1), 0) * {'': 1, 'K': 1024, 'M': 1 << 20}[m.group(2)]


def parse_ld_regions(script, symbols=None):
    """{name: (origin, length)} from the MEMORY block of a linker script. Only literal numbers
    (0x.., 96K) and the --defsym symbols in `symbols` (the ELF's, e.g. FLASH_BODY_MAX) are
    understood: anything else raises ValueError rather than guessing."""
    script = re.sub(r'/\*.*?\*/', '', script, flags=re.S)
    block = re.search(r'\bMEMORY\s*\{(.*?)\}', script, re.S)
    if not block:
        raise ValueError('no MEMORY block in the linker script')
    regions = {m.group(1): (_ld_number(m.group(2), symbols), _ld_number(m.group(3), symbols))
               for m in _LD_REGION.finditer(block.group(1))}
    if not regions:
        raise ValueError('no regions in the linker script MEMORY block')
    return regions


def elf_symbols(data):
    """{name: value} of the symbol table of a 32-bit little-endian ELF (lld's map does not
    print the value of a linker-defined symbol reliably; the symbol table does)."""
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        return {}
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2e)
    headers = [struct.unpack_from('<10I', data, shoff + k * shentsize) for k in range(shnum)]
    out = {}
    for sh in headers:
        if sh[1] != 2:
            continue
        strtab = headers[sh[6]][4]
        for off in range(sh[4] + 16, sh[4] + sh[5], 16):
            name, value = struct.unpack_from('<II', data, off)
            if name:
                out.setdefault(data[strtab + name:data.index(b'\0', strtab + name)].decode(errors='replace'), value)
    return out


def linker_script_for(build_dir):
    """firmware/app*.ld the build of build_dir linked with (the choice of build.py's
    linker_script(), from the CONFIG_* names of gen/config.h)."""
    cfg = (Path(build_dir) / 'gen' / 'config.h')
    if not cfg.exists():
        raise ValueError(f'{cfg} not found: cannot tell which linker script the build used')
    enabled = set(re.findall(r'^#define CONFIG_(\w+) 1$', cfg.read_text(), re.M))
    name = ('app_wifi_nonfree.ld' if 'WIFI_NONFREE' in enabled else 'app_dock.ld' if 'DOCK' in enabled
            else 'app_emu.ld' if 'EMU' in enabled else 'app.ld')
    return Path(__file__).resolve().parents[2] / 'firmware' / name


def load_map(build_dir):
    """parse_map of build/<defconfig>/modd.map, whichever linker wrote it. An lld map gets its
    regions from the linker script's MEMORY block and its symbols from modd.elf."""
    build_dir = Path(build_dir)
    text = (build_dir / 'modd.map').read_text(errors='replace')
    if not is_lld(text):
        return parse_map(text)
    elf = build_dir / 'modd.elf'
    symbols = elf_symbols(elf.read_bytes()) if elf.exists() else {}
    regions = parse_ld_regions(linker_script_for(build_dir).read_text(), symbols)
    m = parse_map(text, regions, symbols)
    for name, addr, size in m['sections']:   # a wrong script would leave placed sections outside
        if size and addr and not name.startswith('.debug') and region_of(regions, addr) is None:
            raise ValueError(f'section {name} at 0x{addr:x} is in no region of {linker_script_for(build_dir).name}')
    return m


def parse_map(text, regions=None, symbols=None):
    """{'regions': {name: (origin, length)}, 'sections': [(name, addr, size)], 'symbols': {name: value}}
    from a GNU ld map: the Memory Configuration table, then the top-level output sections and
    the linker-defined symbols of the memory map. An lld map has neither table nor reliable
    symbol values: pass its regions and symbols (see load_map)."""
    if is_lld(text):
        if regions is None:
            raise ValueError('an lld map has no Memory Configuration table: pass the regions (load_map)')
        return {'regions': dict(regions), 'sections': parse_lld_sections(text), 'symbols': dict(symbols or {})}
    regions, sections, symbols = {}, [], {}
    lines = text.splitlines()
    i = 0
    while i < len(lines) and not lines[i].startswith('Memory Configuration'):
        i += 1
    for i in range(i + 1, len(lines)):
        line = lines[i]
        if line.startswith('Linker script and memory map'):
            break
        m = _REGION.match(line)
        if m and m.group(1) != 'Name':
            regions[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    for j in range(i + 1, len(lines)):
        line = lines[j]
        m = _SECTION.match(line)
        if m:
            sections.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
            continue
        m = _SECTION_NAME.match(line)
        if m and j + 1 < len(lines):
            m2 = _ADDR_SIZE.match(lines[j + 1])
            if m2:
                sections.append((m.group(1), int(m2.group(1), 16), int(m2.group(2), 16)))
            continue
        m = _SYMBOL.match(line)
        if m:
            symbols[m.group(2)] = int(m.group(1), 16)
    return {'regions': regions, 'sections': sections, 'symbols': symbols}


def elf_objects(data):
    """[(addr, size, name)] of the sized data objects (STT_OBJECT) in a 32-bit little-endian ELF."""
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        return []
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2e)
    headers = [struct.unpack_from('<10I', data, shoff + k * shentsize) for k in range(shnum)]
    out = []
    for sh in headers:
        if sh[1] != 2:                       # SHT_SYMTAB
            continue
        strtab = headers[sh[6]][4]
        for off in range(sh[4], sh[4] + sh[5], 16):
            name, value, size, info, _other, _shndx = struct.unpack_from('<IIIBBH', data, off)
            if size and info & 0xf == 1:     # STT_OBJECT
                end = data.index(b'\0', strtab + name)
                out.append((value, size, data[strtab + name:end].decode(errors='replace')))
    return out


def region_of(regions, addr):
    for name, (origin, length) in regions.items():
        if origin <= addr < origin + length:
            return name
    return None


def kib(n):
    """Bytes below 1 KiB, KiB with one decimal above."""
    return f'{n}B' if abs(n) < 1024 else f'{n / 1024:.1f}K'


def analyse(m, objects=(), body_size=None, arena_min_left=None):
    """The report's figures: regions with their sections, budgets, stacks, objects per region."""
    regions, symbols = m['regions'], m['symbols']
    out = {'regions': [], 'budgets': [], 'stacks': [], 'top': {}, 'warnings': [], 'errors': []}
    by_region = {name: [] for name in regions}
    for name, addr, size in m['sections']:
        r = region_of(regions, addr)
        if r and size:
            by_region[r].append((name, size))
    for name, (origin, length) in regions.items():
        used = sum(size for _n, size in by_region[name])
        if name == 'XIP' and body_size is not None:
            used = body_size              # the flash image: .text plus the load images of RAM code and data
        out['regions'].append({'name': name, 'used': used, 'size': length,
                               'sections': by_region[name]})
        if length and used * 100 >= WARN_PCT * length:
            out['warnings'].append(f'{name} is {used * 100 // length}% full ({kib(length - used)} free)')
        if name == 'RAM' and length - used < RAM_MIN_FREE:
            out['errors'].append(f'RAM has {kib(length - used)} free, below the {kib(RAM_MIN_FREE)} minimum '
                                 f'(MODD_RAM_MIN_FREE): move buffers to the POOL or shrink statics')

    def span(lo, hi):
        if lo in symbols and hi in symbols:
            return symbols[hi] - symbols[lo]
        return None

    arena = span('_resource_start', '_resource_end')
    if arena is not None:
        fb = next((size for name, addr, size in m['sections'] if name == '.framebuffer'
                   and symbols['_resource_start'] <= addr < symbols['_resource_end']), 0)
        need = symbols.get('_resource_minimum')
        b = {'name': 'audio arena', 'size': arena, 'need': need, 'borrowed': fb}
        out['budgets'].append(b)
        if need is not None and arena - need < WARN_MARGIN:
            out['warnings'].append(f'audio arena {kib(arena - need)} above its minimum')
        if need is None:
            left = arena - fb
            floor = ARENA_MIN_LEFT if arena_min_left is None else arena_min_left
            if left < floor:
                out['errors'].append(f'audio arena has {kib(left)} left of {kib(arena)}, below the '
                                     f'{kib(floor)} floor (CONFIG_ARENA_MIN_LEFT; no _resource_minimum in this '
                                     f'linker script): shrink the framebuffer or the statics')
            elif left < floor + WARN_MARGIN:
                out['warnings'].append(f'audio arena has {kib(left)} left of {kib(arena)}, within '
                                       f'{kib(WARN_MARGIN)} of the {kib(floor)} floor')
    for label, lo, hi in (('RAM code tail', '_resource_low_start', '_resource_low_end'),
                          ('RAM tail', '_resource_data_start', '_resource_data_end'),
                          ('NOINIT tail', '_resource_retained_start', '_resource_retained_end')):
        size = span(lo, hi)
        if size is not None:
            out['budgets'].append({'name': label, 'size': size, 'need': None, 'borrowed': 0})

    for label, lo, hi in (('CPU0 user', '_ustack_lo', '_ustack_top'),
                          ('CPU0 system', '_sstack_lo', '_sstack_top')):
        size = span(lo, hi)
        if size is not None:
            out['stacks'].append((label, size))
    cpu1 = span('_cpu1_stacks_lo', '_cpu1_stacks_hi')
    if cpu1:
        out['stacks'].append(('CPU1', cpu1))
    for addr, size, name in sorted(objects, key=lambda o: -o[1]):
        if (name.endswith('_stack') or name.endswith('_stacks')) and size >= 256:
            out['stacks'].append((name, size))

    for addr, size, name in objects:
        r = region_of(regions, addr)
        if r:
            out['top'].setdefault(r, []).append((size, name))
    for r in out['top']:
        out['top'][r].sort(key=lambda t: (-t[0], t[1]))
    return out


def _wrapped(head, items, width=100):
    """'head a, b, c' folded under the head at width columns."""
    text = ', '.join(item.replace(' ', '\xa0') for item in items)   # an item never splits
    return textwrap.fill(text, width=width, initial_indent=head, subsequent_indent=' ' * len(head),
                         break_long_words=False, break_on_hyphens=False).replace('\xa0', ' ')


def format_report(a, n=8, title='', extra=()):
    lines = [f'memory: {title}' if title else 'memory:']
    lines.append(f'  {"region":<8} {"used":>8} {"size":>8} {"free":>8} {"use":>4}  holds')
    for r in a['regions']:
        pct = r['used'] * 100 // r['size'] if r['size'] else 0
        flag = '!' if pct >= WARN_PCT else ' '
        lines.append(f'  {r["name"]:<8} {kib(r["used"]):>8} {kib(r["size"]):>8} '
                     f'{kib(r["size"] - r["used"]):>8} {pct:>3}%{flag} {_ROLE.get(r["name"], "")}')
        if r['name'] not in ('XIP',) and len(r['sections']) > 1:
            lines.append(_wrapped(' ' * 11, [f'{name} {kib(size)}' for name, size in r['sections']]))
    if a['budgets']:
        lines.append('  budgets')
        for b in a['budgets']:
            text = f'    {b["name"]:<14} {kib(b["size"]):>8}'
            if b['name'] == 'audio arena':
                if b['need'] is not None:
                    text += f'  needs {kib(b["need"])}, {kib(b["size"] - b["need"])} to spare'
                else:
                    text += '  no minimum in this linker script'
                if b['borrowed']:
                    text += (f'; shadow framebuffer {kib(b["borrowed"])} inside it, '
                             f'{kib(b["size"] - b["borrowed"])} left')
            lines.append(text)
    if a['stacks']:
        lines.append(_wrapped('  stacks  ', [f'{name} {kib(size)}' for name, size in a['stacks']]))
    for region in ('RAM', 'POOL', 'DOCK'):
        top = a['top'].get(region)
        if top and n:
            lines.append(_wrapped(f'  largest in {region}: ',
                                  [f'{name} {kib(size)}' for size, name in top[:n]]))
    lines.extend(extra)
    for w in a['warnings']:
        lines.append(f'  warning: {w}')
    for e in a['errors']:
        lines.append(f'  ERROR: {e}')
    return '\n'.join(lines)


def analyse_build(build_dir):
    """The figures for build/<defconfig>/ (reads modd.map, modd.elf and body.bin)."""
    build_dir = Path(build_dir)
    m = load_map(build_dir)
    elf = build_dir / 'modd.elf'
    body = build_dir / 'body.bin'
    objects = elf_objects(elf.read_bytes()) if elf.exists() else []
    config = build_dir / 'gen' / 'config.h'
    floor = re.search(r'^#define CONFIG_ARENA_MIN_LEFT (\d+)', config.read_text(), re.M) if config.exists() else None
    return analyse(m, objects, body.stat().st_size if body.exists() else None,
                   arena_min_left=int(floor.group(1)) if floor else None)


def report(build_dir, n=8):
    """The report for build/<defconfig>/ as text."""
    build_dir = Path(build_dir)
    a = analyse_build(build_dir)
    try:
        title = str(build_dir.resolve().relative_to(Path.cwd()))
    except ValueError:
        title = str(build_dir)
    try:
        import mem_files            # which components and files fill RAM, POOL and XIP
        extra = [mem_files.format_files(build_dir, n)]
    except Exception as e:  # noqa: BLE001
        extra = [f'  who fills it: skipped ({e})']
    return format_report(a, n, title, extra)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('build_dir', nargs='?', default='build/modd', help='build/<defconfig> (default build/modd)')
    ap.add_argument('-n', type=int, default=8, help='largest objects listed per region (default 8)')
    args = ap.parse_args(argv)
    if not (Path(args.build_dir) / 'modd.map').exists():
        sys.exit(f'{args.build_dir}/modd.map not found: build first (task build)')
    print(report(args.build_dir, args.n))
    errors = analyse_build(args.build_dir)['errors']
    if errors:
        sys.exit(1)


if __name__ == '__main__':
    main()
