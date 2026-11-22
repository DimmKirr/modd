// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* DEMO: the widget showcase, as
 * LVGL widgets. FX held twice opens it (VIEW_CONN, after WX).
 *
 *   CARDS   the palette's tones as swatches; cards: frame, screen, K1, the last note
 *   LIST    16 rows with a bar each, K1 selects, the list scrolls to it
 *   GRAPH   16 bars moving
 *   TEXT    the type sizes, left / centre / right, three buttons
 *   EDIT    an inline text editor: K1 moves the cursor, K2 changes the character
 *   MODALS  three dialog types, K1 picks one, a mock dialog shows it
 *   TOAST   K1 raises the six toast states (modd_toast.h), shown over the footer
 *
 * SELECT changes the screen, past the first or last the page; K1-K4
 * set 0..99 where a screen has no use for them; a note key shows on CARDS. The screen's
 * widgets are made when it is shown: one screen's worth of LVGL heap at a time. */
#ifdef CONFIG_PAGE_DEMO

#define MDEMO_SCREENS 7u
#define MDEMO_LIST_N  16u
#define MDEMO_BARS    16u
#define MDEMO_MODAL_N 3u
#define MDEMO_TOAST_N 6u
#define MDEMO_TEXT_MAX 31u

static const char *const MDEMO_NAMES[MDEMO_SCREENS] = {
    "CARDS", "LIST", "GRAPH", "TEXT", "EDIT", "MODALS", "TOAST"
};

static const char *const MDEMO_LIST[MDEMO_LIST_N] = {
    "Oscillator", "Filter", "Envelope", "LFO",
    "Reverb", "Delay", "Chorus", "Distortion",
    "Sequencer", "Arpeggiator", "Mixer", "Compressor",
    "Phaser", "Flanger", "EQ", "Limiter"
};

static const char *const MDEMO_MODAL_NAMES[MDEMO_MODAL_N] = { "YES / NO", "OK Info", "Text Input" };

static const char *const MDEMO_TOAST_MSGS[MDEMO_TOAST_N] = {
    "(clear)", "Scanning WiFi", "Connecting WiFi", "Acquiring IP", "Fetching weather", "WiFi disconnected"
};
static const uint8_t MDEMO_TOAST_KIND[MDEMO_TOAST_N] = {
    TOAST_NONE, TOAST_STATUS, TOAST_STATUS, TOAST_STATUS, TOAST_STATUS, TOAST_ERROR
};

static char mdemo_text[MDEMO_TEXT_MAX + 1u] = "Hello FM-1!";

static struct {
    uint8_t  screen, val[4], last_note, last_vel, edit_cursor, modal_sel, dirty;
    uint32_t frame, tick_ms;
    lv_obj_t *panel;
    lv_obj_t *item[MDEMO_LIST_N];       /* LIST, MODALS, TOAST rows; GRAPH bars */
    lv_obj_t *edit, *cursor, *info, *modal_title, *modal_body;
} mdemo;

/* -------------------------------------------------------------- widgets --- */
static lv_obj_t *mdemo_label(lv_obj_t *parent, const char *text, const lv_style_t *colour)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_add_style(l, &lvt_st_small, 0);
    if (colour)
        lv_obj_add_style(l, (lv_style_t *)colour, 0);
    lv_label_set_text_static(l, text);
    return l;
}

/* a box placed by hand, out of the panel's column */
static lv_obj_t *mdemo_rect(lv_obj_t *parent, int32_t w, int32_t h, lv_color_t c, int32_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *mdemo_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t fg)
{
    lv_obj_t *b = lv_button_create(parent), *l;
    lv_obj_set_size(b, 64, 18);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_clickable(b, false);
    l = mdemo_label(b, text, 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_center(l);
    return b;
}

/* a list row: "nn  Name" and a bar of `bar` px (0: none) */
static lv_obj_t *mdemo_row(lv_obj_t *parent, const char *num, const char *name, int32_t bar)
{
    lv_obj_t *row = lvt_row(parent, 18), *l;
    if (num)
        mdemo_label(row, num, &lvt_st_dim);
    l = lv_label_create(row);
    lv_label_set_text_static(l, name);
    lv_obj_set_flex_grow(l, 1);
    if (bar) {
        mdemo_rect(row, bar, 8, lvt_pal.mid, 2);
    }
    return row;
}

/* ------------------------------------------------------------ screens --- */
static void mdemo_build_cards(lv_obj_t *p)
{
    struct { const char *name; lv_color_t c; } sw[10];
    uint32_t i;
    lv_obj_t *grid = lvt_box(p, LV_FLEX_FLOW_ROW_WRAP);
    sw[0].name = "BG";    sw[0].c = lvt_pal.bg;
    sw[1].name = "TEXT";  sw[1].c = lvt_pal.text;
    sw[2].name = "SURF";  sw[2].c = lvt_pal.surf;
    sw[3].name = "MID";   sw[3].c = lvt_pal.mid;
    sw[4].name = "THEME"; sw[4].c = lvt_pal.primary;
    sw[5].name = "DIM";   sw[5].c = lvt_pal.dim;
    sw[6].name = "ACC";   sw[6].c = lvt_pal.accent;
    sw[7].name = "LINE";  sw[7].c = lvt_pal.line;
    sw[8].name = "RAISE"; sw[8].c = lvt_mix(lvt_pal.surf, lvt_pal.text, UI_RAISE_PCT);
    sw[9].name = "INK";   sw[9].c = lvt_pal.bg;
    lv_obj_set_style_pad_row(grid, 4, 0);
    for (i = 0; i < 10u; i++) {
        lv_obj_t *cell = lvt_box(grid, LV_FLEX_FLOW_ROW);
        lv_obj_t *r;
        lv_obj_set_width(cell, lv_pct(50));
        lv_obj_set_style_pad_column(cell, 4, 0);
        r = mdemo_rect(cell, 24, 12, sw[i].c, 0);
        lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_style_border_color(r, lvt_pal.line, 0);
        mdemo_label(cell, sw[i].name, &lvt_st_mid);
    }
}

static void mdemo_build_list(lv_obj_t *p)
{
    uint32_t i;
    lv_obj_set_scrollable(p, true);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_style_pad_row(p, 0, 0);
    for (i = 0; i < MDEMO_LIST_N; i++) {
        static const char *const NUM[MDEMO_LIST_N] = {
            " 0", " 1", " 2", " 3", " 4", " 5", " 6", " 7", " 8", " 9", "10", "11", "12", "13", "14", "15"
        };
        int32_t bw = (int32_t)(i * 30u / MDEMO_LIST_N);
        mdemo.item[i] = mdemo_row(p, NUM[i], MDEMO_LIST[i], bw < 2 ? 2 : bw);
    }
}

static void mdemo_build_graph(lv_obj_t *p)
{
    lv_obj_t *row = lvt_box(p, LV_FLEX_FLOW_ROW);
    uint32_t i;
    lv_obj_set_height(row, lv_pct(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lvt_pal.line, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (i = 0; i < MDEMO_BARS; i++)
        mdemo.item[i] = mdemo_rect(row, 11, 4, lvt_pal.primary, 2);
}

static void mdemo_build_text(lv_obj_t *p)
{
    lv_obj_t *l, *row;
    l = mdemo_label(p, "S: The quick brown fox", 0);
    l = lv_label_create(p);
    lv_label_set_text_static(l, "M: ABCDEFGH");
    l = lv_label_create(p);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_label_set_text_static(l, "L: 12345");
    row = lvt_box(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    mdemo_label(row, "LEFT", &lvt_st_mid);
    mdemo_label(row, "CENTER", &lvt_st_theme);
    l = mdemo_label(row, "RIGHT", 0);
    lv_obj_set_style_text_color(l, lvt_pal.accent, 0);
    row = lvt_box(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_margin_top(row, 6, 0);
    mdemo_button(row, "CANCEL", lvt_pal.surf, lvt_pal.text);
    mdemo_button(row, "OK", lvt_pal.primary, lvt_pal.bg);
    mdemo_button(row, "DELETE", lvt_pal.accent, lvt_pal.bg);
}

static void mdemo_build_edit(lv_obj_t *p)
{
    lv_obj_t *box;
    mdemo_label(p, "INLINE TEXT EDITOR", &lvt_st_dim);
    mdemo_label(p, "K1: cursor  K2: character", &lvt_st_dim);
    box = lvt_box(p, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(box, &lvt_st_card, 0);
    lv_obj_set_height(box, 28);
    lv_obj_set_style_pad_hor(box, 6, 0);
    mdemo.edit = lv_label_create(box);
    lv_label_set_text_static(mdemo.edit, mdemo_text);
    mdemo.cursor = mdemo_rect(box, 8, 2, lvt_pal.accent, 0);
    lv_obj_set_ignore_layout(mdemo.cursor, true);
    mdemo.info = mdemo_label(p, "", &lvt_st_mid);
    mdemo_label(p, "Turn K2 to change char value", &lvt_st_dim);
}

static void mdemo_build_modals(lv_obj_t *p)
{
    lv_obj_t *m, *bar;
    uint32_t i;
    static const char *const NUM[MDEMO_MODAL_N] = { "1.", "2.", "3." };
    mdemo_label(p, "MODAL TYPES (K1 to select)", &lvt_st_dim);
    for (i = 0; i < MDEMO_MODAL_N; i++)
        mdemo.item[i] = mdemo_row(p, NUM[i], MDEMO_MODAL_NAMES[i], 0);
    /* the mock dialog over the list */
    m = lvt_box(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_ignore_layout(m, true);
    lv_obj_add_style(m, &lvt_st_card, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_row(m, 4, 0);
    lv_obj_set_style_border_width(m, 1, 0);
    lv_obj_set_style_border_color(m, lvt_pal.line, 0);
    lv_obj_set_style_clip_corner(m, true, 0);
    lv_obj_set_size(m, LVT_W - 40, 76);
    lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, -4);
    bar = lvt_box(m, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(bar, 16);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lvt_pal.primary, 0);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    mdemo.modal_title = mdemo_label(bar, "", 0);
    lv_obj_set_style_text_color(mdemo.modal_title, lvt_pal.bg, 0);
    mdemo.modal_body = lvt_box(m, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_grow(mdemo.modal_body, 1);
    lv_obj_set_style_pad_hor(mdemo.modal_body, 8, 0);
    lv_obj_set_style_pad_row(mdemo.modal_body, 4, 0);
    lv_obj_set_flex_align(mdemo.modal_body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

/* the dialog's body for the type picked: made again when K1 picks another */
static void mdemo_modal_show(void)
{
    lv_obj_t *b = mdemo.modal_body, *l;
    lv_obj_clean(b);
    lvt_text(mdemo.modal_title, MDEMO_MODAL_NAMES[mdemo.modal_sel]);
    switch (mdemo.modal_sel) {
    case 0:
        l = mdemo_label(b, "Are you sure?", 0);
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        mdemo_button(b, "YES", lvt_pal.accent, lvt_pal.bg);
        mdemo_button(b, "NO", lvt_pal.dim, lvt_pal.text);
        break;
    case 1:
        l = mdemo_label(b, "Operation complete.", 0);
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        mdemo_button(b, "OK", lvt_pal.primary, lvt_pal.bg);
        break;
    default:
        l = lvt_box(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(l, lvt_pal.bg, 0);
        lv_obj_set_style_radius(l, 3, 0);
        lv_obj_set_style_pad_all(l, 3, 0);
        mdemo_label(l, "type here...", &lvt_st_dim);
        mdemo_rect(l, 1, 10, lvt_pal.accent, 0);
        break;
    }
}

static void mdemo_build_toast(lv_obj_t *p)
{
    uint32_t i;
    mdemo_label(p, "TOAST NOTIFICATIONS", &lvt_st_dim);
    mdemo_label(p, "K1: cycle toast states", &lvt_st_dim);
    for (i = 0; i < MDEMO_TOAST_N; i++)
        mdemo.item[i] = mdemo_row(p, 0, MDEMO_TOAST_MSGS[i], 0);
}

static void mdemo_build(void)
{
    lv_obj_t *p = mdemo.panel;
    lv_obj_clean(p);
    lv_obj_set_scrollable(p, false);
    lv_obj_set_style_pad_row(p, 2, 0);
    switch (mdemo.screen) {
    case 0: mdemo_build_cards(p);  break;
    case 1: mdemo_build_list(p);   break;
    case 2: mdemo_build_graph(p);  break;
    case 3: mdemo_build_text(p);   break;
    case 4: mdemo_build_edit(p);   break;
    case 5: mdemo_build_modals(p); mdemo_modal_show(); break;
    default: mdemo_build_toast(p); break;
    }
    lvt_footer("", MDEMO_NAMES[mdemo.screen], "");
    mdemo.dirty = 1;
}

/* ------------------------------------------------------------- updates --- */
static void mdemo_num(char *b, uint32_t v) { lv_snprintf(b, 8, "%u", (unsigned)v); }

static void mdemo_rows_sel(uint32_t n, uint32_t sel, int scroll)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        lv_obj_set_state(mdemo.item[i], LV_STATE_CHECKED, i == sel);
    if (scroll && sel < n)
        lv_obj_scroll_to_view(mdemo.item[sel], LV_ANIM_OFF);
}

/* what changes on the screen: on input (dirty), and every 50 ms where something moves */
static void mdemo_show(void)
{
    char a[8], b[8], c[8];
    uint32_t i;
    switch (mdemo.screen) {
    case 0:
        mdemo_num(a, mdemo.frame & 0x7FFFu);
        lv_snprintf(b, sizeof b, "%u/%u", (unsigned)mdemo.screen, (unsigned)(MDEMO_SCREENS - 1u));
        mdemo_num(c, mdemo.val[0]);
        lvt_card_set(0, "FRA", a, "", lvt_pal.primary, (int32_t)(mdemo.frame % 1000u));
        lvt_card_set(1, "SCR", b, "", lvt_pal.accent, (int32_t)mdemo.screen * 200);
        lvt_card_set(2, "K1", c, "", lvt_pal.mid, (int32_t)mdemo.val[0] * 100);
        mdemo_num(a, mdemo.last_note);
        lvt_card_set(3, "NOTE", a, "", mdemo.last_vel ? lvt_pal.primary : lvt_pal.dim,
                     mdemo.last_vel ? (int32_t)mdemo.last_vel * 8 : -1);
        break;
    case 1:
        mdemo_num(a, MDEMO_LIST_N);
        mdemo_num(b, mdemo.val[0]);
        lvt_card_set(0, "ITM", a, "", lvt_pal.mid, -1);
        lvt_card_set(1, "SEL", b, "", lvt_pal.primary, (int32_t)(mdemo.val[0] * 1000u / MDEMO_LIST_N));
        lvt_card_set(2, "", "", "", lvt_pal.dim, -1);
        lvt_card_set(3, "", "", "", lvt_pal.dim, -1);
        mdemo_rows_sel(MDEMO_LIST_N, mdemo.val[0], 1);
        break;
    case 2:
        lvt_card_set(0, "BARS", "16", "", lvt_pal.mid, -1);
        lvt_card_set(1, "FRM", "", "", lvt_pal.dim, (int32_t)(mdemo.frame % 1000u));
        lvt_card_set(2, "ANIM", "ON", "", lvt_pal.primary, -1);
        lvt_card_set(3, "STL", "BAR", "", lvt_pal.dim, -1);
        {
            int32_t full = lv_obj_get_content_height(lv_obj_get_parent(mdemo.item[0])) - 4;
            for (i = 0; i < MDEMO_BARS; i++) {
                uint32_t seed = (mdemo.frame / 3u + i * 37u) & 0xFFu;
                int32_t h = (int32_t)seed * full / 256;
                lvt_height(mdemo.item[i], h < 4 ? 4 : h);
                lvt_bg_color(mdemo.item[i], i == (mdemo.frame / 4u) % MDEMO_BARS ? lvt_pal.accent
                                                                               : lvt_pal.primary, 0);
            }
        }
        break;
    case 3:
        lvt_card_set(0, "S", "10", "", lvt_pal.mid, -1);
        lvt_card_set(1, "M", "14", "", lvt_pal.mid, -1);
        lvt_card_set(2, "L", "20", "", lvt_pal.mid, -1);
        lvt_card_set(3, "SYM", LV_SYMBOL_AUDIO, "", lvt_pal.dim, -1);
        break;
    case 4: {
        uint32_t tl = lv_strlen(mdemo_text);
        lv_point_t pt;
        char info[24];
        uint8_t ch = mdemo.edit_cursor < tl ? (uint8_t)mdemo_text[mdemo.edit_cursor] : 0;
        mdemo_num(a, tl);
        mdemo_num(b, mdemo.edit_cursor);
        lvt_card_set(0, "EDIT", "TXT", "", lvt_pal.primary, -1);
        lvt_card_set(1, "LEN", a, "", lvt_pal.mid, (int32_t)(tl * 1000u / MDEMO_TEXT_MAX));
        lvt_card_set(2, "CUR", b, "", lvt_pal.accent, (int32_t)(mdemo.edit_cursor * 1000u / MDEMO_TEXT_MAX));
        lvt_card_set(3, "MAX", "31", "", lvt_pal.dim, -1);
        if (mdemo.dirty)
            lv_label_set_text_static(mdemo.edit, mdemo_text);     /* the buffer changed in place */
        lv_label_get_letter_pos(mdemo.edit, mdemo.edit_cursor, &pt);
        lv_obj_set_pos(mdemo.cursor, lv_obj_get_x(mdemo.edit) + pt.x, lv_obj_get_y(mdemo.edit) + 17);
        lv_obj_set_hidden(mdemo.cursor, !((mdemo.frame / 8u) & 1u));
        if (ch >= 32u && ch < 127u)
            lv_snprintf(info, sizeof info, "char: '%c' (%03u)", ch, (unsigned)ch);
        else
            lv_snprintf(info, sizeof info, "char: (end)");
        lvt_text(mdemo.info, info);
        break;
    }
    case 5:
        mdemo_num(a, mdemo.modal_sel + 1u);
        lvt_card_set(0, "MDL", "3", "", lvt_pal.mid, -1);
        lvt_card_set(1, "SEL", a, "", lvt_pal.primary, (int32_t)(mdemo.modal_sel + 1u) * 333);
        lvt_card_set(2, "", "", "", lvt_pal.dim, -1);
        lvt_card_set(3, "STL", "MOCK", "", lvt_pal.accent, -1);
        mdemo_rows_sel(MDEMO_MODAL_N, mdemo.modal_sel, 0);
        break;
    default: {
        uint8_t k = MDEMO_TOAST_KIND[mdemo.val[0]];
        mdemo_num(a, mdemo.val[0]);
        lvt_card_set(0, "TST", a, "", lvt_pal.primary, (int32_t)mdemo.val[0] * 200);
        lvt_card_set(1, "KIND", mdemo.val[0] == 0 ? "CLR" : k == TOAST_STATUS ? "STS" : "ERR", "",
                     mdemo.val[0] == 0 ? lvt_pal.dim : k == TOAST_STATUS ? lvt_pal.primary : lvt_pal.rec, -1);
        lvt_card_set(2, "", "", "", lvt_pal.dim, -1);
        lvt_card_set(3, "", "", "", lvt_pal.dim, -1);
        mdemo_rows_sel(MDEMO_TOAST_N, mdemo.val[0], 1);
        break;
    }
    }
    mdemo.dirty = 0;
}

static void mdemo_status(void)
{
    if (fm1_ms - mdemo.tick_ms >= 50u) {    /* the frame count drives what moves */
        mdemo.tick_ms = fm1_ms;
        mdemo.frame++;
        if (mdemo.screen == 0 || mdemo.screen == 2 || mdemo.screen == 4)
            mdemo.dirty = 1;
    }
    if (mdemo.dirty)
        mdemo_show();
}

/* ------------------------------------------------------------- controls --- */
static void mdemo_toast_set(uint32_t idx)
{
    if (idx == 0) { toast_clear(); return; }
    if (MDEMO_TOAST_KIND[idx] == TOAST_STATUS)
        toast_status(MDEMO_TOAST_MSGS[idx]);
    else
        toast_error(MDEMO_TOAST_MSGS[idx]);
}

static int32_t mdemo_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static int mdemo_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        int32_t s = (int32_t)mdemo.screen + clicks;
        if (s < 0 || s >= (int32_t)MDEMO_SCREENS)
            return 0;                   /* past the ends: the shell goes to the next page */
        mdemo.screen = (uint8_t)s;
        mdemo.val[0] = mdemo.val[1] = mdemo.val[2] = mdemo.val[3] = 0;
        mdemo_build();
        return 1;
    }
    if (id >= ENC_K1 && id <= ENC_K4) {
        uint32_t ki = id - ENC_K1;
        if (mdemo.screen == 1 && ki == 0) {
            mdemo.val[0] = (uint8_t)mdemo_clamp((int32_t)mdemo.val[0] + clicks, 0, MDEMO_LIST_N - 1);
        } else if (mdemo.screen == 4 && ki == 0) {
            mdemo.edit_cursor = (uint8_t)mdemo_clamp((int32_t)mdemo.edit_cursor + clicks, 0,
                                                     (int32_t)lv_strlen(mdemo_text));
        } else if (mdemo.screen == 4 && ki == 1) {
            uint32_t tl = lv_strlen(mdemo_text);
            if (mdemo.edit_cursor < tl)
                mdemo_text[mdemo.edit_cursor] =
                    (char)mdemo_clamp((int32_t)mdemo_text[mdemo.edit_cursor] + clicks, 32, 126);
            else if (mdemo.edit_cursor == tl && tl < MDEMO_TEXT_MAX && clicks > 0) {
                mdemo_text[tl] = 'A';
                mdemo_text[tl + 1u] = 0;
            }
        } else if (mdemo.screen == 5 && ki == 0) {
            mdemo.modal_sel = (uint8_t)mdemo_clamp((int32_t)mdemo.modal_sel + clicks, 0, MDEMO_MODAL_N - 1);
            mdemo_modal_show();
        } else if (mdemo.screen == 6 && ki == 0) {
            mdemo.val[0] = (uint8_t)mdemo_clamp((int32_t)mdemo.val[0] + clicks, 0, MDEMO_TOAST_N - 1);
            mdemo_toast_set(mdemo.val[0]);
        } else {
            mdemo.val[ki] = (uint8_t)mdemo_clamp((int32_t)mdemo.val[ki] + clicks, 0, 99);
        }
        mdemo.dirty = 1;
        return 1;
    }
    return 0;
}

static int mdemo_key(uint32_t note, uint32_t vel)
{
    mdemo.last_note = (uint8_t)note;
    mdemo.last_vel = (uint8_t)vel;
    mdemo.dirty = 1;
    return 1;
}

static void mdemo_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    mdemo.screen = 0;
    mdemo.val[0] = mdemo.val[1] = mdemo.val[2] = mdemo.val[3] = 0;
    mdemo.frame = 0;
    mdemo.last_note = mdemo.last_vel = 0;
    mdemo.edit_cursor = 0;
    mdemo.modal_sel = 0;
    lvt_cards(body);
    mdemo.panel = lvt_panel(body);
    mdemo_build();
    mdemo_show();
}

static const modd_page_t PAGE_DEMO = {
    .name   = "DEMO",
    .title  = "DEMO",
    .view   = VIEW_CONN,
    .flags  = PAGEF_SYSTEM,
    .enter  = mdemo_enter,
    .status = mdemo_status,
    .knob   = mdemo_knob,
    .key    = mdemo_key,
    .help_text =
        "SELECT  Next screen\n"
        "K1      Select / cursor\n"
        "K2      Character (EDIT)\n"
        "K1-K4   Values 0-99\n"
        "Keys    Last note (CARDS)",
};

#endif /* CONFIG_PAGE_DEMO */
