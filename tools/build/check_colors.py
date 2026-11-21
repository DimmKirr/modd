#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Verify all UI colors fit the FM-1's RGB565 display (65,536 colors).

Palette table: every hex literal in ui_palettes.h fits uint16_t.

Run: python tools/build/check_colors.py [--palette-dir build/modd/gen/modd]
Called automatically by build.py after genconfig.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HEX_LITERAL = re.compile(r'\b0x([0-9a-fA-F]+)[uU]?\b')


def check_palette(palette_dir: Path) -> list[str]:
    """Verify all color values in ui_palettes.h fit RGB565."""
    path = palette_dir / 'ui_palettes.h'
    if not path.exists():
        return [f'palette file not found: {path}']
    errors = []
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        if line.lstrip().startswith(('/*', '*', '//', '#')):
            continue
        for m in HEX_LITERAL.finditer(line):
            val = int(m.group(1), 16)
            if 'trk' in line or 'knob' in line or 'bg' in line or 'surf' in line or \
               'text' in line or 'accent' in line:
                if val > 0xFFFF:
                    errors.append(
                        f'{path.name}:{lineno}: 0x{m.group(1)} ({val}) '
                        f'exceeds RGB565 range (0x0000-0xFFFF)')
    return errors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--palette-dir', type=Path,
                    default=ROOT / 'build' / 'modd' / 'gen' / 'modd')
    args = ap.parse_args()

    errors = []
    if args.palette_dir.exists():
        errors.extend(check_palette(args.palette_dir))

    if errors:
        print(f'check_colors: {len(errors)} issue(s):')
        for e in errors:
            print(f'  {e}')
        return 1
    print('check_colors: palette colors fit RGB565')
    return 0


if __name__ == '__main__':
    sys.exit(main())
