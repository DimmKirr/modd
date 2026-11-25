// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* MENU (Settings): every SYS_MENU item (modd_sys_menu.h) by tab, as LVGL widgets.
 *
 *   tabs     the tabs that have something: DISPLAY always (PALETTE), NETWORK with the
 *            network readout (CONFIG_SYS_MENU_NETWORK), the rest when they have items
 *   rows     name and value; an OFF/ON item shows a switch, the selected row is raised
 *   DISPLAY  PALETTE first: four swatches of the palette's track colours and its name
 *   NETWORK  its items, then LINK, IP, GATEWAY, DNS (net_read, ui/page_data.c)
 *   footer   the selected item's previous and next values: what OCT- and OCT+ set
 *
 * SELECT changes the tab, ALGO the row, OCT+ / OCT- step the value (wrapping). The REST API's /modd/menu/<tab> asks for a tab through mset.req; it is
 * taken when the page is entered or on the next frame.
 *
 *   INSTR    (CONFIG_DOCK) the list widget (list.c) over LIST_INSTR (dock/dock_list.c):
 *            modd's engines, the installed dock modules and the .dock files on a USB disk;
 *            ALGO moves, OCT+ starts one (installing it from the disk first, its progress in
 *            the footer), OCT+ held deletes, OCT- closes the settings */
#ifdef CONFIG_SYS_MENU

#define MSET_ROW_H 20u
#define MSET_ROWS_MAX 12u

static struct {
    uint8_t tab;
    uint8_t row;
    uint8_t req;                        /* tab + 1 asked for by the REST API, 0: none */
} mset;

static struct {
    lv_obj_t *tabs, *tab[SMTAB_COUNT], *list;
    lv_obj_t *row[MSET_ROWS_MAX], *val[MSET_ROWS_MAX], *sw[MSET_ROWS_MAX];
    const sys_menu_item_t *it[MSET_ROWS_MAX];
    uint32_t  nrows;
    lv_obj_t *swatch[4], *pal_name;
#ifdef CONFIG_SYS_MENU_NETWORK
    lv_obj_t *net[4];
#endif
} lvt_set;

static uint32_t mset_tab_count(uint32_t tab)
{
    uint32_t n = 0, i;
    for (i = 0; i < SYS_MENU_COUNT; i++)
        if (SYS_MENU[i].tab == tab) n++;
    return n;
}

static int mset_tab_visible(uint32_t tab)
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

/* rows of the tab on screen: DISPLAY has PALETTE before its items */
static uint32_t mset_extra(void) { return mset.tab == SMTAB_DISPLAY ? 1u : 0u; }
static uint32_t mset_nrows(void) { return mset_extra() + mset_tab_count(mset.tab); }

static const sys_menu_item_t *mset_item(uint32_t row)   /* 0: PALETTE, or none */
{
    uint32_t n = 0, i;
    if (row < mset_extra())
        return 0;
    row -= mset_extra();
    for (i = 0; i < SYS_MENU_COUNT; i++)
        if (SYS_MENU[i].tab == mset.tab && n++ == row)
            return &SYS_MENU[i];
    return 0;
}

static int mset_is_switch(const sys_menu_item_t *it)
{
    return it->n_values == 2u && it->values == SM_ONOFF;
}

/* ----------------------------------------------------------------- rows --- */
static lv_obj_t *mset_row_new(const char *name)
{
    lv_obj_t *row = lvt_row(lvt_set.list, MSET_ROW_H), *l;
    l = lv_label_create(row);
    lv_label_set_text_static(l, name);
    lv_obj_add_style(l, &lvt_st_dim, 0);
    lv_obj_set_flex_grow(l, 1);
    return row;
}

static void mset_theme_show(void)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        lvt_bg_color(lvt_set.swatch[k], lvt_pal.trk_c[k], 0);
    lvt_text(lvt_set.pal_name, UI_PALETTES[lvt_pal.idx].name);
}

static void mset_theme_row(void)
{
    lv_obj_t *row = mset_row_new("PALETTE"), *sw;
    uint32_t k;
    sw = lvt_box(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(sw, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(sw, 3, 0);
    for (k = 0; k < 4u; k++) {
        lv_obj_t *s = lv_obj_create(sw);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 8, 8);
        lv_obj_set_style_radius(s, 2, 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lvt_set.swatch[k] = s;
    }
    lvt_set.pal_name = lv_label_create(row);
    lv_obj_set_width(lvt_set.pal_name, 60);
    lv_obj_set_style_text_align(lvt_set.pal_name, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_static(lvt_set.pal_name, "");
    lvt_set.row[0] = row;
    lvt_set.it[0] = 0;
    mset_theme_show();
}

static void mset_item_row(uint32_t r, const sys_menu_item_t *it)
{
    lv_obj_t *row = mset_row_new(it->name);
    lvt_set.sw[r] = 0;
    if (mset_is_switch(it)) {           /* shows the value; OCT+/OCT- change it */
        lv_obj_t *s = lv_switch_create(row);
        lv_obj_set_size(s, 30, 16);
        lv_obj_set_clickable(s, false);
        lvt_set.sw[r] = s;
    }
    lvt_set.val[r] = lv_label_create(row);
    lv_label_set_text_static(lvt_set.val[r], "");
    lvt_set.row[r] = row;
    lvt_set.it[r] = it;
}

#ifdef CONFIG_SYS_MENU_NETWORK
static const char *const MSET_NET_NAMES[4] = { "LINK", "IP", "GATEWAY", "DNS" };

static void mset_net_rows(void)
{
    uint32_t i;
    lv_obj_set_style_margin_top(lvt_rule(lvt_set.list), 4, 0);
    for (i = 0; i < 4u; i++) {
        lv_obj_t *row = mset_row_new(MSET_NET_NAMES[i]);
        lvt_set.net[i] = lv_label_create(row);
        lv_obj_add_style(lvt_set.net[i], &lvt_st_dim, 0);
        lv_label_set_text_static(lvt_set.net[i], "");
    }
}

static void mset_ip_text(lv_obj_t *l, uint32_t ip)
{
    char b[16];
    if (!ip) {
        lvt_text(l, "-");
        return;
    }
    lv_snprintf(b, sizeof b, "%u.%u.%u.%u", (unsigned)(ip & 255u), (unsigned)((ip >> 8) & 255u),
                (unsigned)((ip >> 16) & 255u), (unsigned)(ip >> 24));
    lvt_text(l, b);
}

static void mset_net_show(void)
{
    net_data_t d;
    net_read(&d);
    lvt_text(lvt_set.net[0], d.link);
    lvt_text_color(lvt_set.net[0], d.connected ? lvt_pal.primary : lvt_pal.dim);
    mset_ip_text(lvt_set.net[1], d.ip);
    mset_ip_text(lvt_set.net[2], d.gw);
    mset_ip_text(lvt_set.net[3], d.dns);
}
#endif

#ifdef CONFIG_DOCK
/* ---------------------------------------------------- INSTR (CONFIG_DOCK) --- */
static lvt_list_t minstr_list;
static uint8_t    minstr_open;

/* dock_install's progress, drawn at once: the install blocks the UI task until it is done */
static void minstr_progress(const char *what, uint32_t pct)
{
    char b[48];
    lv_snprintf(b, sizeof b, "%s %u%%", what, (unsigned)pct);
    lvt_footer("", b, "");
    lv_refr_now(lvt_disp);
}

static void minstr_enter(void)
{
    lvt_list_init(&minstr_list, &LIST_INSTR, ENC_ALGO);
    lvt_list_build(&minstr_list, lvt_set.list, 7, MSET_ROW_H);
    minstr_list.cursor = dock_cur == DOCK_NONE ? 0 : dock_cur + 1u;
    dock_progress = minstr_progress;
    lvt_list_open(&minstr_list);
    minstr_open = 1;
}

static void minstr_leave(void)
{
    if (!minstr_open)
        return;
    minstr_open = 0;
    dock_progress = 0;
    lvt_list_close(&minstr_list);
}
#endif

/* the tab's rows, made again when the tab changes */
static void mset_build(void)
{
    uint32_t r, n = mset_nrows(), t;
#ifdef CONFIG_DOCK
    minstr_leave();
#endif
    lv_obj_clean(lvt_set.list);
    lvt_set.nrows = 0;
    for (t = 0; t < SMTAB_COUNT; t++)
        if (lvt_set.tab[t])
            lv_obj_set_state(lvt_set.tab[t], LV_STATE_CHECKED, t == mset.tab);
    if (lvt_set.tab[mset.tab])
        lv_obj_scroll_to_view(lvt_set.tab[mset.tab], LV_ANIM_OFF);
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR) {
        minstr_enter();
        return;
    }
#endif
    for (r = 0; r < n && r < MSET_ROWS_MAX; r++) {
        const sys_menu_item_t *it = mset_item(r);
        if (it)
            mset_item_row(r, it);
        else
            mset_theme_row();
        lvt_set.nrows = r + 1u;
    }
#ifdef CONFIG_SYS_MENU_NETWORK
    if (mset.tab == SMTAB_NETWORK)
        mset_net_rows();
#endif
#ifdef CONFIG_SYS_MENU_NETWORK
    if (n == 0 && mset.tab != SMTAB_NETWORK)
#else
    if (n == 0)
#endif
        lvt_caption(lvt_set.list, "(no items)");
    if (mset.row >= lvt_set.nrows)
        mset.row = 0;
}

/* every frame: the values (a setting may change behind the page), the selection, the footer */
static void mset_show(void)
{
    uint32_t r;
    const sys_menu_item_t *fi = 0;
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR) {
        lvt_list_poll(&minstr_list);
        lvt_footer("", "", "");
        return;
    }
#endif
    for (r = 0; r < lvt_set.nrows; r++) {
        const sys_menu_item_t *it = lvt_set.it[r];
        lv_obj_set_state(lvt_set.row[r], LV_STATE_CHECKED, r == mset.row);
        if (!it)
            continue;
        {
            uint32_t v = it->get();
            if (v >= it->n_values)
                v = 0;
            lvt_text(lvt_set.val[r], it->n_values ? it->values[v] : "");
            if (lvt_set.sw[r])
                lv_obj_set_state(lvt_set.sw[r], LV_STATE_CHECKED, v != 0);
        }
        if (r == mset.row)
            fi = it;
    }
#ifdef CONFIG_SYS_MENU_NETWORK
    if (mset.tab == SMTAB_NETWORK)
        mset_net_show();
#endif
    if (mset.tab == SMTAB_DISPLAY && mset.row == 0) {
        uint32_t n = UI_NPALETTES;
        lvt_footer(UI_PALETTES[(lvt_pal.idx + n - 1u) % n].name, "", UI_PALETTES[(lvt_pal.idx + 1u) % n].name);
    } else if (fi && fi->n_values > 1u) {
        uint32_t v = fi->get() % fi->n_values;
        lvt_footer(fi->values[(v + fi->n_values - 1u) % fi->n_values], "", fi->values[(v + 1u) % fi->n_values]);
    } else {
        lvt_footer("", "", "");
    }
}

static void mset_select(uint32_t row)
{
    mset.row = (uint8_t)row;
    if (row < lvt_set.nrows)
        lv_obj_scroll_to_view(lvt_set.row[row], LV_ANIM_OFF);
}

/* ------------------------------------------------------------- controls --- */
static void mset_next_tab(int32_t dir)
{
    int32_t t = (int32_t)mset.tab;
    uint32_t tries = SMTAB_COUNT;
    do {
        t += dir > 0 ? 1 : -1;
        if (t < 0) t = SMTAB_COUNT - 1;
        if (t >= (int32_t)SMTAB_COUNT) t = 0;
    } while (!mset_tab_visible((uint32_t)t) && --tries);
    mset.tab = (uint8_t)t;
    mset.row = 0;
    mset_build();
}

static void mset_step_value(int32_t dir)
{
    const sys_menu_item_t *it;
    if (mset.tab == SMTAB_DISPLAY && mset.row == 0) {   /* PALETTE: the next palette */
        uint32_t n = UI_NPALETTES;
        lvt_pal_set((lvt_pal.idx + (dir > 0 ? 1u : n - 1u)) % n);
        lvt_pal_apply();
        mset_theme_show();
        sys_settings.ui_pal = (uint8_t)lvt_pal_to_stored(lvt_pal.idx);
        sys_settings_dirty = 1;
        return;
    }
    it = mset_item(mset.row);
    if (it && it->n_values > 0) {
        uint8_t v = it->get();
        if (dir > 0)
            v = (uint8_t)((v + 1u) % it->n_values);
        else
            v = v > 0 ? (uint8_t)(v - 1u) : (uint8_t)(it->n_values - 1u);
        it->set(v);
    }
}

static int settings_select(int32_t clicks)
{
    if (clicks)
        mset_next_tab(clicks);
    return 1;
}

static int settings_algo(int32_t clicks)
{
    int32_t r = (int32_t)mset.row + clicks, n = (int32_t)lvt_set.nrows;
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR)
        return lvt_list_input(&minstr_list, ENC_ALGO, clicks);
#endif
    if (r < 0) r = 0;
    if (n > 0 && r >= n) r = n - 1;
    mset_select((uint32_t)r);
    return 1;
}

static const ctl_slot_t SETTINGS_CONTROLS[] = {
    { ENC_SELECT, P_COUNT, 0, 0, 1, 0, "TAB", settings_select },
    { ENC_ALGO,   P_COUNT, 0, 0, 1, 0, "ROW", settings_algo },
};

static int settings_button(uint32_t id, uint32_t down)
{
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR && (id == BTN_OCTUP || id == BTN_OCTDN)) {
        if (lvt_list_button(&minstr_list, id, down))
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

/* ----------------------------------------------------------------- page --- */
static void settings_take_req(void)
{
    uint32_t t = mset.req;
    mset.req = 0;
    if (t && t - 1u < SMTAB_COUNT && mset_tab_visible(t - 1u)) {
        mset.tab = (uint8_t)(t - 1u);
        mset.row = 0;
        mset_build();
    }
}

static void settings_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *l;
    uint32_t t;

    lvt_set.tabs = lvt_box(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(lvt_set.tabs, 18);
    lv_obj_set_style_pad_column(lvt_set.tabs, 2, 0);
    lv_obj_set_scrollable(lvt_set.tabs, true);
    lv_obj_set_scroll_dir(lvt_set.tabs, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(lvt_set.tabs, LV_SCROLLBAR_MODE_OFF);
    mset.tab = SMTAB_COUNT;
    for (t = 0; t < SMTAB_COUNT; t++) {
        lvt_set.tab[t] = 0;
        if (!mset_tab_visible(t))
            continue;
        if (mset.tab == SMTAB_COUNT)
            mset.tab = (uint8_t)t;      /* the first tab there is */
        l = lv_label_create(lvt_set.tabs);
        lv_label_set_text_static(l, SMTAB_NAME[t]);
        lv_obj_add_style(l, &lvt_st_small, 0);
        lv_obj_add_style(l, &lvt_st_dim, 0);
        lv_obj_add_style(l, &lvt_st_row_sel, LV_STATE_CHECKED);
        lv_obj_set_style_pad_hor(l, 4, 0);
        lv_obj_set_style_pad_ver(l, 2, 0);
        lv_obj_set_style_radius(l, 3, 0);
        lvt_set.tab[t] = l;
    }
    if (mset.tab == SMTAB_COUNT)
        mset.tab = SMTAB_DISPLAY;
    lvt_rule(body);

    lvt_set.list = lvt_box(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(lvt_set.list, 1);
    lv_obj_set_style_pad_row(lvt_set.list, 0, 0);
    lv_obj_set_scrollable(lvt_set.list, true);
    lv_obj_set_scroll_dir(lvt_set.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(lvt_set.list, LV_SCROLLBAR_MODE_AUTO);

    mset.row = 0;
    mset_build();
    settings_take_req();
    mset_show();
}

static void settings_status(void)
{
    if (mset.req)
        settings_take_req();
    mset_show();
}

static const modd_page_t PAGE_SETTINGS = {
    .name   = "MENU",
    .title  = "Settings",
    .view   = VIEW_SETTINGS,
    .flags  = PAGEF_SYSTEM,
    .enter  = settings_enter,
#ifdef CONFIG_DOCK
    .leave  = minstr_leave,
#endif
    .status = settings_status,
    .controls  = SETTINGS_CONTROLS,
    .ncontrols = 2,
    .button = settings_button,
    .help_text =
        "SELECT  Switch tab\n"
        "ALGO    Select row\n"
        "OCT+    Value up\n"
        "OCT-    Value down",
};

#endif /* CONFIG_SYS_MENU */
