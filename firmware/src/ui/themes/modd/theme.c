// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
// Portions Copyright (C) 2026 Kerem Kilic (Ellic Studio): the screen layout (30 px header, four
// 57 x 48 cards, the panel, a 28 px sound bar at y 212) follows Melodee's ui.c
// Origin: keremimo/melodee firmware/src/ui.c @ v1.4.5 (398ada1e) (layout constants)
//         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/ui.c
/* modd theme: Melodee's type and tokens, modd's chrome. Exposes the ui_theme_t descriptor the
 * element layer dispatches through. Included after gfx.c, before shell.c. */

/* gfx.c declares ty_pixel() and leaves it to the shell: Melodee switches to its pixel faces in a
 * setting. modd always sets Rubik. */
static uint32_t ty_pixel(void) { return 0; }

#define UI_W       240
#define UI_H       240
#define UI_Y_HEAD  0
#define UI_HEAD_H  30
#define UI_Y_CARD  34
#define UI_CARD_W  57
#define UI_CARD_H  48
#define UI_CARD_X(c) (2 + 60 * (int32_t)(c))
#define UI_Y_PANEL 84                 /* the page's own area */
#define UI_PANEL_H 124
#define UI_Y_FOOT  212
#define UI_FOOT_H  28

/* shared layout tokens: all pages and widgets use these, never local defines */
#define UI_ROW_H     20               /* standard row for menus/lists */
#define UI_ROW_H_XS  13               /* compact row (debug log) */
#define UI_INSET      8               /* horizontal content inset */
#define UI_INSET_S    4               /* narrow inset (compact views) */
#define UI_GAP        4               /* standard gap between sections */
#define UI_GAP_L      8               /* large gap (between distinct groups) */
#define UI_RULE_H     1               /* separator line thickness */
#define UI_TEXT_Y(y, h) ty_y((y) + (h) / 2, TY_LB)
#define UI_TAB_H     16               /* tab bar height */

/* tab bar widget: content-sized tabs with selection highlight and rule */
typedef struct {
    const char *const *labels;
    uint32_t count;
    uint8_t  active;
    int (*visible)(uint8_t idx);
} ui_tabs_t;

static int32_t ui_tabs_draw(const ui_tabs_t *t)
{
    uint32_t i;
    int32_t x = UI_INSET;
    for (i = 0; i < t->count; i++) {
        if (t->visible && !t->visible((uint8_t)i))
            continue;
        int32_t tw = text_w(TY_LB, t->labels[i]);
        int32_t pad = UI_INSET_S * 2;
        uint8_t sel = (i == t->active);
        uint16_t fg = sel ? T_THEME : T_DIM;
        uint16_t bg = sel ? T_SURF : T_BG;
        if (sel)
            cv_rrect(x - UI_INSET_S, 0, tw + pad, UI_TAB_H, 3, T_SURF, T_BG);
        cv_text_fit(x, ty_y(UI_TAB_H / 2, TY_LB), TY_LB, t->labels[i], fg, bg, tw);
        x += tw + pad + UI_GAP;
    }
    cv_rect(0, UI_TAB_H, UI_W, UI_RULE_H, T_LINE);
    return UI_TAB_H + UI_RULE_H;
}

/* key-value row widget: label left, value right, optional selection highlight */
static void ui_row(int32_t y, const char *label, const char *value,
                   uint16_t val_color, uint8_t selected)
{
    uint16_t bg = selected ? T_SURF : T_BG;
    if (selected)
        cv_rrect(2, y, UI_W - 4, UI_ROW_H - 2, 4, T_SURF, T_BG);
    cv_text_fit(UI_INSET, UI_TEXT_Y(y, UI_ROW_H), TY_LB, label, T_DIM, bg, 120);
    cv_text_r(UI_W - UI_INSET, UI_TEXT_Y(y, UI_ROW_H), TY_LB, value, val_color, bg);
}

/* showcase widget: centered title, subtitle, optional color swatches with labels */
typedef struct {
    const char *title;
    const char *subtitle;
    const uint16_t *swatches;
    uint32_t n_swatches;
    const char *const *swatch_labels;
} ui_showcase_t;

static void ui_showcase_draw(int32_t y0, int32_t h, const ui_showcase_t *s)
{
    int32_t cy = y0 + UI_GAP;
    cv_rect(UI_INSET, y0, UI_W - UI_INSET * 2, UI_RULE_H, T_LINE);
    cy += UI_RULE_H + UI_GAP;

    if (s->title && s->title[0]) {
        int32_t th = h / 3;
        if (th < 24) th = 24;
        cv_text_c(UI_W / 2, ty_y(cy + th / 2, TY_VL), TY_VL, s->title, T_THEME, T_BG);
        cy += th;
    }
    if (s->subtitle && s->subtitle[0]) {
        cv_text_c(UI_W / 2, ty_y(cy + UI_GAP_L, TY_LB), TY_LB, s->subtitle, T_MID, T_BG);
        cy += UI_GAP_L * 2 + 10;
    }
    if (s->swatches && s->n_swatches) {
        uint32_t i;
        int32_t sw = 20, gap = 6;
        int32_t total = (int32_t)s->n_swatches * (sw + gap) - gap;
        int32_t sx = (UI_W - total) / 2;
        for (i = 0; i < s->n_swatches; i++) {
            int32_t x = sx + (int32_t)i * (sw + gap);
            cv_rrect(x, cy, sw, 10, 3, s->swatches[i], T_BG);
            if (s->swatch_labels && s->swatch_labels[i])
                cv_text_c(x + sw / 2, cy + 14, TY_LB, s->swatch_labels[i], T_DIM, T_BG);
        }
    }
}

#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
static void mt_wifi_icon(int32_t x, int32_t base, uint32_t bars, uint16_t on, uint16_t off)
{
    int32_t bw = 2, gap = 1, bx = x;
    uint32_t b;
    for (b = 0; b < 3u; b++) {
        int32_t h = 4 + (int32_t)b * 3;
        uint16_t c = (b < bars) ? on : off;
        cv_rect(bx, base - h, bw, h, c);
        bx += bw + gap;
    }
    cv_rect(bx, base - 2, 2, 2, bars > 0u ? on : off);
}

static uint32_t mt_wifi_bars(void)
{
    int st = wifi_get_state();
    if (st == WIFI_STATE_CONNECTED) return 3;
    if (st == WIFI_STATE_WPA || st == WIFI_STATE_ASSOC) return 2;
    if (st == WIFI_STATE_AUTH || st == WIFI_STATE_SCANNING) return 1;
    return 0;
}
#endif

/* header: page title left, wifi + "n/m" position right, a rule underneath */
static void mt_render_header(void)
{
    const modd_page_t *p = nav_active();
    char pos[8];
    uint32_t i = 0, cur = nav_cur + 1u, n = nav_npage;

    cv_begin(UI_W, UI_HEAD_H, T_BG);
    if (p)
        cv_text(6, ty_y(UI_HEAD_H / 2, TY_TX), TY_TX, p->title ? p->title : p->name, T_TEXT);

    int32_t rx = UI_W - 6;
    if (n > 1u) {
        if (cur >= 10u) pos[i++] = (char)('0' + cur / 10u);
        pos[i++] = (char)('0' + cur % 10u);
        pos[i++] = '/';
        if (n >= 10u) pos[i++] = (char)('0' + n / 10u);
        pos[i++] = (char)('0' + n % 10u);
        pos[i] = 0;
        rx = cv_text_r(UI_W - 6, ty_y(UI_HEAD_H / 2, TY_LB), TY_LB, pos, T_MID, T_BG);
    }

#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
    {
        uint32_t wb = mt_wifi_bars();
        mt_wifi_icon(rx - 16, UI_HEAD_H - 6, wb, wb == 3u ? T_THEME : T_MID, T_LINE);
    }
#endif

    cv_rect(0, UI_HEAD_H - 1, UI_W, 1, T_LINE);
    cv_blit(0, UI_Y_HEAD);
}

/* footer, Melodee's sound bar: engine left, preset centred, title right */
static void mt_render_footer(const char *eng, const char *preset, const char *title)
{
    int32_t y = ty_y(UI_FOOT_H / 2, TY_LB), l = 8, r = UI_W - 8, maxw, w, x;
    cv_begin(UI_W, UI_FOOT_H, T_SURF);
    if (eng && *eng)
        l = cv_text_on(8, y, TY_LB, eng, T_MID, T_SURF) + 10;
    if (title && *title)
        r = cv_text_r(UI_W - 8, y, TY_LB, title, T_MID, T_SURF) - 10;
    if (preset && *preset && (maxw = r - l) > 20) {
        const aafont_t *f = ty_fit(TY_TX, preset, maxw);
        w = text_w(f, preset);
        if (w > maxw) w = maxw;
        x = l + (maxw - w) / 2;
        cv_text_fit(x, ty_y(UI_FOOT_H / 2, f), f, preset, T_TEXT, T_SURF, maxw);
    }
    cv_blit(0, UI_Y_FOOT);
}

/* card c of four: label, value with its unit, a 3 px gauge for ratio 0..1000 in the caller's
 * colour. Icons are not drawn: this theme has none yet. */
static void mt_render_card(uint32_t c, const char *label, const char *val,
                           const char *unit, uint16_t color, int32_t ratio, uint32_t icon)
{
    const aafont_t *vf;
    int32_t x;
    (void)icon;
    if (c > 3u)
        return;
    cv_begin(UI_CARD_W, UI_CARD_H, T_BG);
    cv_rrect(0, 0, UI_CARD_W, UI_CARD_H, 6, T_SURF, T_BG);
    if (label && *label)
        cv_text_fit(6, 4, TY_LB, label, T_MID, T_SURF, UI_CARD_W - 12);
    if (val && *val) {
        vf = ty_fit(TY_TX, val, UI_CARD_W - 12);
        x = cv_text_on(6, 20, vf, val, color ? color : T_TEXT, T_SURF);
        if (unit && *unit && x + 4 + text_w(TY_LB, unit) <= UI_CARD_W - 6)
            cv_text_on(x + 3, 20 + (vf->cap - TY_LB->cap), TY_LB, unit, T_MID, T_SURF);
    }
    if (ratio >= 0) {
        int32_t r = ratio > 1000 ? 1000 : ratio, w = UI_CARD_W - 12;
        cv_rect(6, UI_CARD_H - 8, w, 3, T_LINE);
        cv_rect(6, UI_CARD_H - 8, w * r / 1000, 3, color ? color : T_THEME);
    }
    cv_blit(UI_CARD_X(c), UI_Y_CARD);
}

/* a page's panel: the area between the cards and the sound bar, cleared to BG */
static void mt_panel_begin(void) { cv_begin(UI_W, UI_PANEL_H, T_BG); }
static void mt_panel_end(void)   { cv_blit(0, UI_Y_PANEL); }

static const ui_theme_t modd_theme = {
    .screen_w  = UI_W,
    .screen_h  = UI_H,
    .header_h  = UI_HEAD_H,
    .footer_h  = UI_FOOT_H,
    .card_h    = UI_CARD_H,
    .card_w    = UI_CARD_W,

    .font_s    = &AF_LB,
    .font_m    = &AF_TX,
    .font_l    = &AF_VL,

    .render_header = mt_render_header,
    .render_footer = mt_render_footer,
    .render_card   = mt_render_card,
};
