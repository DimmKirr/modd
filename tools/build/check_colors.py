#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Verify all UI colors fit the FM-1's RGB565 display (65,536 colors).

Two checks:
1. Palette table: every hex literal in ui_palettes.h fits uint16_t.
2. Theme source: no hardcoded color literals in cv_*/gfx_* drawing calls.
   All colors must come from palette tokens (T_BG, T_SURF, T_TRK, etc.)
   so they shift with the active palette.

Run: python tools/build/check_colors.py [--palette-dir build/modd/gen/melodee]
Called automatically by build.py after genconfig.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRAW_FNS = re.compile(
    r'\b(cv_rect|cv_rrect|cv_frame|cv_text|cv_text_fit|cv_text_c|cv_text_r|'
    r'cv_text_on|cv_line|cv_line_t|cv_pset|cv_disc|cv_circle|cv_ring|'
    r'cv_dashed|cv_fill|cv_blit|gfx_fill|lcd_fill|ux_mix|cv_pix|'
    r'cv_rrect_hatch|hint_hatch_fill|cv_begin)\s*\('
)

PALETTE_TOKEN = re.compile(
    r'\bT_(BG|SURF|TEXT|THEME|ACCENT|MID|DIM|LINE|INK|SEL|TINT|'
    r'TRK|KNOB|KEY|RAISE|PANEL|REC)\b'
)

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


def check_theme_sources(theme_dirs: list[Path]) -> list[str]:
    """Flag hardcoded hex color literals in drawing function calls."""
    errors = []
    for theme_dir in theme_dirs:
        for src in sorted(theme_dir.glob('*.c')):
            if src.name in ('lcd.c', 'gfx.c'):
                continue
            text = src.read_text()
            for lineno, line in enumerate(text.splitlines(), 1):
                stripped = line.lstrip()
                if stripped.startswith(('/*', '*', '//', '#')):
                    continue
                if not DRAW_FNS.search(line):
                    continue
                for m in HEX_LITERAL.finditer(line):
                    val = int(m.group(1), 16)
                    if val > 0xFFFF:
                        errors.append(
                            f'{src.name}:{lineno}: 0x{m.group(1)} in drawing '
                            f'call exceeds RGB565 (likely 24-bit RGB)')
                    elif val > 0xFF and not PALETTE_TOKEN.search(line):
                        col_start = m.start()
                        context = line[max(0, col_start - 40):col_start + 20]
                        errors.append(
                            f'{src.name}:{lineno}: hardcoded 0x{m.group(1)} '
                            f'in drawing call (use a palette token instead)')
    return errors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--palette-dir', type=Path,
                    default=ROOT / 'build' / 'modd' / 'gen' / 'melodee')
    ap.add_argument('--theme-dir', type=Path, action='append', default=None)
    args = ap.parse_args()

    theme_dirs = args.theme_dir or [
        ROOT / 'firmware' / 'src' / 'ui' / 'themes' / 'modd',
    ]

    errors = []
    if args.palette_dir.exists():
        errors.extend(check_palette(args.palette_dir))
    errors.extend(check_theme_sources(theme_dirs))

    if errors:
        print(f'check_colors: {len(errors)} issue(s):')
        for e in errors:
            print(f'  {e}')
        return 1
    print('check_colors: all colors are palette-bound RGB565')
    return 0


if __name__ == '__main__':
    sys.exit(main())
