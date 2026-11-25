// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Felucca palettes for the LVGL skin (CONFIG_UI_SKIN_FELUCCA).
 *
 * Macros (count, percentages, stored-id tag, migrate table) come from the
 * upstream generated header via #include. The colour array uses modd's
 * ui_pal_t layout (bg, surf, text, accent, trkc[4], knob[4][4]) mapped from
 * upstream's 5-field entries: bg=c1, surf=12% from bg toward c2, text=c3,
 * accent=secondary, trk/knob all=c2.
 *
 * Origin: firmware/upstream/felucca/build/gen/ui_palettes.h (Felucca v1.8.2, d0260074) */

#include "ui/ui_palette.h"

/* pull in ONLY the macros from upstream; redirect the array into a throwaway name
 * so the upstream's 6-field entries don't collide with modd's ui_pal_t */
typedef struct { const char *n_; uint16_t a_, b_, c_, d_, e_; } fe_pal_void_t;
#define ui_pal_t fe_pal_void_t
#define UI_PALETTES fe_pal_void_
#include "felucca_ui_palettes.h"
#undef UI_PALETTES
#undef ui_pal_t

/* modd-format palette array: upstream c1→bg, c2→trk/knob, c3→text, secondary→accent.
 * surf is set at runtime (12% blend) so the surf field here is bg (overwritten). */
#define C4(x) {(x),(x),(x),(x)}
#define K4(x) {C4(x),C4(x),C4(x),C4(x)}
#define P(nm,c1,c2,c3,sec) {(nm),(c1),(c1),(c3),(sec),C4(c2),K4(c2)}
static const ui_pal_t UI_PALETTES[] = {
    P("GREY",   0x0841, 0xb596, 0xef5d, 0x4c7f),
    P("GREEN",  0x0081, 0x56af, 0xe7bc, 0xfeab),
    P("AMBER",  0x1061, 0xfd45, 0xf77b, 0x66bc),
    P("ICE",    0x0883, 0x55dd, 0xe79f, 0xfc6c),
    P("VIOLET", 0x1063, 0xb45e, 0xf75f, 0x6f35),
    P("ROSE",   0x1842, 0xf396, 0xff5e, 0xfead),
    P("PAPER",  0xf77c, 0x0aa8, 0x20e3, 0xa9c1),
    P("HI-CON", 0x0000, 0xff40, 0xffdf, 0x073f),
    P("NIGHT",  0x0000, 0x3eec, 0x9735, 0xffdf),
    P("MONO",   0x0000, 0xffff, 0xffff, 0x4c7f),
};
#undef P
#undef K4
#undef C4
_Static_assert(sizeof UI_PALETTES / sizeof UI_PALETTES[0] == UI_NPALETTES, "palette count mismatch");

#define UI_DEFAULT_INDEX UI_GREY_INDEX

static const uint8_t UI_LINE_PCTS[] = {10, 18, 18, 18, 18, 18, 18, 18, 18, 18};

static struct {
    uint8_t    idx;
    uint8_t    trk;
    uint8_t    light;
    lv_color_t bg, surf, text, accent, primary, mid, dim, line, rec;
    lv_color_t trk_c[4];
} lvt_pal;

static lv_color_t lvt_c565(uint32_t c)
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return lv_color_make((uint8_t)(r * 255u / 31u), (uint8_t)(g * 255u / 63u), (uint8_t)(b * 255u / 31u));
}

static lv_color_t lvt_mix(lv_color_t a, lv_color_t b, uint32_t pct)
{
    return lv_color_mix(b, a, (uint8_t)(pct * 255u / 100u));
}

static void lvt_pal_set(uint32_t i)
{
    const ui_pal_t *p = &UI_PALETTES[i < UI_NPALETTES ? i : UI_DEFAULT_INDEX];
    uint32_t k;
    lvt_pal.idx = (uint8_t)(p - UI_PALETTES);
    lvt_pal.bg = lvt_c565(p->bg);
    lvt_pal.text = lvt_c565(p->text);
    lvt_pal.accent = lvt_c565(p->accent);
    lvt_pal.surf = lvt_mix(lvt_pal.bg, lvt_c565(p->trkc[0]), UI_SURF_PCT);
    for (k = 0; k < 4u; k++)
        lvt_pal.trk_c[k] = lvt_c565(p->trkc[k]);
    lvt_pal.mid = lvt_mix(lvt_pal.bg, lvt_pal.text, UI_MID_PCT);
    lvt_pal.dim = lvt_mix(lvt_pal.bg, lvt_pal.text, UI_DIM_PCT);
    lvt_pal.line = lvt_mix(lvt_pal.bg, lvt_pal.text, UI_LINE_PCTS[lvt_pal.idx]);
    lvt_pal.light = lv_color_luminance(lvt_pal.bg) > 128u;
    lvt_pal.rec = lvt_c565(lvt_pal.light ? UI_REC_LIGHT : UI_REC_DARK);
    lvt_pal.primary = lvt_pal.trk_c[lvt_pal.trk & 3u];
}

static uint32_t lvt_pal_from_stored(uint32_t v)
{
    return v >= UI_PAL_TAG && v - UI_PAL_TAG < UI_NPALETTES ? v - UI_PAL_TAG : UI_DEFAULT_INDEX;
}

static uint32_t lvt_pal_to_stored(uint32_t i) { return UI_PAL_TAG + i % UI_NPALETTES; }
