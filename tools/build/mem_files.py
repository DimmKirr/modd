# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Who fills each memory region: the part of the memory report that names components and files.

modd is a unity build, so the linker map has one object (modd.o) for all of the firmware and
no source file per symbol. Each symbol is therefore matched by name to the file that defines
it (a definition at column 0 under firmware/, the generated headers in build/<defconfig>/gen,
lwIP, FatFs and the upstream trees). LVGL is built as a library, so its objects are grouped by
archive member. The figures are approximate: globals that clang merged (`.L_MergedGlobals.*`)
and names no file defines show up as '(unattributed)'.

    uv run python tools/build/mem_files.py build/modd [-n 12]
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mem_report  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
REGIONS = ('RAM', 'POOL', 'XIP')      # the regions worth a breakdown: where the budget is tight
UNATTRIBUTED = '(unattributed)'
MERGED = '(clang-merged globals)'
_POOL_SECTIONS = ('.pool', '.pool_net', '.dock_pool')
_LWIP_PREFIX = ('memp_', 'dns_', 'tcp_', 'etharp_', 'ip_', 'ip4', 'dhcp_', 'pbuf', 'netif', 'lwip',
                'udp_', 'icmp_', 'raw_', 'sys_', 'mem_', 'autoip', 'igmp', 'nd6', 'inet_')
_SKIP_FIRST = ('__attribute__', 'POOL_BSS', 'sizeof')


def source_roots(build_dir):
    fw = ROOT / 'firmware'
    roots = [fw / 'src', fw / 'adapters', fw / 'hal', Path(build_dir) / 'gen',
             fw / 'vendor' / 'lwip', fw / 'vendor' / 'fatfs']
    roots += sorted(fw.glob('upstream/*/firmware/src')) + sorted(fw.glob('upstream/*/src'))
    return [r for r in roots if r.is_dir()]


def _label_path(path, build_dir):
    p = Path(path)
    try:
        return str(p.relative_to(ROOT / 'firmware'))
    except ValueError:
        return 'gen/' + p.name


def index_sources(build_dir):
    """({name: file}, [(file, text)]): where each top-level function or variable is defined."""
    defs, texts = {}, []
    start = re.compile(r'(static|const|volatile|unsigned|int|uint|char|void|float|short|long|signed|size_t|bool|'
                       r'[A-Za-z_]\w*_t\b|[A-Za-z_]\w*\s+\*?\w+)')
    for root in source_roots(build_dir):
        for p in sorted(list(root.rglob('*.c')) + list(root.rglob('*.h'))):
            rel = _label_path(p, build_dir)
            text = p.read_text(errors='replace')
            texts.append((rel, text))
            for line in text.splitlines():
                if not line or line[0] in ' \t#}/*{)' or line.startswith('typedef') or not start.match(line):
                    continue
                if '(' in line.split('=')[0]:
                    name = None
                    for m in re.finditer(r'([A-Za-z_]\w*)\s*\(', line):
                        name = m.group(1)
                        if name not in _SKIP_FIRST:
                            break
                    if name:
                        defs.setdefault(name, rel)
                else:
                    names = [m.group(1) for m in re.finditer(r'([A-Za-z_]\w*)\s*(?=[\[=;,])', line)]
                    for name in names:
                        defs.setdefault(name, rel)
    return defs, texts


def parse_input_sections(text):
    """[(section, addr, size, object)] of the placed input sections in a GNU ld map."""
    lines = text.splitlines()
    start = next((i for i, l in enumerate(lines) if l.startswith('Linker script and memory map')), None)
    if start is None:
        return []
    out, i = [], start + 1
    while i < len(lines):
        m1 = re.match(r'^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$', lines[i])
        m0 = re.match(r'^ (\S+)\s*$', lines[i])
        if m1:
            out.append((m1.group(1), int(m1.group(2), 16), int(m1.group(3), 16), m1.group(4)))
        elif m0 and i + 1 < len(lines):
            m2 = re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$', lines[i + 1])
            if m2:
                out.append((m0.group(1), int(m2.group(1), 16), int(m2.group(2), 16), m2.group(3)))
                i += 1
        i += 1
    return [o for o in out if o[1] and o[2]]


def _library_component(obj):
    m = re.search(r'liblvgl\.a\(([^)]+)\)', obj)
    if m:
        return 'lvgl ' + m.group(1).split('_')[0]
    if 'usb_sdk' in obj:
        return 'usb_sdk (JieLi SDK)'
    if Path(obj).name != 'modd.o':
        return Path(obj).name
    return None


def _component(rel):
    p = rel.split('/')
    if p[0] == 'src' and len(p) > 2:
        return 'src/' + p[1]
    if p[0] in ('adapters', 'hal', 'gen'):
        return p[0]
    if p[0] in ('vendor', 'upstream') and len(p) > 1:
        return '/'.join(p[:2])
    return rel


def attribute(build_dir):
    """({region: {component: bytes}}, {region: {file: bytes}}, {region: size}) for build/<defconfig>/."""
    build_dir = Path(build_dir)
    m = mem_report.load_map(build_dir)
    regions = m['regions']
    map_text = (build_dir / 'modd.map').read_text(errors='replace')
    inputs = (mem_report.parse_lld_inputs(map_text) if mem_report.is_lld(map_text)
              else parse_input_sections(map_text))
    elf = build_dir / 'modd.elf'
    objects = sorted(mem_report.elf_objects(elf.read_bytes())) if elf.exists() else []
    defs, texts = index_sources(build_dir)
    cache = {}

    def find(name):
        if name in defs:
            return defs[name]
        if name not in cache:
            pat = re.compile(r'^[^\s#/*].*(?:\b' + re.escape(name) + r'\b\s*(?:[\[\(=;]|__attribute__)|\(\s*'
                             + re.escape(name) + r'\s*,)', re.M)
            cache[name] = next((rel for rel, t in texts if pat.search(t)), None)
        return cache[name]

    comp = defaultdict(lambda: defaultdict(int))
    files = defaultdict(lambda: defaultdict(int))

    def add(region, component, file, size):
        comp[region][component] += size
        files[region][file] += size

    def place(region, name, size):
        # statics inside a function are named `function.variable`: try each part
        rel = None
        for part in ([name] + name.split('.') if name and '.' in name else [name]):
            if part and not part.startswith(('L_', 'str1')):
                rel = find(part)
                if rel:
                    break
        if rel is None and name and name.startswith(_LWIP_PREFIX):
            add(region, 'vendor/lwip', 'vendor/lwip', size)
        elif rel is None and name and 'MergedGlobals' in name:
            add(region, MERGED, MERGED, size)
        elif rel is None:
            add(region, UNATTRIBUTED, UNATTRIBUTED, size)
        else:
            add(region, _component(rel), rel, size)

    for sec, addr, size, obj in inputs:
        region = mem_report.region_of(regions, addr)
        if region not in REGIONS and region != 'POOL':
            continue
        lib = _library_component(obj)
        if lib:
            add(region, lib, lib, size)
        elif sec in _POOL_SECTIONS:
            covered = 0
            for a, sz, name in objects:
                if addr <= a < addr + size:
                    place(region, name, sz)
                    covered += sz
            if size > covered:
                add(region, '(pool padding)', '(pool padding)', size - covered)
        else:
            names = [p for p in sec.split('.')[2:] if p and not p.isdigit()]
            name = next((n for n in names if n in defs), None) or next(
                (n for n in names if find(n)), names[0] if names else '')
            place(region, name, size)
    return comp, files, {name: length for name, (_o, length) in regions.items()}


def format_files(build_dir, n=8):
    """Report lines: per region the components and the largest files, as K and % of the region."""
    comp, files, sizes = attribute(build_dir)
    lines = ['  who fills it (symbols matched to the file that defines them; approximate)']
    for region in REGIONS:
        size = sizes.get(region)
        if not size or not comp.get(region):
            continue
        for title, table in (('', comp[region]), (' files', files[region])):
            top = sorted(table.items(), key=lambda kv: -kv[1])[:n]
            entries = [(Path(k).name if title else k, v) for k, v in top if v * 100 >= size / 2]
            if not entries:
                continue
            lines.append(f'  {region}{title}:')
            name_w = max(len(name) for name, _ in entries)
            for name, v in entries:
                lines.append(f'    {name:<{name_w}}  {mem_report.kib(v):>7}  {v * 100 / size:4.1f}%')
    return '\n'.join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('build_dir', nargs='?', default='build/modd')
    ap.add_argument('-n', type=int, default=8, help='entries per list (default 8)')
    args = ap.parse_args(argv)
    if not (Path(args.build_dir) / 'modd.map').exists():
        sys.exit(f'{args.build_dir}/modd.map not found: build first (task build)')
    print(format_files(args.build_dir, args.n))


if __name__ == '__main__':
    main()
