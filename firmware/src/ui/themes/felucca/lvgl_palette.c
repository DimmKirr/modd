// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Felucca palettes for the LVGL theme (CONFIG_UI_THEME_FELUCCA).
 *
 * The same 10 palettes as the upstream Felucca firmware, converted to the
 * modd ui_pal_t format. surf is derived at 12% from bg toward theme (the
 * upstream SURF_PCT). Felucca uses one theme colour per palette; all four
 * track slots and knob colours carry it. GREY is the default.
 *
 * Origin: firmware/upstream/felucca/build/gen/ui_palettes.h (Felucca v2.2) */

typedef struct { const char *name; uint16_t bg, surf, text, accent, trk[4], knob[4][4]; } ui_pal_t;

static const ui_pal_t UI_PALETTES[] = {
    {"GREY",   0x0841, 0x18c3, 0xef5d, 0x4c7f, {0xb596, 0xb596, 0xb596, 0xb596}, {{0xb596, 0xb596, 0xb596, 0xb596}, {0xb596, 0xb596, 0xb596, 0xb596}, {0xb596, 0xb596, 0xb596, 0xb596}, {0xb596, 0xb596, 0xb596, 0xb596}}},
    {"GREEN",  0x0081, 0x0922, 0xe7bc, 0xfeab, {0x56af, 0x56af, 0x56af, 0x56af}, {{0x56af, 0x56af, 0x56af, 0x56af}, {0x56af, 0x56af, 0x56af, 0x56af}, {0x56af, 0x56af, 0x56af, 0x56af}, {0x56af, 0x56af, 0x56af, 0x56af}}},
    {"AMBER",  0x1061, 0x28e1, 0xf77b, 0x66bc, {0xfd45, 0xfd45, 0xfd45, 0xfd45}, {{0xfd45, 0xfd45, 0xfd45, 0xfd45}, {0xfd45, 0xfd45, 0xfd45, 0xfd45}, {0xfd45, 0xfd45, 0xfd45, 0xfd45}, {0xfd45, 0xfd45, 0xfd45, 0xfd45}}},
    {"ICE",    0x0883, 0x0905, 0xe79f, 0xfc6c, {0x55dd, 0x55dd, 0x55dd, 0x55dd}, {{0x55dd, 0x55dd, 0x55dd, 0x55dd}, {0x55dd, 0x55dd, 0x55dd, 0x55dd}, {0x55dd, 0x55dd, 0x55dd, 0x55dd}, {0x55dd, 0x55dd, 0x55dd, 0x55dd}}},
    {"VIOLET", 0x1063, 0x20c6, 0xf75f, 0x6f35, {0xb45e, 0xb45e, 0xb45e, 0xb45e}, {{0xb45e, 0xb45e, 0xb45e, 0xb45e}, {0xb45e, 0xb45e, 0xb45e, 0xb45e}, {0xb45e, 0xb45e, 0xb45e, 0xb45e}, {0xb45e, 0xb45e, 0xb45e, 0xb45e}}},
    {"ROSE",   0x1842, 0x3084, 0xff5e, 0xfead, {0xf396, 0xf396, 0xf396, 0xf396}, {{0xf396, 0xf396, 0xf396, 0xf396}, {0xf396, 0xf396, 0xf396, 0xf396}, {0xf396, 0xf396, 0xf396, 0xf396}, {0xf396, 0xf396, 0xf396, 0xf396}}},
    {"PAPER",  0xf77c, 0xd6d9, 0x20e3, 0xa9c1, {0x0aa8, 0x0aa8, 0x0aa8, 0x0aa8}, {{0x0aa8, 0x0aa8, 0x0aa8, 0x0aa8}, {0x0aa8, 0x0aa8, 0x0aa8, 0x0aa8}, {0x0aa8, 0x0aa8, 0x0aa8, 0x0aa8}, {0x0aa8, 0x0aa8, 0x0aa8, 0x0aa8}}},
    {"HI-CON", 0x0000, 0x18c0, 0xffdf, 0x073f, {0xff40, 0xff40, 0xff40, 0xff40}, {{0xff40, 0xff40, 0xff40, 0xff40}, {0xff40, 0xff40, 0xff40, 0xff40}, {0xff40, 0xff40, 0xff40, 0xff40}, {0xff40, 0xff40, 0xff40, 0xff40}}},
    {"NIGHT",  0x0000, 0x00c1, 0x9735, 0xffdf, {0x3eec, 0x3eec, 0x3eec, 0x3eec}, {{0x3eec, 0x3eec, 0x3eec, 0x3eec}, {0x3eec, 0x3eec, 0x3eec, 0x3eec}, {0x3eec, 0x3eec, 0x3eec, 0x3eec}, {0x3eec, 0x3eec, 0x3eec, 0x3eec}}},
    {"MONO",   0x0000, 0x18e3, 0xffff, 0x4c7f, {0xffff, 0xffff, 0xffff, 0xffff}, {{0xffff, 0xffff, 0xffff, 0xffff}, {0xffff, 0xffff, 0xffff, 0xffff}, {0xffff, 0xffff, 0xffff, 0xffff}, {0xffff, 0xffff, 0xffff, 0xffff}}},
};
#define UI_NPALETTES 10u
#define UI_DEFAULT_INDEX 0u
#define UI_PAL_TAG 64u
#define UI_MID_PCT 70
#define UI_DIM_PCT 42
#define UI_LINE_PCT 18
#define UI_SEL_PCT 80
#define UI_TINT_PCT 12
#define UI_KEY_PCT 78
#define UI_RAISE_PCT 12
static const uint8_t UI_LINE_PCTS[] = {10, 18, 18, 18, 18, 18, 18, 18, 18, 18};
#define UI_REC_DARK 0xfa49u
#define UI_REC_LIGHT 0xb8c5u
#define UI_QR_LIGHT 0xffdfu
#define UI_QR_DARK 0x0000u
#define UI_CRASH_BG 0xa000u
#define UI_CRASH_INK 0xffdfu

static struct {
    uint8_t    idx;
    uint8_t    trk;
    uint8_t    light;
    lv_color_t bg, surf, text, accent, theme, mid, dim, line, rec;
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
    lvt_pal.surf = lvt_c565(p->surf);
    lvt_pal.text = lvt_c565(p->text);
    lvt_pal.accent = lvt_c565(p->accent);
    for (k = 0; k < 4u; k++)
        lvt_pal.trk_c[k] = lvt_c565(p->trk[k]);
    lvt_pal.mid = lvt_mix(lvt_pal.bg, lvt_pal.text, UI_MID_PCT);
    lvt_pal.dim = lvt_mix(lvt_pal.bg, lvt_pal.text, UI_DIM_PCT);
    lvt_pal.line = lvt_mix(lvt_pal.bg, lvt_pal.text, UI_LINE_PCTS[lvt_pal.idx]);
    lvt_pal.light = lv_color_luminance(lvt_pal.bg) > 128u;
    lvt_pal.rec = lvt_c565(lvt_pal.light ? UI_REC_LIGHT : UI_REC_DARK);
    lvt_pal.theme = lvt_pal.trk_c[lvt_pal.trk & 3u];
}

static uint32_t lvt_pal_from_stored(uint32_t v)
{
    return v >= UI_PAL_TAG && v - UI_PAL_TAG < UI_NPALETTES ? v - UI_PAL_TAG : UI_DEFAULT_INDEX;
}

static uint32_t lvt_pal_to_stored(uint32_t i) { return UI_PAL_TAG + i % UI_NPALETTES; }
