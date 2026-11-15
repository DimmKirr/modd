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
#define UI_HEAD_H  24
#define UI_Y_CARD  26
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

/* shared formatting: unsigned int to decimal string */
static void ui_fmt_u32(char *out, uint32_t v)
{
    char tmp[12];
    uint32_t i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v && i < 11) { tmp[i++] = '0' + (char)(v % 10u); v /= 10u; }
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

/* hint badges and zone outlines: drawn by widgets when hints_visible is set.
 * hints_visible is declared in modd.c before ui_nav.c so both nav and theme see it.
 * mt_zone_count tracks how many zones were drawn; shell falls back to text when zero. */

static uint32_t mt_zone_count;
static struct { int32_t x; int32_t w; } mt_foot[2];  /* [0]=left pill, [1]=right pill */

/* hint color per hardware control: each encoder/button gets its own color
 * so zones are visually distinct. derived from the 4 palette track colors
 * plus blends. the color follows the physical control, not the zone. */
#define HINT_CTL_ENC  0
#define HINT_CTL_BTN  1

static uint16_t ui_hint_enc_color(uint8_t enc_id)
{
    switch (enc_id) {
    case ENC_SELECT: return T_TRK(0);
    case ENC_ALGO:   return T_TRK(1);
    case ENC_K1:     return T_TRK(2);
    case ENC_K2:     return ux_mix(T_TRK(1), T_TRK(2), 50);
    case ENC_K3:     return T_TRK(3);
    case ENC_K4:     return ux_mix(T_TRK(2), T_TRK(3), 50);
    case ENC_PRESET: return ux_mix(T_TRK(0), T_TRK(3), 50);
    default:         return T_THEME;
    }
}

static uint16_t ui_hint_btn_color(uint8_t btn_id)
{
    uint16_t base;
    switch (btn_id) {
    case BTN_OCTDN:  return ux_mix(T_TRK(0), T_BG, 35);
    case BTN_OCTUP:  return ux_mix(T_TRK(3), T_BG, 35);
    case BTN_FX:     base = T_TRK(0); break;
    case BTN_SCL:    base = T_TRK(1); break;
    case BTN_ENV:    base = T_TRK(2); break;
    case BTN_LFO:    base = T_TRK(3); break;
    case BTN_EDIT:   base = ux_mix(T_TRK(0), T_TRK(1), 50); break;
    case BTN_GLO:    base = ux_mix(T_TRK(0), T_TRK(3), 50); break;
    case BTN_HOME:   base = ux_mix(T_TRK(1), T_TRK(2), 50); break;
    case BTN_SAVE:   base = ux_mix(T_TRK(2), T_TRK(3), 50); break;
    case BTN_ARP:    base = ux_mix(T_TRK(0), T_TRK(2), 50); break;
    case BTN_SEQ:    base = ux_mix(T_TRK(1), T_TRK(3), 50); break;
    case BTN_PLAY:   return T_MID;
    case BTN_REC:    return ux_mix(T_THEME, T_BG, 35);
    default:         return T_THEME;
    }
    return ux_mix(base, T_BG, 35);
}

/* occupied-rect registry for overlap avoidance (reset each frame) */
#define HINT_MAX_RECTS 12
static struct { int32_t x, y, w, h; } hint_rects[HINT_MAX_RECTS];
static uint32_t hint_n_rects;

static void hint_push_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (hint_n_rects < HINT_MAX_RECTS) {
        hint_rects[hint_n_rects].x = x;
        hint_rects[hint_n_rects].y = y;
        hint_rects[hint_n_rects].w = w;
        hint_rects[hint_n_rects].h = h;
        hint_n_rects++;
    }
}

static int32_t hint_overlap(int32_t bx, int32_t by, int32_t bw, int32_t bh)
{
    uint32_t i;
    int32_t total = 0;
    for (i = 0; i < hint_n_rects; i++) {
        int32_t ox = hint_rects[i].x, oy = hint_rects[i].y;
        int32_t ow = hint_rects[i].w, oh = hint_rects[i].h;
        int32_t dx = (bx + bw < ox + ow ? bx + bw : ox + ow) - (bx > ox ? bx : ox);
        int32_t dy = (by + bh < oy + oh ? by + bh : oy + oh) - (by > oy ? by : oy);
        if (dx > 0 && dy > 0)
            total += dx * dy;
    }
    return total;
}

/* deferred badge array: collected during draw, rendered as overlay after all canvases */
#define HINT_BADGE_H   14
#define HINT_MAX_BADGES 8

static struct {
    int32_t  sx, sy;
    uint16_t color;
    uint8_t  enc_id;
    uint8_t  is_btn;
} mt_badges[HINT_MAX_BADGES];
static uint32_t mt_badge_count;

static void hint_defer_badge(int32_t screen_x, int32_t screen_y,
                             uint8_t id, uint8_t is_btn, uint16_t color)
{
    if (mt_badge_count >= HINT_MAX_BADGES) return;
    mt_badges[mt_badge_count].sx = screen_x;
    mt_badges[mt_badge_count].sy = screen_y;
    mt_badges[mt_badge_count].color = color;
    mt_badges[mt_badge_count].enc_id = id;
    mt_badges[mt_badge_count].is_btn = is_btn;
    mt_badge_count++;
}

static const char *ui_enc_label(uint8_t enc)
{
    if (enc < ENC_COUNT) return CTL_ENC_NAME[enc];
    return 0;
}

static void hint_hatch_fill(int32_t w, int32_t h, uint16_t stripe)
{
    int32_t x, y;
    for (y = 1; y < h - 1; y++)
        for (x = 1; x < w - 1; x++)
            if (((x + y) % 3) == 0)
                cv_pset(x, y, stripe);
}

static void hint_draw_deferred_badges(void)
{
    uint32_t i;
    for (i = 0; i < mt_badge_count; i++) {
        const char *lbl = mt_badges[i].is_btn
            ? CTL_BTN_NAME[mt_badges[i].enc_id]
            : ui_enc_label(mt_badges[i].enc_id);
        if (!lbl || !lbl[0]) continue;
        int32_t tw = text_w(TY_LB, lbl);
        int32_t pw = tw + 8;
        int32_t bx = mt_badges[i].sx;
        if (bx + pw > UI_W) bx = UI_W - pw;
        if (bx < 0) bx = 0;
        uint16_t fill = mt_badges[i].color;
        uint8_t hatch = mt_badges[i].is_btn
            && mt_badges[i].enc_id != BTN_OCTDN
            && mt_badges[i].enc_id != BTN_OCTUP;
        cv_begin(pw, HINT_BADGE_H, fill);
        cv_rrect(0, 0, pw, HINT_BADGE_H, 3, fill, fill);
        if (hatch)
            hint_hatch_fill(pw, HINT_BADGE_H, ux_mix(fill, T_BG, 50));
        cv_text_fit(4, ty_y(HINT_BADGE_H / 2, TY_LB), TY_LB, lbl, T_BG, fill, tw);
        cv_blit(bx, mt_badges[i].sy);
    }
}

static void ui_hint_badge_at(int32_t x, int32_t y, const char *label, uint16_t color)
{
    if (!label || !label[0]) return;
    int32_t tw = text_w(TY_LB, label);
    int32_t pw = tw + 8;
    if (x + pw > UI_W) x = UI_W - pw;
    if (x < 0) x = 0;
    cv_rrect(x, y, pw, HINT_BADGE_H, 3, color, T_BG);
    cv_text_fit(x + 4, ty_y(y + HINT_BADGE_H / 2, TY_LB), TY_LB, label, T_BG, color, tw);
}

/* find the best corner of a zone outline for a badge and defer it.
 * ox,oy,ow,oh: outline rect in canvas coords.
 * enc_id: encoder ID for the badge. color: badge fill. slot: 0-based for stacking. */
static void ui_zone_place_badge(int32_t ox, int32_t oy, int32_t ow, int32_t oh,
                                int32_t screen_y, uint8_t enc_id, uint16_t color,
                                uint32_t slot)
{
    const char *label = ui_enc_label(enc_id);
    if (!label || !label[0]) return;
    int32_t bw = text_w(TY_LB, label) + 8;
    int32_t bh = HINT_BADGE_H;
    int32_t stack = (int32_t)slot * (bh + 2);

    struct { int32_t x, y; } cands[4] = {
        { ox + ow - bw,  oy - bh - stack },
        { ox,            oy - bh - stack },
        { ox + ow - bw,  oy + oh + stack },
        { ox,            oy + oh + stack },
    };

    int32_t best_ov = 0x7FFFFFFF;
    uint32_t best = 0, c;
    for (c = 0; c < 4u; c++) {
        int32_t cx = cands[c].x, cy = cands[c].y;
        if (cx < 0) cx = 0;
        if (cx + bw > UI_W) cx = UI_W - bw;
        int32_t ov = hint_overlap(cx, cy + screen_y, bw, bh);
        if (cy < 0) ov += (-cy) * bw;
        if (ov < best_ov) {
            best_ov = ov;
            best = c;
        }
        if (ov == 0) break;
    }

    int32_t bx = cands[best].x, by = cands[best].y;
    if (bx < 0) bx = 0;
    if (bx + bw > UI_W) bx = UI_W - bw;

    hint_defer_badge(bx, by + screen_y, enc_id, 0, color);
    hint_push_rect(bx, by + screen_y, bw, bh);
}

/* draw a zone outline + per-encoder badge(s).
 * ox,oy,ow,oh: outline in canvas coords. screen_y: canvas blit y.
 * enc_ids[]: encoder IDs for each badge. n: count.
 * outline color = first encoder's color. each badge gets its own encoder color. */
static void ui_zone_enc(int32_t ox, int32_t oy, int32_t ow, int32_t oh,
                        int32_t screen_y,
                        const uint8_t *enc_ids, uint32_t n)
{
    uint16_t outline_color = n > 0 ? ui_hint_enc_color(enc_ids[0]) : T_THEME;
    uint32_t i;
    cv_frame(ox, oy, ow, oh, outline_color);
    hint_push_rect(ox, oy + screen_y, ow, oh);
    for (i = 0; i < n; i++) {
        uint16_t bc = ui_hint_enc_color(enc_ids[i]);
        ui_zone_place_badge(ox, oy, ow, oh, screen_y, enc_ids[i], bc, i);
    }
    mt_zone_count++;
}

/* tab bar widget: content-sized tabs with selection highlight and rule */
typedef struct {
    const char *const *labels;
    uint32_t count;
    uint8_t  active;
    uint8_t  enc;                        /* encoder that cycles tabs (ENC_NONE = none) */
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
    if (hints_visible && t->enc != ENC_NONE) {
        uint16_t hc = ui_hint_enc_color(t->enc);
        cv_frame(0, 0, UI_W, UI_TAB_H + UI_RULE_H, hc);
        hint_push_rect(0, UI_Y_CARD, UI_W, UI_TAB_H + UI_RULE_H);
        mt_zone_count++;
    }
    return UI_TAB_H + UI_RULE_H;
}

/* defer the tab bar's badge above the tab outline, in the header area.
 * tries right-aligned first; shifts left if it overlaps existing rects. */
static void ui_tabs_place_badge(const ui_tabs_t *t, int32_t screen_y)
{
    if (!hints_visible || t->enc == ENC_NONE) return;
    uint16_t hc = ui_hint_enc_color(t->enc);
    const char *lbl = ui_enc_label(t->enc);
    int32_t bw = text_w(TY_LB, lbl) + 8;
    int32_t by = screen_y - HINT_BADGE_H - 1;
    int32_t bx;
    if (by < 0) by = 0;
    for (bx = UI_W - bw - 1; bx >= 0; bx -= bw + UI_GAP) {
        if (hint_overlap(bx, by, bw, HINT_BADGE_H) == 0) break;
    }
    if (bx < 0) bx = 0;
    hint_defer_badge(bx, by, t->enc, 0, hc);
    hint_push_rect(bx, by, bw, HINT_BADGE_H);
}

static int ui_tabs_input(const ui_tabs_t *t, uint32_t id, int32_t clicks,
                         void (*cycle)(int32_t dir))
{
    if (t->enc == ENC_NONE || id != t->enc) return 0;
    if (cycle) cycle(clicks);
    nav_dirty = 1;
    return 1;
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

    {
        int32_t ix = rx - 4;
#ifdef CONFIG_USB_HOST
        if (udisk_mounted) {
            cv_frame(ix - 9, UI_HEAD_H - 13, 9, 7, T_TEXT);
            cv_rect(ix - 4, UI_HEAD_H - 11, 2, 2, T_TEXT);
            ix -= 13;
        }
#endif
#ifdef CONFIG_BLE
        if (ble_connected()) {
            cv_text(ix - 7, ty_y(UI_HEAD_H / 2, TY_LB), TY_LB, "B", T_TEXT);
            ix -= 11;
        }
#endif
#ifdef CONFIG_USB_NET
        if (ecm_is_up()) {
            uint16_t c = net_get_ip() ? T_TEXT : T_DIM;
            cv_text(ix - 7, ty_y(UI_HEAD_H / 2, TY_LB), TY_LB, "E", c);
            ix -= 11;
        }
#endif
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
        {
            uint32_t wb = mt_wifi_bars();
            if (wb > 0 || wifi_get_state() != WIFI_STATE_OFF)
                mt_wifi_icon(ix - 11, UI_HEAD_H - 6, wb, wb == 3u ? T_THEME : T_MID, T_LINE);
        }
#endif
    }

    if (hints_visible)
        hint_push_rect(rx - 2, UI_Y_HEAD, UI_W - rx + 2, UI_HEAD_H);

    cv_rect(0, UI_HEAD_H - 1, UI_W, 1, T_LINE);
    cv_blit(0, UI_Y_HEAD);
}

/* footer: action hints for OCT-/OCT+ in left/right, context label centred.
 * Non-empty left/right are rendered as tinted pills so the user can tell
 * at a glance which buttons do something (action bar) vs. passive info
 * (context bar).  Upstream Felucca/Melodee/SLOOP flip the whole footer to
 * keycap-labelled hints in action mode; this is modd's simpler take. */
static void mt_render_footer(const char *left, const char *center, const char *right)
{
    int32_t y = ty_y(UI_FOOT_H / 2, TY_LB), l = 8, r = UI_W - 8, maxw, w, x;
    cv_begin(UI_W, UI_FOOT_H, T_SURF);

    if (left && *left) {
        int32_t tw = text_w(TY_LB, left);
        int32_t pw = tw + 12;
        uint16_t pill = ux_mix(T_SURF, T_THEME, 18);
        cv_rrect(4, 5, pw, UI_FOOT_H - 10, 4, pill, T_SURF);
        cv_text_fit(10, y, TY_LB, left, T_TEXT, pill, tw);
        l = pw + 10;
    }

    if (right && *right) {
        int32_t tw = text_w(TY_LB, right);
        int32_t pw = tw + 12;
        uint16_t pill = ux_mix(T_SURF, T_THEME, 18);
        cv_rrect(UI_W - 4 - pw, 5, pw, UI_FOOT_H - 10, 4, pill, T_SURF);
        cv_text_fit(UI_W - 4 - pw + 6, y, TY_LB, right, T_TEXT, pill, tw);
        r = UI_W - pw - 8;
    }

    if (center && *center && (maxw = r - l) > 20) {
        const aafont_t *f = ty_fit(TY_TX, center, maxw);
        w = text_w(f, center);
        if (w > maxw) w = maxw;
        x = l + (maxw - w) / 2;
        cv_text_fit(x, ty_y(UI_FOOT_H / 2, f), f, center, T_MID, T_SURF, maxw);
    }

    if (hints_visible) {
        if (left && *left) {
            uint16_t bc = ui_hint_btn_color(BTN_OCTDN);
            int32_t pw = text_w(TY_LB, left) + 12;
            cv_frame(3, 4, pw + 2, UI_FOOT_H - 8, bc);
            hint_push_rect(3, UI_Y_FOOT + 4, pw + 2, UI_FOOT_H - 8);
            mt_foot[0].x = 3; mt_foot[0].w = pw + 2;
            mt_zone_count++;
        }
        if (right && *right) {
            uint16_t bc = ui_hint_btn_color(BTN_OCTUP);
            int32_t pw = text_w(TY_LB, right) + 12;
            int32_t rx = UI_W - 4 - pw - 1;
            cv_frame(rx, 4, pw + 2, UI_FOOT_H - 8, bc);
            hint_push_rect(rx, UI_Y_FOOT + 4, pw + 2, UI_FOOT_H - 8);
            mt_foot[1].x = rx; mt_foot[1].w = pw + 2;
            mt_zone_count++;
        }
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

/* scrollbar: 3px track with a 6px square thumb at the right edge.
 * shown only when total > visible. y0/h: the row area in canvas coords. */
static void ui_scrollbar(int32_t y0, int32_t h, uint32_t total, uint32_t visible, uint32_t scroll)
{
    int32_t track, thumb, ty;
    if (total <= visible || visible == 0) return;
    track = h - 2;
    thumb = 6;
    ty = (track - thumb) * (int32_t)scroll / (int32_t)(total - visible);
    cv_rect(UI_W - 4, y0, 3, track, T_LINE);
    cv_rect(UI_W - 4, y0 + ty, 3, thumb, T_MID);
}

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
