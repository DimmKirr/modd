// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd SETTINGS: tabbed menu driven by modd_sys_menu adapter.
 * HOME hold opens VIEW_SETTINGS (single page, tabs only).
 * SELECT switches tabs, ALGO scrolls rows, PRESET changes value.
 * OCT+ confirms/toggles, OCT- closes settings.
 * INSTR (CONFIG_DOCK): modd's engines, the installed dock modules and the .dock files on a
 * USB disk; OCT+ starts one, installing it from the disk first (dock.c, dock_disk.c). */

#define MSET_ROW_H    20
#define MSET_X_PAD    8
#define MSET_TAB_H    16
#define MSET_CANVAS_Y MT_Y_CARD
#define MSET_CANVAS_H ((uint32_t)(MT_Y_FOOT - MT_Y_CARD))
#define MSET_LIST_Y   (MSET_TAB_H + 4)
#define MSET_LIST_H   (MSET_CANVAS_H - (uint32_t)MSET_LIST_Y)
#define MSET_VISIBLE  (MSET_LIST_H / MSET_ROW_H)

static struct {
    uint8_t tab;
    uint8_t row;
    uint8_t scroll;
} mset;

static uint32_t mset_tab_count(uint8_t tab)
{
    uint32_t n = 0, i;
    for (i = 0; i < SYS_MENU_COUNT; i++)
        if (SYS_MENU[i].tab == tab) n++;
    return n;
}

static const sys_menu_item_t *mset_tab_item(uint8_t tab, uint32_t idx)
{
    uint32_t n = 0, i;
    for (i = 0; i < SYS_MENU_COUNT; i++) {
        if (SYS_MENU[i].tab == tab) {
            if (n == idx) return &SYS_MENU[i];
            n++;
        }
    }
    return 0;
}

static int mset_tab_visible(uint8_t tab)
{
    if (mset_tab_count(tab) > 0) return 1;
    if (tab == SMTAB_DISPLAY) return 1;
#ifdef CONFIG_DOCK
    if (tab == SMTAB_INSTR) return 1;
#endif
#ifdef CONFIG_SYS_MENU_NETWORK
    if (tab == SMTAB_NETWORK) return 1;
#endif
    return 0;
}

#ifdef CONFIG_DOCK
/* ---- INSTR: the list widget (list.c) over LIST_INSTR (dock/dock_list.c) ---- */
static ui_list_t minstr_list;
#define MSET_CANVAS_REAL_H 124                  /* the renderer caps a canvas at 240 x 124 (gfx.c CV_MAX) */

static void minstr_fmt_pct(char *b, uint32_t pct)
{
    uint32_t i = 0;
    if (pct >= 100u) b[i++] = '1';
    if (pct >= 10u) b[i++] = (char)('0' + (pct / 10u) % 10u);
    b[i++] = (char)('0' + pct % 10u);
    b[i++] = '%';
    b[i] = 0;
}

/* dock_install's progress, drawn at once: the handler blocks the UI task until it is done */
static void minstr_progress(const char *what, uint32_t pct)
{
    char b[8];
    minstr_fmt_pct(b, pct);
    gfx_frame(1);
    cv_begin(MT_W, MSET_ROW_H, T_BG);
    cv_text_fit(MSET_X_PAD, 3, TY_LB, what, T_THEME, T_BG, 120);
    cv_text_r(MT_W - MSET_X_PAD, 3, TY_LB, b, T_DIM, T_BG);
    cv_blit(0, (uint32_t)(MT_Y_FOOT - MSET_ROW_H));
    gfx_frame(0);
}

static void minstr_enter(void)
{
    ui_list_init(&minstr_list, &LIST_INSTR, MSET_ROW_H);
    minstr_list.cursor = dock_cur == DOCK_NONE ? 0 : dock_cur + 1u;
    dock_progress = minstr_progress;
    ui_list_open(&minstr_list);
}

static void minstr_leave(void)
{
    dock_progress = 0;
    ui_list_close(&minstr_list);
}

static void mset_draw_instr(int32_t y0)
{
    ui_list_draw(&minstr_list, y0, MSET_CANVAS_REAL_H - y0);
}

static void settings_status(void)
{
    if (mset.tab == SMTAB_INSTR)
        ui_list_poll(&minstr_list);
}
#endif /* CONFIG_DOCK */

static void mset_next_tab(int32_t dir)
{
    int32_t t = (int32_t)mset.tab;
    uint32_t tries = SMTAB_COUNT;
    do {
        t += dir > 0 ? 1 : -1;
        if (t < 0) t = SMTAB_COUNT - 1;
        if (t >= (int32_t)SMTAB_COUNT) t = 0;
    } while (!mset_tab_visible((uint8_t)t) && --tries);
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR && t != (int32_t)SMTAB_INSTR) minstr_leave();
#endif
    mset.tab = (uint8_t)t;
    mset.row = 0;
    mset.scroll = 0;
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR) minstr_enter();
#endif
}

#if defined(CONFIG_SYS_MENU_NETWORK) && (defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE))
static void mset_fmt_ip(char *buf, uint32_t ip)
{
    uint32_t i, v, d;
    char *p = buf;
    for (i = 0; i < 4u; i++) {
        v = (ip >> (i * 8u)) & 0xFFu;
        if (v >= 100u) { *p++ = '0' + (char)(v / 100u); }
        if (v >= 10u)  { d = (v / 10u) % 10u; *p++ = '0' + (char)d; }
        *p++ = '0' + (char)(v % 10u);
        if (i < 3u) *p++ = '.';
    }
    *p = 0;
}

static void mset_draw_network(int32_t y0)
{
    char buf[20];
    uint32_t ip;
    int32_t y = y0;
    wifi_state_t st = (wifi_state_t)wifi_get_state();
    static const char *const WIFI_ST[] = {
        "OFF", "IDLE", "SCAN", "AUTH", "ASSOC", "WPA", "OK"
    };
    const char *stname = (uint32_t)st < 7u ? WIFI_ST[(uint32_t)st] : "?";

    cv_text_fit(MSET_X_PAD, y + 3, TY_LB, "STATUS", T_DIM, T_BG, 100);
    cv_text_r(MSET_CANVAS_H > 0 ? (int32_t)MT_W - MSET_X_PAD : 0,
              y + 3, TY_LB, stname,
              st == WIFI_STATE_CONNECTED ? T_THEME : T_DIM, T_BG);
    y += MSET_ROW_H;

    ip = wifi_get_ip();
    if (ip) { mset_fmt_ip(buf, ip); } else { buf[0] = '-'; buf[1] = 0; }
    cv_text_fit(MSET_X_PAD, y + 3, TY_LB, "IP", T_DIM, T_BG, 100);
    cv_text_r((int32_t)MT_W - MSET_X_PAD, y + 3, TY_LB, buf, T_DIM, T_BG);
    y += MSET_ROW_H;

    ip = wifi_get_gw();
    if (ip) { mset_fmt_ip(buf, ip); } else { buf[0] = '-'; buf[1] = 0; }
    cv_text_fit(MSET_X_PAD, y + 3, TY_LB, "GATEWAY", T_DIM, T_BG, 100);
    cv_text_r((int32_t)MT_W - MSET_X_PAD, y + 3, TY_LB, buf, T_DIM, T_BG);
    y += MSET_ROW_H;

    ip = wifi_get_dns();
    if (ip) { mset_fmt_ip(buf, ip); } else { buf[0] = '-'; buf[1] = 0; }
    cv_text_fit(MSET_X_PAD, y + 3, TY_LB, "DNS", T_DIM, T_BG, 100);
    cv_text_r((int32_t)MT_W - MSET_X_PAD, y + 3, TY_LB, buf, T_DIM, T_BG);
}
#endif

static void mset_draw_display(int32_t y0)
{
    uint32_t i;
    char pos[8];
    i = 0;
    pos[i++] = (char)('0' + (ux.pal + 1u) / 10u);
    if (pos[0] == '0') i = 0;
    pos[i++] = (char)('0' + (ux.pal + 1u) % 10u);
    pos[i++] = '/';
    if (UI_NPALETTES >= 10u) pos[i++] = (char)('0' + UI_NPALETTES / 10u);
    pos[i++] = (char)('0' + UI_NPALETTES % 10u);
    pos[i] = 0;

    cv_text_c(MT_W / 2, ty_y(y0 + 20, TY_VL), TY_VL,
              UI_PALETTES[ux.pal].name, T_THEME, T_BG);
    cv_text_c(MT_W / 2, ty_y(y0 + 52, TY_LB), TY_LB, pos, T_DIM, T_BG);
    for (i = 0; i < 4u; i++)
        cv_rrect(72 + 26 * (int32_t)i, y0 + 70, 20, 10, 3,
                 UI_PALETTES[ux.pal].trk[i], T_BG);
}

static void settings_draw(void)
{
    uint32_t t, n, r;

    cv_begin(MT_W, MSET_CANVAS_H, T_BG);

    /* tab bar: pinned at top of canvas (right below header) */
    {
        int32_t x = MSET_X_PAD;
        for (t = 0; t < (uint32_t)SMTAB_COUNT; t++) {
            if (!mset_tab_visible((uint8_t)t))
                continue;
            uint8_t sel = (t == mset.tab);
            uint16_t fg = sel ? T_THEME : T_DIM;
            uint16_t bg = sel ? T_SURF : T_BG;

            if (sel)
                cv_rrect(x - 2, 0, 42, MSET_TAB_H, 3, T_SURF, T_BG);

            cv_text_fit(x, 3, TY_LB, SMTAB_NAME[t], fg, bg, 38);
            x += 44;
        }
        cv_rect(0, MSET_TAB_H, MT_W, 1, T_LINE);
    }

    /* item list for current tab */
    n = mset_tab_count(mset.tab);
    for (r = 0; r < MSET_VISIBLE && (mset.scroll + r) < n; r++) {
        uint32_t idx = mset.scroll + r;
        const sys_menu_item_t *item = mset_tab_item(mset.tab, idx);
        if (!item) break;
        int32_t y = MSET_LIST_Y + (int32_t)r * MSET_ROW_H;
        uint8_t sel = (idx == mset.row);
        uint16_t bg = sel ? T_SURF : T_BG;
        uint16_t fg = sel ? T_THEME : T_TEXT;

        if (sel)
            cv_rrect(2, y, MT_W - 4, MSET_ROW_H - 2, 4, T_SURF, T_BG);

        cv_text_fit(MSET_X_PAD, y + 3, TY_LB, item->name, T_DIM, bg, 120);
        cv_text_r(MT_W - MSET_X_PAD, y + 3, TY_LB,
                  item->values[item->get()], fg, bg);
    }

    if (mset.tab == SMTAB_DISPLAY) {
        mset_draw_display(MSET_LIST_Y);
    } else
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR) {
        mset_draw_instr(MSET_LIST_Y);
    } else
#endif
#if defined(CONFIG_SYS_MENU_NETWORK) && (defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE))
    if (mset.tab == SMTAB_NETWORK) {
        int32_t info_y = MSET_LIST_Y + (int32_t)n * MSET_ROW_H + 6;
        mset_draw_network(info_y);
    } else
#endif
    if (n == 0)
        cv_text_c(MT_W / 2, ty_y(MSET_LIST_Y + 30, TY_LB), TY_LB,
                  "(no items)", T_DIM, T_BG);

    cv_blit(0, MSET_CANVAS_Y);
    ui_footer("settings", SMTAB_NAME[mset.tab], "MENU");
}

static void mset_step_value(int32_t dir)
{
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR) {
        ui_list_act(&minstr_list);
        return;
    }
#endif
    if (mset.tab == SMTAB_DISPLAY) {
        uint32_t n = UI_NPALETTES;
        uint32_t next = (uint32_t)((int32_t)ux.pal
            + (dir > 0 ? 1 : -1) + (int32_t)n) % n;
        palette_set(next);
        gfx_forget();
#ifdef CONFIG_SYS_MENU
        sys_settings.ui_pal = (uint8_t)palette_to_stored(ux.pal);
        sys_settings_dirty = 1;
#endif
        nav_dirty = 1;
        return;
    }
    {
        const sys_menu_item_t *item = mset_tab_item(mset.tab, mset.row);
        if (item && item->n_values > 0) {
            uint8_t v = item->get();
            if (dir > 0)
                v = (uint8_t)((v + 1u) % item->n_values);
            else if (v > 0)
                v--;
            else
                v = (uint8_t)(item->n_values - 1u);
            item->set(v);
            nav_dirty = 1;
        }
    }
}

static int settings_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        mset_next_tab(clicks);
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_ALGO) {
        uint32_t n = mset_tab_count(mset.tab);
#ifdef CONFIG_DOCK
        if (mset.tab == SMTAB_INSTR) {
            ui_list_knob(&minstr_list, clicks);
            return 1;
        }
#endif
        int32_t r = (int32_t)mset.row + clicks;
        if (r < 0) r = 0;
        if (n > 0 && (uint32_t)r >= n) r = (int32_t)(n - 1);
        mset.row = (uint8_t)r;
        if (mset.row < mset.scroll)
            mset.scroll = mset.row;
        if (mset.row >= mset.scroll + MSET_VISIBLE)
            mset.scroll = (uint8_t)(mset.row - MSET_VISIBLE + 1u);
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_K1) {
        mset_step_value(clicks);
        return 1;
    }
    return 0;
}

static int settings_button(uint32_t id, uint32_t down)
{
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR && (id == BTN_OCTUP || id == BTN_OCTDN)) {
        if (ui_list_button(&minstr_list, id, down))
            return 1;
        if (id == BTN_OCTDN && !down) {         /* short-: nothing to go up to, settings close */
            nav_open_view(VIEW_HOME);
            return 1;
        }
        return 0;
    }
#endif
    if (!down) return 0;
    if (id == BTN_OCTUP) {
        mset_step_value(1);
        return 1;
    }
    if (id == BTN_OCTDN) {
        nav_open_view(VIEW_HOME);
        return 1;
    }
    return 0;
}

static void settings_enter(void)
{
    mset.row = 0;
    mset.scroll = 0;
    {
        uint32_t t;
        for (t = 0; t < (uint32_t)SMTAB_COUNT; t++) {
            if (mset_tab_visible((uint8_t)t)) {
                mset.tab = (uint8_t)t;
                break;
            }
        }
    }
}

#ifdef CONFIG_DOCK
static void settings_leave(void)
{
    if (mset.tab == SMTAB_INSTR) minstr_leave();
}
#endif

static const modd_page_t PAGE_SETTINGS = {
    .name   = "MENU",
    .title  = "Settings",
    .view   = VIEW_SETTINGS,
    .flags  = PAGEF_SYSTEM,
    .enter  = settings_enter,
#ifdef CONFIG_DOCK
    .leave  = settings_leave,
#endif
    .draw   = settings_draw,
#ifdef CONFIG_DOCK
    .status = settings_status,
#endif
    .knob   = settings_knob,
    .button = settings_button,
};
