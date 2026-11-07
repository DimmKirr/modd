#!/usr/bin/env python3
#  SPDX-License-Identifier: GPL-3.0-only
#  Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Generate the pixel-art weather scenes: firmware/src/ui/themes/felucca/weather_art.h

Replaces the old 1-bit 32x32 glyphs. Each WMO condition gets a 64x48 indexed
sprite drawn from a shared 16-colour palette, plus a two-colour sky the page
dithers into a vertical gradient.

Sprites are composed from a few authored parts (clouds as ASCII art, sun and
bolt generated so they stay symmetrical) rather than hand-placing every pixel.

Usage:
  python3 tools/codegen/gen_weather_art.py header  > .../weather_art.h
  python3 tools/codegen/gen_weather_art.py preview [out.png]
"""
import sys
import datetime

W, H = 40, 30                      # sprite size, drawn at 2x so the art's
                                   # pixels match the chunk of the 5x7 text
TRANSPARENT = 0

# ── palette ─────────────────────────────────────────────────────────────────
# Index 0 is transparent. Keep this under 16 entries: the sprite is 4bpp.
PALETTE = [
    ("TRANSPARENT", (0, 0, 0)),
    ("SUN_CORE",    (255, 236, 170)),
    ("SUN_MID",     (255, 200, 77)),
    ("SUN_DEEP",    (238, 150, 30)),
    ("CLOUD_HI",    (255, 255, 255)),
    ("CLOUD_MID",   (216, 222, 233)),
    ("CLOUD_LO",    (167, 178, 196)),
    ("CLOUD_SHADE", (120, 133, 157)),
    ("RAIN_HI",     (158, 216, 255)),
    ("RAIN_LO",     (79, 163, 224)),
    ("SNOW",        (255, 255, 255)),
    ("BOLT_HI",     (255, 227, 77)),
    ("BOLT_LO",     (245, 166, 35)),
    ("INK",         (27, 36, 48)),
    ("FOG",         (201, 206, 214)),
    ("MUTED",       (136, 147, 165)),
]
IDX = {name: i for i, (name, _) in enumerate(PALETTE)}


def rgb_to_565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# ── authored parts ──────────────────────────────────────────────────────────
# Legend: '.' transparent, 'H' highlight, 'M' mid, 'L' low, 'S' shade, 'o' ink
CLOUD_CHARS = {'.': TRANSPARENT, 'H': IDX["CLOUD_HI"], 'M': IDX["CLOUD_MID"],
               'L': IDX["CLOUD_LO"], 'S': IDX["CLOUD_SHADE"], 'o': IDX["INK"]}

def parse(rows, charmap):
    return [[charmap[c] for c in row] for row in rows]


def make_cloud(lobes, baseline, w, h):
    """A cumulus built as a union of lobes over a flat base, then shaded by how
    far down each pixel sits in its own column. Single domes read as rocks; the
    overlapping lobes are what make it read as a cloud."""
    mask = set()
    for cx, cy, r in lobes:
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    mask.add((x, y))
    cols = {}
    for (x, y) in mask:
        cols.setdefault(x, []).append(y)
    for x, ys in cols.items():                      # drop every column to the flat base
        for y in range(min(ys), baseline + 1):
            mask.add((x, y))
            cols[x].append(y)

    art = [[TRANSPARENT] * w for _ in range(h)]
    for (x, y) in mask:
        if not (0 <= x < w and 0 <= y < h):
            continue
        top = min(cols[x])
        span = max(1, baseline - top)
        t = (y - top) / span
        art[y][x] = (IDX["CLOUD_HI"] if t < 0.34 else
                     IDX["CLOUD_MID"] if t < 0.66 else
                     IDX["CLOUD_LO"] if t < 0.88 else IDX["CLOUD_SHADE"])
    for (x, y) in mask:                             # outline where a neighbour is outside
        if not (0 <= x < w and 0 <= y < h):
            continue
        if any((x + dx, y + dy) not in mask for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            art[y][x] = IDX["INK"]
    return art


#            (cx, cy, r)  overlapping lobes, left small -> centre tall -> right wide
CLOUD_BIG = make_cloud([(8, 11, 5), (16, 8, 7), (24, 11, 5)], baseline=16, w=31, h=18)
CLOUD_SMALL = make_cloud([(4, 7, 3), (9, 5, 4), (14, 7, 3)], baseline=10, w=19, h=12)


# ── generated parts ─────────────────────────────────────────────────────────
def blank(w=W, h=H):
    return [[TRANSPARENT] * w for _ in range(h)]


def put(dst, src, ox, oy, skip_transparent=True):
    for y, row in enumerate(src):
        for x, v in enumerate(row):
            if skip_transparent and v == TRANSPARENT:
                continue
            px, py = ox + x, oy + y
            if 0 <= px < len(dst[0]) and 0 <= py < len(dst):
                dst[py][px] = v


def disc(dst, cx, cy, r, core, mid, deep, outline=None):
    """A pixel-art sun: hard circle, light from the upper left, optional outline."""
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            if not (0 <= x < len(dst[0]) and 0 <= y < len(dst)):
                continue
            dx, dy = x - cx + 0.5, y - cy + 0.5
            d2 = dx * dx + dy * dy
            if d2 > (r + 0.5) ** 2:
                continue
            if outline is not None and d2 > (r - 0.5) ** 2:
                dst[y][x] = outline
                continue
            # shading band from the upper-left highlight to the lower-right
            t = (dx + dy) / (2.0 * r)
            dst[y][x] = core if t < -0.18 else mid if t < 0.34 else deep


def rays(dst, cx, cy, r0, r1, colour, outline=None):
    """Eight straight rays on the axes and diagonals, drawn as whole pixels."""
    dirs = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]
    for dx, dy in dirs:
        diag = dx and dy
        for t in range(r0, r1 + 1):
            # diagonals step on both axes, so shorten them to look even
            if diag and t > r0 + (r1 - r0) * 2 // 3:
                break
            x, y = cx + dx * t, cy + dy * t
            thick = 2 if not diag else 1
            for k in range(thick):
                xx = x + (k if dy == 0 else 0)
                yy = y + (k if dx == 0 else 0)
                if 0 <= xx < len(dst[0]) and 0 <= yy < len(dst):
                    dst[yy][xx] = colour
    if outline is None:
        return


def bolt(dst, ox, oy, hi, lo, ink):
    art = [
        "....oooooo..",
        "...oHHHHHo..",
        "..oHHHHHHo..",
        "..oHHHHHo...",
        ".oHHHHHo....",
        ".oHHHHHooooo",
        "oHHHHHHHHHHo",
        "oHHHHHLLLLLo",
        "oHHHLLLLLLo.",
        ".oLLLLLLLo..",
        ".oLLLLLLo...",
        "..oLLLLo....",
        "...oLLLo....",
        "....oLLo....",
        ".....oLo....",
        ".....ooo....",
    ]
    cm = {'.': TRANSPARENT, 'H': hi, 'L': lo, 'o': ink}
    put(dst, parse(art, cm), ox, oy)


def drops(dst, xs, y0, length, hi, lo, slant=1):
    for i, x in enumerate(xs):
        for t in range(length):
            px, py = x + t * slant // 3, y0 + t
            if 0 <= px < len(dst[0]) and 0 <= py < len(dst):
                dst[py][px] = hi if t < length - 2 else lo


def flakes(dst, pts, colour, ink):
    for (cx, cy) in pts:
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
            x, y = cx + dx, cy + dy
            if 0 <= x < len(dst[0]) and 0 <= y < len(dst):
                dst[y][x] = colour
        for dx, dy in ((1, 1), (-1, -1), (1, -1), (-1, 1)):
            x, y = cx + dx, cy + dy
            if 0 <= x < len(dst[0]) and 0 <= y < len(dst):
                dst[y][x] = ink


# ── scene assembly ──────────────────────────────────────────────────────────
CLOUD_B = CLOUD_BIG
CLOUD_S = CLOUD_SMALL
SUN = (IDX["SUN_CORE"], IDX["SUN_MID"], IDX["SUN_DEEP"])
INK = IDX["INK"]


def scene_sun():
    s = blank()
    rays(s, 19, 14, 12, 18, IDX["SUN_MID"])
    disc(s, 19, 14, 10, *SUN, outline=INK)
    return s


def scene_partial():
    s = blank()
    rays(s, 13, 10, 9, 13, IDX["SUN_MID"])
    disc(s, 13, 10, 7, *SUN, outline=INK)
    put(s, CLOUD_B, 8, 11)
    return s


def scene_cloud():
    s = blank()
    put(s, CLOUD_S, 1, 2)
    put(s, CLOUD_B, 8, 10)
    return s


def scene_drizzle():
    s = blank()
    put(s, CLOUD_B, 5, 3)
    drops(s, [11, 17, 23, 29], 20, 4, IDX["RAIN_HI"], IDX["RAIN_LO"])
    return s


def scene_rain():
    s = blank()
    put(s, CLOUD_B, 5, 2)
    drops(s, [8, 14, 20, 26, 32], 19, 8, IDX["RAIN_HI"], IDX["RAIN_LO"], slant=2)
    return s


def scene_snow():
    s = blank()
    put(s, CLOUD_B, 5, 2)
    flakes(s, [(11, 22), (19, 26), (28, 21), (15, 27), (24, 27)], IDX["SNOW"], IDX["CLOUD_LO"])
    return s


def scene_thunder():
    s = blank()
    put(s, CLOUD_B, 5, 1)
    bolt(s, 15, 16, IDX["BOLT_HI"], IDX["BOLT_LO"], INK)
    drops(s, [6, 32], 18, 7, IDX["RAIN_HI"], IDX["RAIN_LO"], slant=2)
    return s


def scene_fog():
    s = blank()
    put(s, CLOUD_B, 5, 1)
    bands = [(4, 28, IDX["FOG"]), (9, 32, IDX["MUTED"]), (2, 22, IDX["FOG"]),
             (11, 30, IDX["MUTED"])]
    for i, (x0, x1, col) in enumerate(bands):
        y = 19 + i * 3
        for x in range(x0, x1):
            if 0 <= x < W and 0 <= y < H:
                s[y][x] = col
    return s


def scene_unknown():
    s = blank()
    art = [
        "...oooooo...",
        "..oHHHHHHo..",
        ".oHHo..oHHo.",
        ".oHo....oHo.",
        ".ooo....oHo.",
        "........oHo.",
        ".......oHHo.",
        "......oHHo..",
        "......oHo...",
        "......ooo...",
        "............",
        "......ooo...",
        ".....oHHHo..",
        "......ooo...",
    ]
    cm = {'.': TRANSPARENT, 'H': IDX["MUTED"], 'o': INK}
    put(s, parse(art, cm), 14, 8)
    return s


# order must match the enum the page indexes with
SCENES = [
    ("SUN",           scene_sun,     (90, 160, 235), (175, 215, 250)),
    ("PARTIAL_CLOUD", scene_partial, (104, 158, 215), (182, 210, 238)),
    ("CLOUD",         scene_cloud,   (118, 130, 148), (176, 186, 200)),
    ("DRIZZLE",       scene_drizzle, (86, 110, 150), (138, 162, 196)),
    ("RAIN",          scene_rain,    (60, 82, 124),  (104, 132, 176)),
    ("SNOW",          scene_snow,    (122, 142, 176), (196, 210, 230)),
    ("THUNDER",       scene_thunder, (48, 56, 86),   (96, 104, 144)),
    ("FOG",           scene_fog,     (138, 144, 154), (196, 200, 208)),
    ("UNKNOWN",       scene_unknown, (96, 104, 120),  (150, 158, 174)),
]


WMO_TO_SCENE = [
    (0, "SUN"), (1, "SUN"), (2, "PARTIAL_CLOUD"), (3, "CLOUD"),
    (45, "FOG"), (48, "FOG"),
    (51, "DRIZZLE"), (53, "DRIZZLE"), (55, "DRIZZLE"), (56, "DRIZZLE"), (57, "DRIZZLE"),
    (61, "RAIN"), (63, "RAIN"), (65, "RAIN"), (66, "RAIN"), (67, "RAIN"),
    (71, "SNOW"), (73, "SNOW"), (75, "SNOW"), (77, "SNOW"),
    (80, "RAIN"), (81, "RAIN"), (82, "RAIN"),
    (85, "SNOW"), (86, "SNOW"),
    (95, "THUNDER"), (96, "THUNDER"), (99, "THUNDER"),
]


def build():
    return [(name, fn(), top, bot) for name, fn, top, bot in SCENES]


# ── emit ────────────────────────────────────────────────────────────────────
def pack4(sprite):
    """Two pixels per byte, high nibble first."""
    out = bytearray()
    for row in sprite:
        for x in range(0, W, 2):
            out.append((row[x] << 4) | row[x + 1])
    return bytes(out)


def header():
    scenes = build()
    date = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d')
    L = []
    L.append("/* SPDX-License-Identifier: GPL-3.0-only */")
    L.append("/* AUTO-GENERATED by tools/codegen/gen_weather_art.py -- DO NOT EDIT */")
    L.append(f"/* Generated {date} */")
    L.append("")
    L.append("#ifndef WEATHER_ART_H")
    L.append("#define WEATHER_ART_H")
    L.append("")
    L.append("#include <stdint.h>")
    L.append("")
    L.append(f"#define WTHR_ART_W {W}")
    L.append(f"#define WTHR_ART_H {H}")
    L.append(f"#define WTHR_ART_STRIDE {W // 2}")
    L.append("")
    L.append("/* palette: index 0 is transparent */")
    L.append(f"static const uint16_t wthr_pal[{len(PALETTE)}] = {{")
    for i, (name, rgb) in enumerate(PALETTE):
        L.append(f"    0x{rgb_to_565(*rgb):04X},  /* {i:2d} {name} */")
    L.append("};")
    L.append("")
    L.append("enum {")
    for i, (name, *_ ) in enumerate(scenes):
        L.append(f"    WTHR_ART_{name} = {i},")
    L.append(f"    WTHR_ART_COUNT = {len(scenes)}")
    L.append("};")
    L.append("")
    L.append("/* sky gradient endpoints, dithered top to bottom by the page */")
    L.append(f"static const uint16_t wthr_sky[{len(scenes)}][2] = {{")
    for name, _sprite, top, bot in scenes:
        L.append(f"    {{ 0x{rgb_to_565(*top):04X}, 0x{rgb_to_565(*bot):04X} }},  /* {name} */")
    L.append("};")
    L.append("")
    for name, sprite, _t, _b in scenes:
        data = pack4(sprite)
        L.append(f"static const uint8_t wthr_art_{name.lower()}[{len(data)}] = {{")
        for i in range(0, len(data), 16):
            L.append("    " + ",".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
        L.append("};")
    L.append("")
    L.append(f"static const uint8_t *const wthr_art[{len(scenes)}] = {{")
    for name, *_ in scenes:
        L.append(f"    wthr_art_{name.lower()},")
    L.append("};")
    L.append("")
    for name, i in IDX.items():
        L.append(f"#define WTHR_C_{name} {i}")
    L.append("")
    L.append("static int weather_art_for_code(uint8_t wmo_code)")
    L.append("{")
    L.append("    switch (wmo_code) {")
    for code, scene in WMO_TO_SCENE:
        L.append(f"    case {code:3d}: return WTHR_ART_{scene};")
    L.append("    default:  return WTHR_ART_UNKNOWN;")
    L.append("    }")
    L.append("}")
    L.append("")
    L.append("#endif /* WEATHER_ART_H */")
    return "\n".join(L) + "\n"


def preview(path):
    from PIL import Image
    scenes = build()
    scale = 4
    pad = 8
    cols = 3
    rows = (len(scenes) + cols - 1) // cols
    cw, ch = W * scale + pad * 2, H * scale + pad * 2 + 14
    im = Image.new('RGB', (cols * cw, rows * ch), (24, 24, 28))
    px = im.load()
    for n, (name, sprite, top, bot) in enumerate(scenes):
        ox = (n % cols) * cw + pad
        oy = (n // cols) * ch + pad
        for y in range(H):
            # the sky the sprite will actually sit on
            t = y / max(1, H - 1)
            sky = tuple(int(a + (b - a) * t) for a, b in zip(top, bot))
            for x in range(W):
                v = sprite[y][x]
                c = sky if v == TRANSPARENT else PALETTE[v][1]
                for sy in range(scale):
                    for sx in range(scale):
                        px[ox + x * scale + sx, oy + y * scale + sy] = c
    im.save(path)
    return path


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "header"
    if cmd == "header":
        sys.stdout.write(header())
    elif cmd == "preview":
        out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/weather-art.png"
        print(preview(out))
    else:
        sys.exit(f"unknown command: {cmd}")
