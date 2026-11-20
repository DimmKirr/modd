// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* REST API pages: going to a page, and what a page shows as JSON.
 *
 * The data comes from the shared page controllers (ui/page_data.c: home_read,
 * net_read, wx_read), the same structs the themes draw from, so every theme that
 * registers pages with ui_nav.c gets the same JSON for the same page name. A lookup
 * table maps a page's .name to its serializer (pages are const, so they carry no
 * callback). The modd and LVGL themes have the same pages; the palette and the
 * settings tab are each theme's own state (ux / lvt_pal, mset).
 *
 *   page     modd theme   LVGL theme   data
 *   HOME     yes          yes          tracks, transport, the selected track's sound, palette
 *   MENU     yes          yes          the settings of the tab showing (NETWORK: the link)
 *   WX       yes          yes          the weather report
 *
 * api_page_goto() is the one page lookup: GET /modd/<page>[/<sub>] (api_ctl.c)
 * and FM1.navigate (rpc_fm1.c) both call it. */

typedef void (*api_page_json_fn)(struct rpc_tx *tx);

typedef struct {
    const char       *name;
    api_page_json_fn  fn;
} api_page_json_entry_t;

#ifdef UI_SHELL
static void api_fmt_ip(char *buf, uint32_t ip)    /* "a.b.c.d", buf of 16 or more */
{
    uint32_t i, n = 0;
    for (i = 0; i < 4u; i++) {
        uint32_t o = (ip >> (8u * i)) & 255u;
        if (i) buf[n++] = '.';
        if (o >= 100u) buf[n++] = (char)('0' + o / 100u);
        if (o >= 10u) buf[n++] = (char)('0' + o / 10u % 10u);
        buf[n++] = (char)('0' + o % 10u);
    }
    buf[n] = 0;
}

/* ── HOME ───────────────────────────────────────────────────────── */

static void api_page_home_json(struct rpc_tx *tx)
{
    home_data_t d;
    uint32_t k;
    home_read(&d);
    rpc_tx_key_uint(tx, "selected_track", d.selected);
    rpc_tx_char(tx, ',');
    rpc_tx_raw(tx, "\"tracks\":[");
    for (k = 0; k < NTRK; k++) {
        if (k) rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "{");
        rpc_tx_key_uint(tx, "index", k);
        rpc_tx_char(tx, ',');
        rpc_tx_key_str(tx, "engine", d.engine_name[k]);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "preset", d.preset[k]);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "engine_id", d.engine_id[k]);
        rpc_tx_raw(tx, "}");
    }
    rpc_tx_raw(tx, "]");
#if defined(CONFIG_UI_MODD)
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "palette", ux.pal);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "palette_name", UI_PALETTES[ux.pal].name);
#elif defined(CONFIG_UI_LVGL)
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "palette", lvt_pal.idx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "palette_name", UI_PALETTES[lvt_pal.idx].name);
#endif
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "bpm", d.bpm);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "playing", d.playing);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "recording", d.rec);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "oct", d.oct);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "root", d.root);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "scale", d.scale);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "level", d.level);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "atk", d.atk);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "dec", d.dec);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "sus", d.sus);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "rel", d.rel);
}

/* ── network and settings: MENU ─────────────────────────────────── */

/* "link", "ip", "ip_str", "gw", "dns", "connected": no leading comma */
static void api_net_json(struct rpc_tx *tx)
{
    net_data_t d;
    char buf[16];
    net_read(&d);
    rpc_tx_key_str(tx, "link", d.link);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "ip", d.ip);
    rpc_tx_char(tx, ',');
    if (d.ip) api_fmt_ip(buf, d.ip); else buf[0] = 0;
    rpc_tx_key_str(tx, "ip_str", buf);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "gw", d.gw);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "dns", d.dns);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "connected", d.connected);
}

#ifdef CONFIG_SYS_MENU
/* "items":[{name, value, index, tab}]: the SYS_MENU items of one tab; no leading comma */
static void api_items_json(struct rpc_tx *tx, uint32_t tab)
{
    uint32_t i, n = 0;
    rpc_tx_raw(tx, "\"items\":[");
    for (i = 0; i < SYS_MENU_COUNT; i++) {
        const sys_menu_item_t *it = &SYS_MENU[i];
        uint32_t v;
        if (it->tab != tab)
            continue;
        v = it->get();
        if (n++) rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "{");
        rpc_tx_key_str(tx, "name", it->name);
        rpc_tx_char(tx, ',');
        rpc_tx_key_str(tx, "value", v < it->n_values ? it->values[v] : "");
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "index", v);
        rpc_tx_char(tx, ',');
        rpc_tx_key_str(tx, "tab", SMTAB_NAME[it->tab]);
        rpc_tx_raw(tx, "}");
    }
    rpc_tx_raw(tx, "]");
}
#endif

/* ── MENU (the tabbed settings) ─────────────────────────────────── */

#ifdef CONFIG_SYS_MENU
static void api_page_settings_json(struct rpc_tx *tx)
{
    rpc_tx_key_str(tx, "tab", SMTAB_NAME[mset.tab]);
#ifdef CONFIG_SYS_MENU_NETWORK
    if (mset.tab == SMTAB_NETWORK) {
        rpc_tx_char(tx, ',');
        api_net_json(tx);
        return;
    }
#endif
    rpc_tx_char(tx, ',');
    api_items_json(tx, mset.tab);
}
#endif

/* ── WX ─────────────────────────────────────────────────────────── */

#ifdef CONFIG_LWIP
static void api_page_weather_json(struct rpc_tx *tx)
{
    wx_data_t d;
    wx_read(&d);
    rpc_tx_key_bool(tx, "valid", d.w->valid);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "fetching", d.fetching);
    if (!d.w->valid) return;
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "temp_c", (int32_t)d.w->temp_c);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "humidity", d.w->humidity);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "wmo_code", d.w->wmo_code);
#ifdef WEATHER_COLORS_H    /* the condition names come with a theme's weather page */
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "condition", weather_condition_name(d.w->wmo_code));
#endif
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "wind_kmh", d.w->wind_kmh);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "city", d.w->city);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "country", d.w->country);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "updated_ms", d.w->updated_ms);
}
#endif
#endif /* UI_SHELL */

/* ── Lookup table ───────────────────────────────────────────────── */

static const api_page_json_entry_t api_page_json_table[] = {
#ifdef UI_SHELL
    { "HOME",   api_page_home_json },
#ifdef CONFIG_SYS_MENU
    { "MENU",   api_page_settings_json },
#endif
#ifdef CONFIG_LWIP
    { "WX",     api_page_weather_json },
#endif
#endif
    { 0, 0 }
};

static api_page_json_fn api_page_json_find(const char *name)
{
    const api_page_json_entry_t *e;
    for (e = api_page_json_table; e->name; e++) {
        if (rpc_streq_nocase(e->name, name))
            return e->fn;
    }
    return 0;
}

/* ── going to a page ────────────────────────────────────────────── */

/* A page's sub-path (`menu/net`): 1 handled, 0 unknown. */
static int api_page_sub(uint32_t idx, const char *sub)
{
#if defined(UI_SHELL) && defined(CONFIG_SYS_MENU)
    if (rpc_streq_nocase(nav_pages[idx]->name, "MENU")) {
        uint32_t t;
        for (t = 0; t < SMTAB_COUNT; t++) {
            if (rpc_streq_nocase(SMTAB_NAME[t], sub)) {
#ifdef CONFIG_UI_LVGL
                mset.req = (uint8_t)(t + 1u);   /* entered later, on the UI task: the page takes the tab */
#else
                mset.tab = (uint8_t)t;
#endif
                nav_dirty = 1;
                return 1;
            }
        }
    }
#endif
    (void)idx;
    (void)sub;
    return 0;
}

enum { API_GOTO_OK = 0, API_GOTO_NO_PAGE = -1, API_GOTO_NO_SUB = -2 };

/* Go to the page called `page` (case ignored), then to its `sub` ("" for none). On
 * API_GOTO_OK *idx is the page. A theme that changes pages in its own task (LVGL) shows
 * it on its next frame: /api/page reports it from then on. */
static int api_page_goto(const char *page, const char *sub, uint32_t *idx)
{
    uint32_t i = nav_find(page);
    if (i >= nav_npage)
        return API_GOTO_NO_PAGE;
    nav_request(i);
    if (sub[0] && !api_page_sub(i, sub))
        return API_GOTO_NO_SUB;
    *idx = i;
    return API_GOTO_OK;
}

/* "page", "index" of page `idx` and its "data" if it has any: no leading comma */
static void api_page_fields_json(struct rpc_tx *tx, uint32_t idx)
{
    api_page_json_fn jfn = api_page_json_find(nav_pages[idx]->name);
    rpc_tx_key_str(tx, "page", nav_pages[idx]->name);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "index", idx);
    if (jfn) {
        rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "\"data\":{");
        jfn(tx);
        rpc_tx_char(tx, '}');
    }
}
