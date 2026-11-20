// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd SETTINGS: tabbed menu driven by modd_sys_menu adapter.
 * HOME hold opens VIEW_SETTINGS (single page, tabs only).
 * SELECT switches tabs, ALGO scrolls rows, OCT+/- steps value.
 * INSTR (CONFIG_DOCK): modd's engines, the installed dock modules and the .dock files on a
 * USB disk; OCT+ starts one, installing it from the disk first (dock.c, dock_disk.c). */

#define MSET_CANVAS_Y UI_Y_CARD
#define MSET_CANVAS_H ((uint32_t)(UI_Y_FOOT - UI_Y_CARD))
#define MSET_LIST_Y   (UI_TAB_H + UI_RULE_H + UI_GAP)
#define MSET_LIST_H   (MSET_CANVAS_H - (uint32_t)MSET_LIST_Y)
#define MSET_VISIBLE  (MSET_LIST_H / UI_ROW_H)

static struct {
    uint8_t tab;
    uint8_t row;
    uint8_t scroll;
} mset;

#ifdef CONFIG_SYS_MENU_NETWORK
/* net_data_t, net_read(), net_link_name(): ui/page_data.c (shared with the REST API) */

/* NET tab: redraw when anything it shows changes. The rows are LINK, IP,
 * GATEWAY and DNS, and they do not all change at the same moment: a link
 * attaches before it has an address, and goes before its DNS is dropped. */
static void mset_network_status(void)
{
    static uint32_t prev[4];
    static const char *prev_link;
    uint32_t cur[4];
    const char *link = net_link_name();
    uint32_t i, changed = (link != prev_link);

    cur[0] = (uint32_t)wifi_get_state();
    cur[1] = net_get_ip();
    cur[2] = net_get_gw();
    cur[3] = net_get_dns();
    for (i = 0; i < 4u; i++) {
        if (cur[i] != prev[i]) changed = 1;
        prev[i] = cur[i];
    }
    prev_link = link;
    if (changed)
        nav_dirty = 1;
}
#endif

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
    cv_begin(UI_W, UI_ROW_H, T_BG);
    cv_text_fit(UI_INSET, UI_TEXT_Y(0, UI_ROW_H), TY_LB, what, T_THEME, T_BG, 120);
    cv_text_r(UI_W - UI_INSET, UI_TEXT_Y(0, UI_ROW_H), TY_LB, b, T_DIM, T_BG);
    cv_blit(0, (uint32_t)(UI_Y_FOOT - UI_ROW_H));
    gfx_frame(0);
}

static void minstr_enter(void)
{
    ui_list_init(&minstr_list, &LIST_INSTR, UI_ROW_H);
    minstr_list.enc = ENC_ALGO;
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
#ifdef CONFIG_SYS_MENU_NETWORK
    if (mset.tab == SMTAB_NETWORK)
        mset_network_status();
#endif
}
#else
static void settings_status(void)
{
#ifdef CONFIG_SYS_MENU_NETWORK
    if (mset.tab == SMTAB_NETWORK)
        mset_network_status();
#endif
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

#ifdef CONFIG_SYS_MENU_NETWORK
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
    int32_t y = y0;
    net_data_t d;
    net_read(&d);

    ui_row(y, "LINK", d.link, d.connected ? T_THEME : T_DIM, 0);
    y += UI_ROW_H;

    if (d.ip) { mset_fmt_ip(buf, d.ip); } else { buf[0] = '-'; buf[1] = 0; }
    ui_row(y, "IP", buf, T_DIM, 0);
    y += UI_ROW_H;

    if (d.gw) { mset_fmt_ip(buf, d.gw); } else { buf[0] = '-'; buf[1] = 0; }
    ui_row(y, "GATEWAY", buf, T_DIM, 0);
    y += UI_ROW_H;

    if (d.dns) { mset_fmt_ip(buf, d.dns); } else { buf[0] = '-'; buf[1] = 0; }
    ui_row(y, "DNS", buf, T_DIM, 0);
}
#endif

/* DISPLAY tab: THEME row (virtual, index 0) + regular sys_menu items after it.
 * THEME shows 4 colour swatches inline between the label and the palette name. */
#define MSET_DISPLAY_THEME 0
#define MSET_DISPLAY_EXTRA 1

static uint32_t mset_display_count(void)
{
    return MSET_DISPLAY_EXTRA + mset_tab_count(SMTAB_DISPLAY);
}

static void mset_draw_theme_row(int32_t y, uint8_t selected)
{
    uint16_t bg = selected ? T_SURF : T_BG;
    if (selected)
        cv_rrect(2, y, UI_W - 4, UI_ROW_H - 2, 4, T_SURF, T_BG);
    cv_text_fit(UI_INSET, UI_TEXT_Y(y, UI_ROW_H), TY_LB, "THEME",
                T_DIM, bg, 120);
    /* 4 colour swatches, 8x8 with 3 px gap, right of centre */
    {
        uint32_t sw = 8, gap = 3, k;
        int32_t bx = (int32_t)(UI_W / 2 - (sw * 4 + gap * 3) / 2);
        int32_t by = y + ((int32_t)UI_ROW_H - (int32_t)sw) / 2;
        for (k = 0; k < 4u; k++)
            cv_rrect(bx + (int32_t)(k * (sw + gap)), by,
                     (int32_t)sw, (int32_t)sw, 2,
                     UI_PALETTES[ux.pal].trk[k], bg);
    }
    cv_text_r(UI_W - UI_INSET, UI_TEXT_Y(y, UI_ROW_H), TY_LB,
              UI_PALETTES[ux.pal].name,
              selected ? T_THEME : T_TEXT, bg);
}

/* two-pass blit: the canvas buffer is 240x124; the content area is taller,
 * so we draw in two bands using cv_oy to keep all drawing code in
 * canvas-relative coordinates.
 * Origin: hugelton/Felucca firmware/src/ui_menu.c @ v1.6.1 (a53ecf42) (2-pass blit pattern) */
#define MSET_BAND1  124u
#define MSET_BAND2  (MSET_CANVAS_H - MSET_BAND1)

static void settings_draw(void)
{
    uint32_t n, r, pass;

    ui_tabs_t tabs = {
        .labels  = SMTAB_NAME,
        .count   = SMTAB_COUNT,
        .active  = mset.tab,
        .enc     = ENC_SELECT,
        .visible = mset_tab_visible,
    };

    n = mset.tab == SMTAB_DISPLAY ? mset_display_count() : mset_tab_count(mset.tab);

    for (pass = 0; pass < 2u; pass++) {
        uint32_t band_h = pass ? MSET_BAND2 : MSET_BAND1;
        int32_t  band_oy = pass ? -(int32_t)MSET_BAND1 : 0;

        cv_begin(UI_W, band_h, T_BG);
        cv_oy = band_oy;

        if (!pass)
            ui_tabs_draw(&tabs);

        /* item list for current tab */
#ifdef CONFIG_DOCK
        if (mset.tab == SMTAB_INSTR) {
            if (!pass) mset_draw_instr(MSET_LIST_Y);
        } else
#endif
#ifdef CONFIG_SYS_MENU_NETWORK
        if (mset.tab == SMTAB_NETWORK) {
            for (r = 0; r < MSET_VISIBLE && (mset.scroll + r) < n; r++) {
                uint32_t idx = mset.scroll + r;
                int32_t y = MSET_LIST_Y + (int32_t)r * UI_ROW_H;
                uint8_t sel = (idx == mset.row);
                uint32_t mi = mset.tab == SMTAB_DISPLAY ? idx - MSET_DISPLAY_EXTRA : idx;
                const sys_menu_item_t *item = mset_tab_item(mset.tab, mi);
                if (!item) break;
                uint16_t fg = sel ? T_THEME : T_TEXT;
                ui_row(y, item->name, item->values[item->get()], fg, sel);
            }
            {
                int32_t info_y = MSET_LIST_Y + (int32_t)n * UI_ROW_H + 6;
                mset_draw_network(info_y);
            }
        } else
#endif
        {
            for (r = 0; r < MSET_VISIBLE && (mset.scroll + r) < n; r++) {
                uint32_t idx = mset.scroll + r;
                int32_t y = MSET_LIST_Y + (int32_t)r * UI_ROW_H;
                uint8_t sel = (idx == mset.row);
                if (mset.tab == SMTAB_DISPLAY && idx == MSET_DISPLAY_THEME) {
                    mset_draw_theme_row(y, sel);
                } else {
                    uint32_t mi = mset.tab == SMTAB_DISPLAY ? idx - MSET_DISPLAY_EXTRA : idx;
                    const sys_menu_item_t *item = mset_tab_item(mset.tab, mi);
                    if (!item) break;
                    uint16_t fg = sel ? T_THEME : T_TEXT;
                    ui_row(y, item->name, item->values[item->get()], fg, sel);
                }
            }
            if (n == 0)
                cv_text_c(UI_W / 2, ty_y(MSET_LIST_Y + 30, TY_LB), TY_LB,
                          "(no items)", T_DIM, T_BG);
        }

        ui_scrollbar(MSET_LIST_Y, (int32_t)MSET_LIST_H, n, MSET_VISIBLE, mset.scroll);

        /* hint zones (only in pass 0 to avoid double-registering) */
        if (!pass && hints_visible && n > 0
#ifdef CONFIG_DOCK
            && mset.tab != SMTAB_INSTR
#endif
        ) {
            uint32_t vis = n < MSET_VISIBLE ? n : MSET_VISIBLE;
            int32_t zh = (int32_t)vis * UI_ROW_H;
            uint8_t eids[1] = { ENC_ALGO };
            ui_zone_enc(1, MSET_LIST_Y, UI_W - 2, zh, MSET_CANVAS_Y, eids, 1);
        }

        if (!pass)
            ui_tabs_place_badge(&tabs, MSET_CANVAS_Y);

        cv_oy = 0;
        cv_blit(0, MSET_CANVAS_Y + (pass ? MSET_BAND1 : 0u));
    }

    /* footer: prev/next value for the selected row (always show both) */
    {
        const char *fl = "", *fr = "";
        if (mset.tab == SMTAB_DISPLAY && mset.row == MSET_DISPLAY_THEME) {
            uint32_t pi = ux.pal > 0 ? ux.pal - 1u : UI_NPALETTES - 1u;
            uint32_t ni = ux.pal + 1u < UI_NPALETTES ? ux.pal + 1u : 0u;
            fl = UI_PALETTES[pi].name;
            fr = UI_PALETTES[ni].name;
        } else {
            uint32_t mi = (mset.tab == SMTAB_DISPLAY)
                        ? mset.row - MSET_DISPLAY_EXTRA : mset.row;
            const sys_menu_item_t *fi = mset_tab_item(mset.tab, mi);
            if (fi && fi->n_values > 1) {
                uint8_t fv = fi->get();
                uint8_t pv = fv > 0 ? fv - 1u : fi->n_values - 1u;
                uint8_t nv = fv + 1u < fi->n_values ? fv + 1u : 0u;
                fl = fi->values[pv];
                fr = fi->values[nv];
            }
        }
        ui_footer(fl, "", fr);
    }
}

static void mset_step_value(int32_t dir)
{
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR) {
        ui_list_act(&minstr_list);
        return;
    }
#endif
    if (mset.tab == SMTAB_DISPLAY && mset.row == MSET_DISPLAY_THEME) {
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
        uint32_t mi = (mset.tab == SMTAB_DISPLAY) ? mset.row - MSET_DISPLAY_EXTRA : mset.row;
        const sys_menu_item_t *item = mset_tab_item(mset.tab, mi);
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
        uint32_t n = mset.tab == SMTAB_DISPLAY ? mset_display_count()
                                                : mset_tab_count(mset.tab);
#ifdef CONFIG_DOCK
        if (mset.tab == SMTAB_INSTR)
            return ui_list_input(&minstr_list, id, clicks);
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
        mset_step_value(-1);
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
    .status = settings_status,
    .knob   = settings_knob,
    .button = settings_button,
    .help_text =
        "SELECT  Switch tab\n"
        "ALGO    Select row\n"
        "OCT+    Value up\n"
        "OCT-    Value down",
};
