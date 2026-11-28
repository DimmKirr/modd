#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Convert assets/logo.svg to boot_logo.h: pure-Python SVG rasterizer.

Parses SVG path geometry, rasterizes with 4x supersampling for clean
antialiased edges, and emits an RLE-compressed RGB565 C header.

No external dependencies: stdlib xml.etree, math, struct, re only.
Colors come from the SVG fill attributes; the background is the FM-1
body cream (#E8E4DF). The SVG is the single source of truth.

BOOT_BG must stay in step with the splash fill colour in
firmware/ports/felucca/shell/shell.c (fsh_splash_render).

RLE format: pairs of (count, rgb565_be). Count is 1 byte (1-255 repeats);
a zero count byte means end-of-row. One row = 240 pixels.
"""
import math
import re
import struct
import sys
from pathlib import Path
from xml.etree import ElementTree

ROOT = Path(__file__).resolve().parents[2]

BOOT_BG = (0xE8, 0xE4, 0xDF)
LCD_W = 240
CANVAS_H = 240
SS = 4


# ---------------------------------------------------------------------------
# colour helpers
# ---------------------------------------------------------------------------

def rgb565_be(r, g, b):
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack('>H', v)


def parse_hex(s):
    s = s.lstrip('#')
    return int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)


def blend(fg, bg, t):
    return tuple(int(f * t + b * (1 - t) + 0.5) for f, b in zip(fg, bg))


# ---------------------------------------------------------------------------
# affine transforms (6-tuple: a, c, e, b, d, f)
#   x' = a*x + c*y + e
#   y' = b*x + d*y + f
# ---------------------------------------------------------------------------

def _identity():
    return (1.0, 0.0, 0.0, 0.0, 1.0, 0.0)

def _translate(tx, ty=0.0):
    return (1.0, 0.0, tx, 0.0, 1.0, ty)

def _scale(sx, sy=None):
    return (sx, 0.0, 0.0, 0.0, sy if sy is not None else sx, 0.0)

def _mul(a, b):
    return (
        a[0]*b[0] + a[1]*b[3],  a[0]*b[1] + a[1]*b[4],  a[0]*b[2] + a[1]*b[5] + a[2],
        a[3]*b[0] + a[4]*b[3],  a[3]*b[1] + a[4]*b[4],  a[3]*b[2] + a[4]*b[5] + a[5],
    )

def _apply(m, x, y):
    return m[0]*x + m[1]*y + m[2], m[3]*x + m[4]*y + m[5]

def _parse_xform(s):
    m = _identity()
    for name, args in re.findall(r'(\w+)\(([^)]+)\)', s):
        v = [float(x) for x in args.replace(',', ' ').split()]
        if name == 'translate':
            m = _mul(m, _translate(v[0], v[1] if len(v) > 1 else 0))
        elif name == 'scale':
            m = _mul(m, _scale(v[0], v[1] if len(v) > 1 else None))
    return m


# ---------------------------------------------------------------------------
# SVG path parser (handles m l h v q t z, relative and absolute)
# ---------------------------------------------------------------------------

_PATH_RE = re.compile(r'[A-Za-z]|[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?')


def _flatten_qbez(x0, y0, cx, cy, x1, y1, tol=0.25):
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    if length < 1e-10:
        return [(x0, y0), (x1, y1)]
    dist = abs((cx - x0) * dy - (cy - y0) * dx) / length
    if dist < tol:
        return [(x0, y0), (x1, y1)]
    m01x, m01y = (x0 + cx) / 2, (y0 + cy) / 2
    m12x, m12y = (cx + x1) / 2, (cy + y1) / 2
    mx, my = (m01x + m12x) / 2, (m01y + m12y) / 2
    left = _flatten_qbez(x0, y0, m01x, m01y, mx, my, tol)
    right = _flatten_qbez(mx, my, m12x, m12y, x1, y1, tol)
    return left + right[1:]


def _parse_path(d):
    """Parse SVG path 'd' attribute into subpaths (lists of (x,y) points)."""
    toks = _PATH_RE.findall(d)
    subpaths = []
    cur = []
    cx = cy = sx = sy = 0.0
    qcx = qcy = None
    last = None
    i = 0

    def num():
        nonlocal i
        v = float(toks[i]); i += 1; return v

    while i < len(toks):
        if toks[i].isalpha():
            cmd = toks[i]; i += 1
        elif last:
            cmd = 'l' if last == 'm' else 'L' if last == 'M' else last
        else:
            break

        if cmd in 'Mm':
            if cur:
                subpaths.append(cur); cur = []
            dx, dy = num(), num()
            if cmd == 'm': cx += dx; cy += dy
            else: cx, cy = dx, dy
            sx, sy = cx, cy
            cur.append((cx, cy))
            qcx = qcy = None

        elif cmd in 'Ll':
            dx, dy = num(), num()
            if cmd == 'l': cx += dx; cy += dy
            else: cx, cy = dx, dy
            cur.append((cx, cy))
            qcx = qcy = None

        elif cmd in 'Hh':
            dx = num()
            cx = cx + dx if cmd == 'h' else dx
            cur.append((cx, cy))
            qcx = qcy = None

        elif cmd in 'Vv':
            dy = num()
            cy = cy + dy if cmd == 'v' else dy
            cur.append((cx, cy))
            qcx = qcy = None

        elif cmd in 'Qq':
            d1, d2, d3, d4 = num(), num(), num(), num()
            if cmd == 'q':
                px, py, ex, ey = cx + d1, cy + d2, cx + d3, cy + d4
            else:
                px, py, ex, ey = d1, d2, d3, d4
            pts = _flatten_qbez(cx, cy, px, py, ex, ey)
            cur.extend(pts[1:])
            qcx, qcy = px, py
            cx, cy = ex, ey

        elif cmd in 'Tt':
            dx, dy = num(), num()
            if qcx is not None:
                px, py = 2 * cx - qcx, 2 * cy - qcy
            else:
                px, py = cx, cy
            if cmd == 't':
                ex, ey = cx + dx, cy + dy
            else:
                ex, ey = dx, dy
            pts = _flatten_qbez(cx, cy, px, py, ex, ey)
            cur.extend(pts[1:])
            qcx, qcy = px, py
            cx, cy = ex, ey

        elif cmd in 'Zz':
            if cur and (abs(cx - sx) > 1e-6 or abs(cy - sy) > 1e-6):
                cur.append((sx, sy))
            subpaths.append(cur); cur = []
            cx, cy = sx, sy
            qcx = qcy = None

        last = cmd

    if cur:
        subpaths.append(cur)
    return subpaths


# ---------------------------------------------------------------------------
# scanline rasterizer (even-odd fill rule)
# ---------------------------------------------------------------------------

def _rasterize(subpaths, fill, w, h, grid):
    edges = []
    for sp in subpaths:
        for j in range(len(sp) - 1):
            x0, y0 = sp[j]
            x1, y1 = sp[j + 1]
            if abs(y0 - y1) < 1e-6:
                continue
            if y0 > y1:
                x0, y0, x1, y1 = x1, y1, x0, y0
            edges.append((y0, y1, x0, x1))

    for y in range(h):
        yf = y + 0.5
        xs = []
        for ey0, ey1, ex0, ex1 in edges:
            if ey0 <= yf < ey1:
                t = (yf - ey0) / (ey1 - ey0)
                xs.append(ex0 + t * (ex1 - ex0))
        xs.sort()
        for j in range(0, len(xs) - 1, 2):
            x_start = max(0, int(math.ceil(xs[j] - 0.5)))
            x_end = min(w - 1, int(math.ceil(xs[j + 1] - 0.5)) - 1)
            for x in range(x_start, x_end + 1):
                grid[y][x] = fill


# ---------------------------------------------------------------------------
# RLE encoder
# ---------------------------------------------------------------------------

def _rle_row(pixels, width):
    out = bytearray()
    i = 0
    while i < width:
        val = pixels[i]
        run = 1
        while i + run < width and pixels[i + run] == val and run < 255:
            run += 1
        out.append(run)
        out.extend(val)
        i += run
    out.append(0)
    return bytes(out)


# ---------------------------------------------------------------------------
# main conversion
# ---------------------------------------------------------------------------

SVG_NS = '{http://www.w3.org/2000/svg}'


def convert(svg_path, dst, width=LCD_W, canvas=CANVAS_H, ss=SS):
    tree = ElementTree.parse(svg_path)
    root = tree.getroot()

    vb = root.get('viewBox', '0 0 100 100').split()
    vb_x, vb_y, vb_w, vb_h = (float(v) for v in vb)

    img_h = int(vb_h * width / vb_w)
    vb_mat = _mul(_scale(width / vb_w), _translate(-vb_x, -vb_y))
    vb_mat_ss = _mul(_scale(ss), vb_mat)

    sw, sh = width * ss, img_h * ss
    grid = [[None] * sw for _ in range(sh)]

    paths = root.findall(f'.//{SVG_NS}path') or root.findall('.//path')
    for el in paths:
        d = el.get('d', '')
        fill = parse_hex(el.get('fill', '#000000'))
        local_xf = _parse_xform(el.get('transform', ''))
        combined = _mul(vb_mat_ss, local_xf)

        subpaths = _parse_path(d)
        xformed = [[_apply(combined, px, py) for px, py in sp] for sp in subpaths]
        _rasterize(xformed, fill, sw, sh, grid)

    bg565 = rgb565_be(*BOOT_BG)
    y_off = (canvas - img_h) // 2
    ss2 = ss * ss

    rle_data = bytearray()
    for y in range(canvas):
        if y < y_off or y >= y_off + img_h:
            rle_data.extend(_rle_row([bg565] * width, width))
            continue
        iy = y - y_off
        row_px = []
        for x in range(width):
            colors = {}
            for sy in range(ss):
                for sx in range(ss):
                    c = grid[iy * ss + sy][x * ss + sx]
                    if c is not None:
                        colors[c] = colors.get(c, 0) + 1
            if not colors:
                row_px.append(bg565)
            else:
                ink, count = max(colors.items(), key=lambda kv: kv[1])
                row_px.append(rgb565_be(*blend(ink, BOOT_BG, count / ss2)))
        rle_data.extend(_rle_row(row_px, width))

    dst.parent.mkdir(parents=True, exist_ok=True)
    with open(dst, 'w') as f:
        f.write('/* generated by tools/codegen/logo_convert.py from logo.svg */\n')
        f.write(f'#define BOOT_LOGO_W {width}\n')
        f.write(f'#define BOOT_LOGO_H {canvas}\n')
        f.write(f'#define BOOT_LOGO_IMG_H {img_h}\n')
        f.write(f'#define BOOT_LOGO_Y_OFF {y_off}\n')
        bg565_val = struct.unpack('>H', bg565)[0]
        f.write(f'#define BOOT_LOGO_BG 0x{bg565_val:04X}'
                f'  /* #{BOOT_BG[0]:02X}{BOOT_BG[1]:02X}{BOOT_BG[2]:02X} */\n')
        f.write(f'static const uint8_t boot_logo_rle[{len(rle_data)}] = {{\n')
        for i in range(0, len(rle_data), 16):
            chunk = rle_data[i:i + 16]
            f.write('    ' + ','.join(f'0x{b:02X}' for b in chunk) + ',\n')
        f.write('};\n')

    print(f'logo: {svg_path.name} -> {dst.relative_to(ROOT)} '
          f'({width}x{img_h} in {canvas}x{canvas}, {len(rle_data)} bytes RLE, {ss}x AA)')


if __name__ == '__main__':
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'assets' / 'logo.svg'
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'build' / 'gen' / 'boot_logo.h'
    convert(src, dst)
