// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd WEATHER: pixel-art weather page under VIEW_CONN (FX hold).
 * Uses the shared weather_art data from the Felucca theme dir and the font5x7
 * bitmap face. Sky gradient and text colours follow the active palette (T_*
 * tokens); sprite art keeps its own fixed palette. */

#include "ui/themes/felucca/weather_colors.h"
#include "ui/themes/felucca/weather_art.h"

/* wx_data_t, wx_read(): ui/page_data.c (shared with the REST API) */

static uint32_t mwx_frame;

/* ── pixel-art helpers (same primitives as Felucca, modd layout) ──── */

static uint16_t mwx_mix(uint16_t a, uint16_t b, uint32_t n, uint32_t d)
{
    uint32_t r = (((a >> 11) & 31u) * (d - n) + ((b >> 11) & 31u) * n) / d;
    uint32_t g = (((a >> 5) & 63u) * (d - n) + ((b >> 5) & 63u) * n) / d;
    uint32_t bl = ((a & 31u) * (d - n) + (b & 31u) * n) / d;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

#define MWX_BANDS 7u
static void mwx_sky(uint32_t w, uint32_t h, uint16_t top, uint16_t bot)
{
    static const uint8_t BAYER[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    uint32_t x, y;
    if (h < 2u) return;
    for (y = 0; y < h; y++) {
        uint32_t f = y * (MWX_BANDS - 1u) * 16u / (h - 1u);
        uint32_t band = f >> 4, frac = f & 15u;
        uint16_t c0 = mwx_mix(top, bot, band, MWX_BANDS - 1u);
        uint16_t c1 = mwx_mix(top, bot, band + 1u < MWX_BANDS ? band + 1u : band, MWX_BANDS - 1u);
        for (x = 0; x < w; x++)
            cv_pset((int32_t)x, (int32_t)y, frac > BAYER[y & 3u][x & 3u] ? c1 : c0);
    }
}

static void mwx_art(int32_t ox, int32_t oy, uint32_t idx, uint32_t s)
{
    const uint8_t *d = wthr_art[idx];
    uint32_t x, y, i, j;
    for (y = 0; y < (uint32_t)WTHR_ART_H; y++) {
        for (x = 0; x < (uint32_t)WTHR_ART_W; x++) {
            uint8_t byte = d[y * (uint32_t)WTHR_ART_STRIDE + (x >> 1)];
            uint8_t v = (x & 1u) ? (uint8_t)(byte & 0x0Fu) : (uint8_t)(byte >> 4);
            if (!v) continue;
            for (j = 0; j < s; j++)
                for (i = 0; i < s; i++)
                    cv_pset(ox + (int32_t)(x * s + i), oy + (int32_t)(y * s + j), wthr_pal[v]);
        }
    }
}

static void mwx_char(int32_t x, int32_t y, char ch, uint32_t s, uint16_t c)
{
    const uint8_t *g;
    uint32_t col, row, i, j;
    if (ch < ' ' || ch > '~') ch = ' ';
    g = &font5x7[(ch - ' ') * 5];
    for (col = 0; col < 5u; col++) {
        uint8_t bits = g[col];
        for (row = 0; row < 7u; row++) {
            if (!(bits & (1u << row))) continue;
            for (j = 0; j < s; j++)
                for (i = 0; i < s; i++)
                    cv_pset(x + (int32_t)(col * s + i), y + (int32_t)(row * s + j), c);
        }
    }
}

static int32_t mwx_text_w(const char *t, uint32_t s)
{
    uint32_t n = 0;
    while (t[n]) n++;
    return n ? (int32_t)(n * 6u * s - s) : 0;
}

static void mwx_text(int32_t x, int32_t y, const char *t, uint32_t s,
                     uint16_t c, uint16_t sh)
{
    const char *p;
    int32_t cx;
    if (sh) {
        for (p = t, cx = x + (int32_t)s; *p; p++, cx += (int32_t)(6u * s))
            mwx_char(cx, y + (int32_t)s, *p, s, sh);
    }
    for (p = t, cx = x; *p; p++, cx += (int32_t)(6u * s))
        mwx_char(cx, y, *p, s, c);
}

static uint32_t mwx_fit(const char *t, uint32_t s, int32_t avail)
{
    while (s > 1u && mwx_text_w(t, s) > avail) s--;
    return s;
}

/* ── formatting ──────────────────────────────────────────────────── */

static void mwx_fmt_temp(char *buf, int16_t tc)
{
    int neg = tc < 0;
    uint32_t i = 0;
    uint16_t v;
    if (neg) { buf[i++] = '-'; tc = -tc; }
    v = (uint16_t)tc;
    if (v >= 1000) buf[i++] = '0' + (char)(v / 1000);
    if (v >= 100)  buf[i++] = '0' + (char)((v / 100) % 10);
    buf[i++] = '0' + (char)((v / 10) % 10);
    buf[i++] = '.';
    buf[i++] = '0' + (char)(v % 10);
    buf[i] = 0;
}

static void mwx_fmt_hum(char *buf, uint8_t h)
{
    uint32_t i = 0;
    if (h >= 100) buf[i++] = '1';
    if (h >= 10)  buf[i++] = '0' + (char)((h / 10) % 10);
    buf[i++] = '0' + (char)(h % 10);
    buf[i++] = '%';
    buf[i] = 0;
}

static void mwx_fmt_wind(char *buf, uint16_t w)
{
    uint32_t i = 0;
    if (w >= 1000) buf[i++] = '0' + (char)(w / 1000);
    if (w >= 100)  buf[i++] = '0' + (char)((w / 100) % 10);
    buf[i++] = '0' + (char)((w / 10) % 10);
    buf[i++] = '.';
    buf[i++] = '0' + (char)(w % 10);
    buf[i] = 0;
}

/* ── draw states ────────────────────────────────────────────────── */

static void mwx_draw_loading(void)
{
    mt_panel_begin();
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 - 10, TY_TX), TY_TX,
              "Fetching...", T_DIM, T_BG);
    mt_panel_end();
}

static void mwx_draw_error(void)
{
    mt_panel_begin();
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 - 16, TY_TX), TY_TX,
              "No Data", T_DIM, T_BG);
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 + 8, TY_LB), TY_LB,
              "check WiFi", T_DIM, T_BG);
    mt_panel_end();
}

static void mwx_draw_weather(const struct weather_data *w)
{
    int art = weather_art_for_code(w->wmo_code);
    const char *cond = weather_condition_name(w->wmo_code);
    uint16_t fg = T_TEXT;
    uint16_t sh = ux_mix(T_BG, T_THEME, 18);
    uint16_t sky_top = ux_mix(T_BG, T_THEME, 40);
    uint16_t sky_bot = ux_mix(T_SURF, T_THEME, 20);
    char temp[12], hum[8], wind[12];
    uint32_t s;

    mwx_fmt_temp(temp, w->temp_c);
    mwx_fmt_hum(hum, w->humidity);
    mwx_fmt_wind(wind, w->wind_kmh);

    cv_begin(UI_W, UI_PANEL_H, sky_top);
    mwx_sky(UI_W, UI_PANEL_H, sky_top, sky_bot);

    /* sprite at 3x (was 2x), centered vertically in the upper half */
    mwx_art(6, 6, (uint32_t)art, 3);

    /* big temperature to the right of the sprite */
    mwx_text(96, 10, temp, 4, fg, sh);
#ifdef CONFIG_WEATHER_UNIT_F
    mwx_text(96 + mwx_text_w(temp, 4) + 6, 10, "F", 2, fg, sh);
#else
    mwx_text(96 + mwx_text_w(temp, 4) + 6, 10, "C", 2, fg, sh);
#endif

    /* condition name */
    s = mwx_fit(cond, 2, UI_W - 96 - 6);
    mwx_text(96, 46, cond, s, fg, sh);

    /* single info line: "City, CC · 82% · 7.2 km/h" */
    {
        char info[48];
        uint32_t ii = 0;
        const char *c;
        if (w->city[0]) {
            for (c = w->city; *c && ii < sizeof(info) - 20; c++)
                info[ii++] = *c;
            if (w->country[0]) {
                info[ii++] = ','; info[ii++] = ' ';
                for (c = w->country; *c && ii < sizeof(info) - 16; c++)
                    info[ii++] = *c;
            }
            info[ii++] = ' ';
        }
        for (c = hum; *c; c++) info[ii++] = *c;
        info[ii++] = ' ';
        for (c = wind; *c; c++) info[ii++] = *c;
#ifdef CONFIG_WEATHER_UNIT_F
        for (c = " MPH"; *c && ii < sizeof(info) - 1; c++) info[ii++] = *c;
#else
        for (c = " KM/H"; *c && ii < sizeof(info) - 1; c++) info[ii++] = *c;
#endif
        info[ii] = 0;
        mwx_text(10, (int32_t)UI_PANEL_H - 22, info,
                 mwx_fit(info, 1, UI_W - 20), T_MID, sh);
    }

    cv_blit(0, UI_Y_PANEL);
}

/* ── page vtable ─────────────────────────────────────────────────── */

static void mwx_draw(void)
{
    wx_data_t d;
    wx_read(&d);

    if (!d.w->valid) {
        ui_card(0, "TEMP", "---", "", T_DIM, -1, 0);
        ui_card(1, "HUM", "---", "", T_DIM, -1, 0);
        ui_card(2, "WIND", "---", "", T_DIM, -1, 0);
        ui_card(3, "WMO", "---", "", T_DIM, -1, 0);
    }

    if (d.fetching && !d.w->valid)
        mwx_draw_loading();
    else if (d.w->valid)
        mwx_draw_weather(d.w);
    else
        mwx_draw_error();

    mwx_frame++;

    ui_footer("", d.w->valid ? d.w->city : "---", "");
}

static void mwx_leave(void)
{
    net_release();
}

static void mwx_enter(void)
{
    mwx_frame = 0;
    net_need();                         /* the page needs the network while it is open */
    if (!weather_get()->valid && !weather_fetching())
        weather_fetch_start();
}

static int mwx_button(uint32_t id, uint32_t down)
{
    if (!down && id == BTN_PLAY) {
        weather_fetch_start();
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static void mwx_status(void)
{
    if (weather_fetching())
        nav_dirty = 1;
}

static const modd_page_t PAGE_WEATHER = {
    .name   = "WX",
    .title  = "Weather",
    .view   = VIEW_CONN,
    .flags  = PAGEF_SYSTEM,
    .enter  = mwx_enter,
    .leave  = mwx_leave,
    .draw   = mwx_draw,
    .status = mwx_status,
    .button = mwx_button,
    .help_text =
        "PLAY    Refresh",
};
