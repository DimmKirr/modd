/* SPDX-License-Identifier: GPL-3.0-only */
/* WEATHER page: current weather fetched via WiFi (ip-api + open-meteo).
 * Layout: colored background area with glyph, temperature, condition,
 * city/country, humidity, and wind speed. Spinner during fetch. */

#include "ui/themes/felucca/weather_colors.h"
#include "ui/themes/felucca/weather_art.h"

static uint32_t fwx_frame;

/* ── pixel-art drawing ──────────────────────────────────────────────────────
 * This page is deliberately aliased: every pixel is a whole pixel. The shared
 * antialiased faces blend their edges toward the colour they are told sits
 * behind them, which fringes badly over a saturated sky, so text here uses the
 * 5x7 bitmap face at integer scale instead. */

static uint16_t wx_mix(uint16_t a, uint16_t b, uint32_t n, uint32_t d)
{
    uint32_t r = (((a >> 11) & 31u) * (d - n) + ((b >> 11) & 31u) * n) / d;
    uint32_t g = (((a >> 5) & 63u) * (d - n) + ((b >> 5) & 63u) * n) / d;
    uint32_t bl = ((a & 31u) * (d - n) + (b & 31u) * n) / d;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* Banded sky with an ordered dither on the band edges, so the gradient reads as
 * pixel art rather than as a smooth ramp the panel would band unevenly anyway. */
#define WX_BANDS 7u
static void wx_sky(uint32_t w, uint32_t h, uint16_t top, uint16_t bot)
{
    static const uint8_t BAYER[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    uint32_t x, y;
    if (h < 2u)
        return;
    for (y = 0; y < h; y++) {
        uint32_t f = y * (WX_BANDS - 1u) * 16u / (h - 1u);
        uint32_t band = f >> 4, frac = f & 15u;
        uint16_t c0 = wx_mix(top, bot, band, WX_BANDS - 1u);
        uint16_t c1 = wx_mix(top, bot, band + 1u < WX_BANDS ? band + 1u : band, WX_BANDS - 1u);
        for (x = 0; x < w; x++)
            cv_pset((int32_t)x, (int32_t)y, frac > BAYER[y & 3u][x & 3u] ? c1 : c0);
    }
}

/* 4bpp sprite, palette index 0 is transparent. Drawn at an integer scale so
 * the art's pixels stay the same size as the text's. */
static void wx_art(int32_t ox, int32_t oy, uint32_t idx, uint32_t s)
{
    const uint8_t *d = wthr_art[idx];
    uint32_t x, y, i, j;
    for (y = 0; y < (uint32_t)WTHR_ART_H; y++) {
        for (x = 0; x < (uint32_t)WTHR_ART_W; x++) {
            uint8_t byte = d[y * (uint32_t)WTHR_ART_STRIDE + (x >> 1)];
            uint8_t v = (x & 1u) ? (uint8_t)(byte & 0x0Fu) : (uint8_t)(byte >> 4);
            if (!v)
                continue;
            for (j = 0; j < s; j++)
                for (i = 0; i < s; i++)
                    cv_pset(ox + (int32_t)(x * s + i), oy + (int32_t)(y * s + j), wthr_pal[v]);
        }
    }
}

static void wx_char(int32_t x, int32_t y, char ch, uint32_t s, uint16_t c)
{
    const uint8_t *g;
    uint32_t col, row, i, j;
    if (ch < ' ' || ch > '~')
        ch = ' ';
    g = &font5x7[(ch - ' ') * 5];
    for (col = 0; col < 5u; col++) {
        uint8_t bits = g[col];
        for (row = 0; row < 7u; row++) {
            if (!(bits & (1u << row)))
                continue;
            for (j = 0; j < s; j++)
                for (i = 0; i < s; i++)
                    cv_pset(x + (int32_t)(col * s + i), y + (int32_t)(row * s + j), c);
        }
    }
}

static int32_t wx_text_w(const char *t, uint32_t s)
{
    uint32_t n = 0;
    while (t[n])
        n++;
    return n ? (int32_t)(n * 6u * s - s) : 0;
}

/* One logical pixel of drop shadow keeps the face legible over any sky. */
static void wx_text(int32_t x, int32_t y, const char *t, uint32_t s, uint16_t c, int shadow)
{
    const char *p;
    int32_t cx;
    if (shadow) {
        for (p = t, cx = x + (int32_t)s; *p; p++, cx += (int32_t)(6u * s))
            wx_char(cx, y + (int32_t)s, *p, s, wthr_pal[WTHR_C_INK]);
    }
    for (p = t, cx = x; *p; p++, cx += (int32_t)(6u * s))
        wx_char(cx, y, *p, s, c);
}

/* Largest integer scale whose run still fits, so long city or condition names
 * step down instead of running off the card. */
static uint32_t wx_fit(const char *t, uint32_t s, int32_t avail)
{
    while (s > 1u && wx_text_w(t, s) > avail)
        s--;
    return s;
}


/* format temp_c (x10 int) into string like "22.5" or "-3.0" */
static void fwx_fmt_temp(char *buf, int16_t tc)
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

/* format humidity as "65%" */
static void fwx_fmt_hum(char *buf, uint8_t h)
{
    uint32_t i = 0;
    if (h >= 100) buf[i++] = '1';
    if (h >= 10)  buf[i++] = '0' + (char)((h / 10) % 10);
    buf[i++] = '0' + (char)(h % 10);
    buf[i++] = '%';
    buf[i] = 0;
}

/* format wind x10 as "12.5" */
static void fwx_fmt_wind(char *buf, uint16_t w)
{
    uint32_t i = 0;
    if (w >= 1000) buf[i++] = '0' + (char)(w / 1000);
    if (w >= 100)  buf[i++] = '0' + (char)((w / 100) % 10);
    buf[i++] = '0' + (char)((w / 10) % 10);
    buf[i++] = '.';
    buf[i++] = '0' + (char)(w % 10);
    buf[i] = 0;
}

static void fwx_draw_loading(void)
{
    uint32_t h = Y_FOOT - Y_GRAPH;
    cv_begin(240, h, T_BG);

    fui_draw_spinner(120, (int32_t)(h / 2) - 10, 12,
                     fwx_frame, T_THEME, T_DIM);
    {
        const char *m = "FETCHING WEATHER...";
        wx_text((240 - wx_text_w(m, 2)) / 2, (int32_t)(h / 2) + 8, m, 2, T_MID, 0);
    }

    cv_blit(0, Y_GRAPH);
}

static void fwx_draw_error(void)
{
    uint32_t h = Y_FOOT - Y_GRAPH;
    cv_begin(240, h, T_BG);

    {
        const char *a = "NO WEATHER DATA", *b = "CHECK WIFI CONNECTION";
        wx_text((240 - wx_text_w(a, 2)) / 2, (int32_t)(h / 2) - 18, a, 2, T_DIM, 0);
        wx_text((240 - wx_text_w(b, 1)) / 2, (int32_t)(h / 2) + 6, b, 1, T_DIM, 0);
    }

    cv_blit(0, Y_GRAPH);
}

static void fwx_draw_weather(const struct weather_data *w)
{
    uint32_t h = Y_FOOT - Y_GRAPH;
    int art = weather_art_for_code(w->wmo_code);
    const char *cond = weather_condition_name(w->wmo_code);
    uint16_t fg = wthr_pal[WTHR_C_CLOUD_HI];
    char temp[12], hum[8], wind[12];
    uint32_t s;

    fwx_fmt_temp(temp, w->temp_c);
    fwx_fmt_hum(hum, w->humidity);
    fwx_fmt_wind(wind, w->wind_kmh);

    cv_begin(240, h, wthr_sky[art][0]);
    wx_sky(240, h, wthr_sky[art][0], wthr_sky[art][1]);

    wx_art(8, 12, (uint32_t)art, 2);

    /* temperature, the one piece of hero type */
    wx_text(96, 16, temp, 4, fg, 1);
    wx_text(96 + wx_text_w(temp, 4) + 8, 18, "C", 2, fg, 1);

    s = wx_fit(cond, 2, 240 - 96 - 6);
    wx_text(96, 52, cond, s, fg, 1);

    if (w->city[0]) {
        char loc[32];
        uint32_t li = 0;
        const char *c = w->city;
        while (*c && li < sizeof(loc) - 6)
            loc[li++] = *c++;
        if (w->country[0]) {
            loc[li++] = ',';
            loc[li++] = ' ';
            c = w->country;
            while (*c && li < sizeof(loc) - 1)
                loc[li++] = *c++;
        }
        loc[li] = 0;
        wx_text(10, (int32_t)h - 42, loc, wx_fit(loc, 2, 220), fg, 1);
    }

    {
        char info[24];
        uint32_t ii = 0;
        const char *c;
        for (c = hum; *c; c++)
            info[ii++] = *c;
        info[ii++] = ' ';
        info[ii++] = ' ';
        for (c = wind; *c; c++)
            info[ii++] = *c;
        for (c = " KM/H"; *c; c++)
            info[ii++] = *c;
        info[ii] = 0;
        wx_text(10, (int32_t)h - 22, info, wx_fit(info, 2, 220), fg, 1);
    }

    if (weather_fetching())
        fui_draw_spinner(226, 10, 6, fwx_frame, fg, wthr_sky[art][0]);

    cv_blit(0, Y_GRAPH);
}

static void fwx_draw(void)
{
    const struct weather_data *w = weather_get();

    ui_header();

    /* cards: temp, humidity, wind, condition */
    if (w->valid) {
        char temp[8], hum[8], wind[8];
        fwx_fmt_temp(temp, w->temp_c);
        fwx_fmt_hum(hum, w->humidity);
        fwx_fmt_wind(wind, w->wind_kmh);

        ui_card(0, "TEMP", temp, "C",
                      T_THEME, -1, ICON_GENERIC);
        ui_card(1, "HUMID", hum, "",
                      T_THEME, (int32_t)w->humidity * 10, ICON_GENERIC);
        ui_card(2, "WIND", wind, "km/h",
                      T_MID, -1, ICON_GENERIC);
        ui_card(3, "WMO", weather_condition_name(w->wmo_code), "",
                      T_MID, -1, ICON_GENERIC);
    } else {
        ui_card(0, "TEMP", "---", "", T_DIM, -1, ICON_GENERIC);
        ui_card(1, "HUMID", "---", "", T_DIM, -1, ICON_GENERIC);
        ui_card(2, "WIND", "---", "", T_DIM, -1, ICON_GENERIC);
        ui_card(3, "WMO", "---", "", T_DIM, -1, ICON_GENERIC);
    }

    /* main area */
    if (weather_fetching() && !w->valid)
        fwx_draw_loading();
    else if (w->valid)
        fwx_draw_weather(w);
    else
        fwx_draw_error();

    fwx_frame++;

    ui_footer("WIFI", w->valid ? w->city : "---", "WEATHER");
}

static void fwx_enter(void)
{
    fwx_frame = 0;
    if (!weather_get()->valid && !weather_fetching())
        weather_fetch_start();
}

static int fwx_button(uint32_t id, uint32_t down)
{
    if (!down && id == BTN_PLAY) {
        weather_fetch_start();
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static void fwx_status(void)
{
    if (weather_fetching())
        nav_dirty = 1;
}

static const modd_page_t FPAGE_WEATHER = {
    .name   = "WX",
    .title  = "WEATHER",
    .view   = VIEW_CONN,
    .flags  = PAGEF_SYSTEM,
    .enter  = fwx_enter,
    .draw   = fwx_draw,
    .status = fwx_status,
    .button = fwx_button,
};
