// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd DEMO: 6-screen kitchen-sink UI showcase under VIEW_DEMO (FX hold).
 * SELECT switches screens, K1-K4 adjust per-screen values. */

#define MDEMO_SCREENS  6

static const char *MDEMO_NAMES[] = {
    "CARDS", "LIST", "GRAPH", "TEXT", "EDIT", "MODALS"
};

static char mdemo_text[32] = "Hello FM-1!";

static struct {
    uint8_t  screen;
    uint8_t  val[4];
    uint8_t  scroll;
    uint32_t frame;
    uint8_t  last_note;
    uint8_t  last_vel;
    uint8_t  edit_cursor;
    uint8_t  modal_sel;
} mdemo;

/* --- int-to-string helper (no libc) --- */

static void mdemo_fmt(char *buf, int16_t v)
{
    char tmp[8];
    uint32_t i = 0, j = 0;
    int16_t av = v < 0 ? -v : v;
    if (v < 0) buf[j++] = '-';
    do { tmp[i++] = (char)('0' + av % 10); av /= 10; } while (av && i < 7);
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
}

/* --- Screen 0: Cards --- */

static void mdemo_draw_cards(void)
{
    char fr[8], sc[4], v0[4], note[4];

    mdemo_fmt(fr, (int16_t)(mdemo.frame & 0x7FFF));
    sc[0] = '0' + mdemo.screen; sc[1] = '/';
    sc[2] = '0' + MDEMO_SCREENS - 1; sc[3] = 0;
    mdemo_fmt(v0, (int16_t)mdemo.val[0]);

    ui_card(0, "FRA", fr, "", T_THEME, (int32_t)(mdemo.frame % 1000), 0);
    ui_card(1, "SCR", sc, "", T_ACCENT, (int32_t)mdemo.screen * 200, 0);
    ui_card(2, "K1", v0, "", T_MID, (int32_t)mdemo.val[0] * 100, 0);

    mdemo_fmt(note, (int16_t)mdemo.last_note);
    ui_card(3, "NOTE", note, "",
            mdemo.last_vel ? T_THEME : T_DIM,
            mdemo.last_vel ? (int32_t)mdemo.last_vel * 8 : -1, 0);

    mt_panel_begin();

    cv_rect(8, 4, 24, 12, T_BG);     cv_text_fit(36, 6, TY_LB, "BG", T_DIM, T_BG, 60);
    cv_rect(8, 20, 24, 12, T_SURF);  cv_text_fit(36, 22, TY_LB, "SURF", T_DIM, T_BG, 60);
    cv_rect(8, 36, 24, 12, T_THEME); cv_text_fit(36, 38, TY_LB, "THEME", T_TEXT, T_BG, 60);
    cv_rect(8, 52, 24, 12, T_ACCENT);cv_text_fit(36, 54, TY_LB, "ACCENT", T_TEXT, T_BG, 60);

    cv_rect(124, 4, 24, 12, T_TEXT);  cv_text_fit(152, 6, TY_LB, "TEXT", T_DIM, T_BG, 60);
    cv_rect(124, 20, 24, 12, T_MID);  cv_text_fit(152, 22, TY_LB, "MID", T_DIM, T_BG, 60);
    cv_rect(124, 36, 24, 12, T_DIM);  cv_text_fit(152, 38, TY_LB, "DIM", T_TEXT, T_BG, 60);
    cv_rect(124, 52, 24, 12, T_LINE); cv_text_fit(152, 54, TY_LB, "LINE", T_TEXT, T_BG, 60);

    cv_rect(8, 70, 24, 12, T_RAISE);  cv_text_fit(36, 72, TY_LB, "RAISE", T_TEXT, T_BG, 60);
    cv_rect(124, 70, 24, 12, T_INK);  cv_text_fit(152, 72, TY_LB, "INK", T_DIM, T_BG, 60);

    mt_panel_end();
}

/* --- Screen 1: List --- */

#define MDEMO_LIST_N  16
static const char *MDEMO_LIST[] = {
    "Oscillator", "Filter", "Envelope", "LFO",
    "Reverb", "Delay", "Chorus", "Distortion",
    "Sequencer", "Arpeggiator", "Mixer", "Compressor",
    "Phaser", "Flanger", "EQ", "Limiter"
};

static void mdemo_draw_list(void)
{
    uint32_t i, visible = UI_PANEL_H / UI_ROW_H;
    char n[4], s[4];

    mdemo_fmt(n, MDEMO_LIST_N);
    mdemo_fmt(s, (int16_t)mdemo.val[0]);
    ui_card(0, "ITEMS", n, "", T_MID, -1, 0);
    ui_card(1, "SEL", s, "", T_THEME, (int32_t)mdemo.val[0] * 1000 / MDEMO_LIST_N, 0);
    ui_card(2, "", "", "", T_DIM, -1, 0);
    ui_card(3, "", "", "", T_DIM, -1, 0);

    mt_panel_begin();
    for (i = 0; i < visible && (i + mdemo.scroll) < MDEMO_LIST_N; i++) {
        uint32_t idx = i + mdemo.scroll;
        int32_t y = (int32_t)i * UI_ROW_H;
        uint8_t sel = (idx == mdemo.val[0]);
        uint16_t bg = sel ? T_SURF : T_BG;
        uint16_t fg = sel ? T_THEME : T_TEXT;

        if (sel)
            cv_rrect(2, y, UI_W - 4, UI_ROW_H, 3, T_SURF, T_BG);

        {
            char num[4];
            num[0] = '0' + (char)(idx / 10);
            num[1] = '0' + (char)(idx % 10);
            num[2] = 0;
            if (idx < 10) num[0] = ' ';
            cv_text_fit(8, UI_TEXT_Y(y, UI_ROW_H), TY_LB, num, T_DIM, bg, 16);
        }
        cv_text_fit(28, UI_TEXT_Y(y, UI_ROW_H), TY_LB, MDEMO_LIST[idx], fg, bg, 160);
        {
            int32_t bw = (int32_t)idx * 40 / MDEMO_LIST_N;
            cv_rrect(200, y + 4, bw < 2 ? 2 : bw, 8, 2, fg, bg);
        }
    }
    mt_panel_end();
}

/* --- Screen 2: Graph/bars --- */

static void mdemo_draw_graph(void)
{
    uint32_t i;
    uint32_t bar_w = 12, gap = 3, n_bars = 16;
    int32_t base_x = (int32_t)(UI_W - n_bars * (bar_w + gap)) / 2;

    ui_card(0, "BARS", "16", "", T_MID, -1, 0);
    ui_card(1, "FRAME", "", "", T_DIM, (int32_t)(mdemo.frame % 1000), 0);
    ui_card(2, "ANIM", "ON", "", T_THEME, -1, 0);
    ui_card(3, "STYLE", "BAR", "", T_DIM, -1, 0);

    mt_panel_begin();
    for (i = 0; i < n_bars; i++) {
        uint32_t seed = (mdemo.frame / 3 + i * 37) & 0xFF;
        int32_t bar_h = (int32_t)((seed * (UI_PANEL_H - 8)) >> 8);
        if (bar_h < 4) bar_h = 4;
        int32_t x = base_x + (int32_t)i * (int32_t)(bar_w + gap);
        int32_t y = (int32_t)UI_PANEL_H - bar_h - 2;
        uint16_t c = (i == (mdemo.frame / 4) % n_bars) ? T_ACCENT : T_THEME;
        cv_rrect(x, y, (int32_t)bar_w, bar_h, 2, c, T_BG);
    }
    cv_rect(base_x - 2, (int32_t)UI_PANEL_H - 2,
            (int32_t)(n_bars * (bar_w + gap)) + 4, 1, T_LINE);
    mt_panel_end();
}

/* --- Screen 3: Text/font --- */

static void mdemo_draw_text(void)
{
    ui_card(0, "FONT", "S", "", T_MID, -1, 0);
    ui_card(1, "FONT", "M", "", T_MID, -1, 0);
    ui_card(2, "FONT", "L", "", T_MID, -1, 0);
    ui_card(3, "DEMO", "TXT", "", T_DIM, -1, 0);

    mt_panel_begin();

    cv_text_fit(8, 4, TY_LB, "LB: The quick brown fox", T_TEXT, T_BG, UI_W - 16);
    cv_text_fit(8, 20, TY_TX, "TX: ABCDEFGH", T_TEXT, T_BG, UI_W - 16);
    cv_text_fit(8, 42, TY_VL, "VL: 12345", T_TEXT, T_BG, UI_W - 16);

    cv_text_c(UI_W / 2, ty_y(72, TY_LB), TY_LB, "CENTERED TEXT", T_THEME, T_BG);
    cv_text_r(UI_W - 8, ty_y(88, TY_LB), TY_LB, "RIGHT ALIGNED", T_ACCENT, T_BG);
    cv_text_fit(8, 88, TY_LB, "LEFT", T_MID, T_BG, 60);

    cv_rrect(8, 104, 60, 14, 4, T_SURF, T_BG);
    cv_text_c(38, ty_y(107, TY_LB), TY_LB, "RRECT4", T_TEXT, T_SURF);

    cv_rrect(76, 104, 60, 14, 8, T_THEME, T_BG);
    cv_text_c(106, ty_y(107, TY_LB), TY_LB, "RRECT8", T_INK, T_THEME);

    cv_rrect(144, 104, 60, 14, 2, T_ACCENT, T_BG);
    cv_text_c(174, ty_y(107, TY_LB), TY_LB, "RRECT2", T_INK, T_ACCENT);

    mt_panel_end();
}

/* --- Screen 4: Inline text editor (no modal keyboard) --- */

static void mdemo_draw_edit(void)
{
    uint8_t tl = 0;
    char ln[4], cur[4];
    while (mdemo_text[tl]) tl++;

    mdemo_fmt(ln, (int16_t)tl);
    mdemo_fmt(cur, (int16_t)mdemo.edit_cursor);
    ui_card(0, "EDIT", "TXT", "", T_THEME, -1, 0);
    ui_card(1, "LEN", ln, "", T_MID, (int32_t)tl * 1000 / 31, 0);
    ui_card(2, "CUR", cur, "", T_ACCENT, (int32_t)mdemo.edit_cursor * 1000 / 31, 0);
    ui_card(3, "MAX", "31", "", T_DIM, -1, 0);

    mt_panel_begin();

    cv_text_fit(8, 4, TY_LB, "INLINE TEXT EDITOR", T_DIM, T_BG, UI_W - 16);
    cv_text_fit(8, 16, TY_LB, "K1: cursor  K2: character", T_DIM, T_BG, UI_W - 16);

    cv_rrect(4, 34, UI_W - 8, 28, 4, T_SURF, T_BG);
    cv_text_fit(10, 42, TY_TX, mdemo_text, T_TEXT, T_SURF, UI_W - 20);

    /* cursor underline */
    if (mdemo.edit_cursor < tl || tl < 31) {
        int32_t cx = 10 + (int32_t)mdemo.edit_cursor * 8;
        if (cx > UI_W - 18) cx = UI_W - 18;
        if ((mdemo.frame / 8) & 1)
            cv_rect(cx, 56, 8, 2, T_ACCENT);
    }

    /* character code display */
    {
        char info[24] = "char: ";
        uint8_t ch = (mdemo.edit_cursor < tl)
                     ? (uint8_t)mdemo_text[mdemo.edit_cursor] : 0;
        if (ch >= 32 && ch < 127) {
            info[6] = '\''; info[7] = (char)ch; info[8] = '\'';
            info[9] = ' '; info[10] = '(';
            info[11] = '0' + (char)(ch / 100);
            info[12] = '0' + (char)((ch / 10) % 10);
            info[13] = '0' + (char)(ch % 10);
            info[14] = ')'; info[15] = 0;
        } else {
            info[6] = '('; info[7] = 'e'; info[8] = 'n';
            info[9] = 'd'; info[10] = ')'; info[11] = 0;
        }
        cv_text_fit(8, 72, TY_LB, info, T_MID, T_BG, UI_W - 16);
    }

    cv_text_fit(8, 92, TY_LB, "Turn K2 to change char value", T_DIM, T_BG, UI_W - 16);

    mt_panel_end();
}

/* --- Screen 5: Mock modals (drawn inline, no real modal system) --- */

#define MDEMO_MODAL_N  3
static const char *MDEMO_MODAL_NAMES[] = {
    "YES / NO", "OK Info", "Text Input"
};

static void mdemo_draw_modals(void)
{
    uint32_t i;
    char s[4];

    mdemo_fmt(s, (int16_t)(mdemo.modal_sel + 1));
    ui_card(0, "MODALS", "3", "", T_MID, -1, 0);
    ui_card(1, "SEL", s, "", T_THEME, (int32_t)(mdemo.modal_sel + 1) * 333, 0);
    ui_card(2, "", "", "", T_DIM, -1, 0);
    ui_card(3, "STYLE", "MOCK", "", T_ACCENT, -1, 0);

    mt_panel_begin();

    cv_text_fit(8, 2, TY_LB, "MODAL TYPES (K1 to select)", T_DIM, T_BG, UI_W - 16);

    for (i = 0; i < MDEMO_MODAL_N; i++) {
        int32_t y = 18 + (int32_t)i * UI_ROW_H;
        uint8_t sel = ((uint8_t)i == mdemo.modal_sel);
        uint16_t bg = sel ? T_SURF : T_BG;
        uint16_t fg = sel ? T_THEME : T_TEXT;

        if (sel)
            cv_rrect(2, y, UI_W - 4, UI_ROW_H, 3, T_SURF, T_BG);

        {
            char num[4];
            num[0] = '0' + (char)(i + 1);
            num[1] = '.';
            num[2] = 0;
            cv_text_fit(8, UI_TEXT_Y(y, UI_ROW_H), TY_LB, num, T_DIM, bg, 16);
        }
        cv_text_fit(28, UI_TEXT_Y(y, UI_ROW_H), TY_LB, MDEMO_MODAL_NAMES[i], fg, bg, 160);
    }

    /* draw a mock modal overlay for the selected type */
    {
        int32_t mx = 20, my = 72, mw = UI_W - 40, mh = 46;
        cv_rrect(mx, my, mw, mh, 6, T_SURF, T_BG);
        cv_rect(mx, my, mw, 14, T_THEME);
        cv_text_c(UI_W / 2, ty_y(my + 2, TY_LB), TY_LB,
                  MDEMO_MODAL_NAMES[mdemo.modal_sel], T_INK, T_THEME);

        switch (mdemo.modal_sel) {
        case 0:
            cv_text_c(UI_W / 2, ty_y(my + 20, TY_LB), TY_LB,
                      "Are you sure?", T_TEXT, T_SURF);
            cv_rrect(mx + 10, my + 32, 60, 12, 3, T_ACCENT, T_SURF);
            cv_text_c(mx + 40, ty_y(my + 34, TY_LB), TY_LB, "YES", T_INK, T_ACCENT);
            cv_rrect(mw - 50, my + 32, 60, 12, 3, T_DIM, T_SURF);
            cv_text_c(mw - 20, ty_y(my + 34, TY_LB), TY_LB, "NO", T_TEXT, T_DIM);
            break;
        case 1:
            cv_text_c(UI_W / 2, ty_y(my + 20, TY_LB), TY_LB,
                      "Operation complete.", T_TEXT, T_SURF);
            cv_rrect(UI_W / 2 - 25, my + 32, 50, 12, 3, T_THEME, T_SURF);
            cv_text_c(UI_W / 2, ty_y(my + 34, TY_LB), TY_LB, "OK", T_INK, T_THEME);
            break;
        case 2:
            cv_rrect(mx + 10, my + 18, mw - 20, 14, 3, T_BG, T_SURF);
            cv_text_fit(mx + 14, my + 21, TY_LB, "type here...", T_DIM, T_BG, mw - 30);
            cv_rect(mx + 70, my + 30, 6, 1, T_ACCENT);
            break;
        }
    }

    mt_panel_end();
}

/* --- Main draw dispatch --- */

static void mdemo_draw(void)
{
    switch (mdemo.screen) {
    case 0: mdemo_draw_cards();  break;
    case 1: mdemo_draw_list();   break;
    case 2: mdemo_draw_graph();  break;
    case 3: mdemo_draw_text();   break;
    case 4: mdemo_draw_edit();   break;
    case 5: mdemo_draw_modals(); break;
    default: mdemo_draw_cards(); break;
    }

    mdemo.frame++;

    ui_footer("DEMO", MDEMO_NAMES[mdemo.screen], "DEMO");
}

static void mdemo_enter(void)
{
    mdemo.screen = 0;
    mdemo.val[0] = mdemo.val[1] = mdemo.val[2] = mdemo.val[3] = 0;
    mdemo.scroll = 0;
    mdemo.frame = 0;
    mdemo.last_note = 0;
    mdemo.last_vel = 0;
    mdemo.edit_cursor = 0;
    mdemo.modal_sel = 0;
}

static int mdemo_knob(uint32_t id, int32_t clicks)
{
    if (id >= ENC_K1 && id <= ENC_K4) {
        uint32_t ki = id - ENC_K1;

        if (mdemo.screen == 1 && ki == 0) {
            /* list selection */
            uint32_t visible = UI_PANEL_H / UI_ROW_H;
            int32_t r = (int32_t)mdemo.val[0] + clicks;
            if (r < 0) r = 0;
            if (r >= MDEMO_LIST_N) r = MDEMO_LIST_N - 1;
            mdemo.val[0] = (uint8_t)r;
            if (mdemo.val[0] < mdemo.scroll)
                mdemo.scroll = mdemo.val[0];
            if (mdemo.val[0] >= mdemo.scroll + visible)
                mdemo.scroll = (uint8_t)(mdemo.val[0] - visible + 1u);
        } else if (mdemo.screen == 4 && ki == 0) {
            /* edit: move cursor */
            uint8_t tl = 0;
            while (mdemo_text[tl]) tl++;
            int32_t c = (int32_t)mdemo.edit_cursor + clicks;
            if (c < 0) c = 0;
            if (c > (int32_t)tl) c = (int32_t)tl;
            mdemo.edit_cursor = (uint8_t)c;
        } else if (mdemo.screen == 4 && ki == 1) {
            /* edit: change character at cursor */
            uint8_t tl = 0;
            while (mdemo_text[tl]) tl++;
            if (mdemo.edit_cursor < tl) {
                int32_t ch = (int32_t)mdemo_text[mdemo.edit_cursor] + clicks;
                if (ch < 32) ch = 32;
                if (ch > 126) ch = 126;
                mdemo_text[mdemo.edit_cursor] = (char)ch;
            } else if (mdemo.edit_cursor == tl && tl < 31 && clicks > 0) {
                mdemo_text[tl] = 'A';
                mdemo_text[tl + 1u] = 0;
            }
        } else if (mdemo.screen == 5 && ki == 0) {
            /* modals: select type */
            int32_t r = (int32_t)mdemo.modal_sel + clicks;
            if (r < 0) r = 0;
            if (r >= MDEMO_MODAL_N) r = MDEMO_MODAL_N - 1;
            mdemo.modal_sel = (uint8_t)r;
        } else {
            int32_t v = (int32_t)mdemo.val[ki] + clicks;
            if (v < 0) v = 0;
            if (v > 99) v = 99;
            mdemo.val[ki] = (uint8_t)v;
        }
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static int mdemo_key(uint32_t note, uint32_t vel)
{
    mdemo.last_note = (uint8_t)note;
    mdemo.last_vel = (uint8_t)vel;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_DEMO = {
    .name   = "DEMO",
    .title  = "DEMO",
    .view   = VIEW_CONN,
    .flags  = PAGEF_SYSTEM,
    .enter  = mdemo_enter,
    .draw   = mdemo_draw,
    .knob   = mdemo_knob,
    .key    = mdemo_key,
};
