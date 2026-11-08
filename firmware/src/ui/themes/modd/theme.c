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

#define MT_W       240
#define MT_H       240
#define MT_Y_HEAD  0
#define MT_H_HEAD  30
#define MT_Y_CARD  34
#define MT_CARD_W  57
#define MT_CARD_H  48
#define MT_CARD_X(c) (2 + 60 * (int32_t)(c))
#define MT_Y_PANEL 84                 /* the page's own area */
#define MT_H_PANEL 124
#define MT_Y_FOOT  212
#define MT_H_FOOT  28

/* header: page title left, "n/m" position right, a rule underneath */
static void mt_render_header(void)
{
    const modd_page_t *p = nav_active();
    char pos[8];
    uint32_t i = 0, cur = nav_cur + 1u, n = nav_npage;

    cv_begin(MT_W, MT_H_HEAD, T_BG);
    if (p)
        cv_text(6, ty_y(MT_H_HEAD / 2, TY_TX), TY_TX, p->title ? p->title : p->name, T_TEXT);
    if (n > 1u) {
        if (cur >= 10u) pos[i++] = (char)('0' + cur / 10u);
        pos[i++] = (char)('0' + cur % 10u);
        pos[i++] = '/';
        if (n >= 10u) pos[i++] = (char)('0' + n / 10u);
        pos[i++] = (char)('0' + n % 10u);
        pos[i] = 0;
        cv_text_r(MT_W - 6, ty_y(MT_H_HEAD / 2, TY_LB), TY_LB, pos, T_MID, T_BG);
    }
    cv_rect(0, MT_H_HEAD - 1, MT_W, 1, T_LINE);
    cv_blit(0, MT_Y_HEAD);
}

/* footer, Melodee's sound bar: engine left, preset centred, title right */
static void mt_render_footer(const char *eng, const char *preset, const char *title)
{
    int32_t y = ty_y(MT_H_FOOT / 2, TY_LB), l = 8, r = MT_W - 8, maxw, w, x;
    cv_begin(MT_W, MT_H_FOOT, T_SURF);
    if (eng && *eng)
        l = cv_text_on(8, y, TY_LB, eng, T_MID, T_SURF) + 10;
    if (title && *title)
        r = cv_text_r(MT_W - 8, y, TY_LB, title, T_MID, T_SURF) - 10;
    if (preset && *preset && (maxw = r - l) > 20) {
        const aafont_t *f = ty_fit(TY_TX, preset, maxw);
        w = text_w(f, preset);
        if (w > maxw) w = maxw;
        x = l + (maxw - w) / 2;
        cv_text_fit(x, ty_y(MT_H_FOOT / 2, f), f, preset, T_TEXT, T_SURF, maxw);
    }
    cv_blit(0, MT_Y_FOOT);
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
    cv_begin(MT_CARD_W, MT_CARD_H, T_BG);
    cv_rrect(0, 0, MT_CARD_W, MT_CARD_H, 6, T_SURF, T_BG);
    if (label && *label)
        cv_text_fit(6, 4, TY_LB, label, T_MID, T_SURF, MT_CARD_W - 12);
    if (val && *val) {
        vf = ty_fit(TY_TX, val, MT_CARD_W - 12);
        x = cv_text_on(6, 20, vf, val, color ? color : T_TEXT, T_SURF);
        if (unit && *unit && x + 4 + text_w(TY_LB, unit) <= MT_CARD_W - 6)
            cv_text_on(x + 3, 20 + (vf->cap - TY_LB->cap), TY_LB, unit, T_MID, T_SURF);
    }
    if (ratio >= 0) {
        int32_t r = ratio > 1000 ? 1000 : ratio, w = MT_CARD_W - 12;
        cv_rect(6, MT_CARD_H - 8, w, 3, T_LINE);
        cv_rect(6, MT_CARD_H - 8, w * r / 1000, 3, color ? color : T_THEME);
    }
    cv_blit(MT_CARD_X(c), MT_Y_CARD);
}

/* a page's panel: the area between the cards and the sound bar, cleared to BG */
static void mt_panel_begin(void) { cv_begin(MT_W, MT_H_PANEL, T_BG); }
static void mt_panel_end(void)   { cv_blit(0, MT_Y_PANEL); }

static const ui_theme_t modd_theme = {
    .screen_w  = MT_W,
    .screen_h  = MT_H,
    .header_h  = MT_H_HEAD,
    .footer_h  = MT_H_FOOT,
    .card_h    = MT_CARD_H,
    .card_w    = MT_CARD_W,

    .font_s    = &AF_LB,
    .font_m    = &AF_TX,
    .font_l    = &AF_VL,

    .render_header = mt_render_header,
    .render_footer = mt_render_footer,
    .render_card   = mt_render_card,
};
