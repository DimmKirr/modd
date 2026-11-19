// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* REST API page JSON serializers: MVC controller layer.
 * Each serializer reads page data via its _read() function (defined
 * alongside the page) and writes JSON via rpc_tx_* helpers.
 * Lookup table keeps pages decoupled from the RPC layer. */

typedef void (*api_page_json_fn)(struct rpc_tx *tx);

typedef struct {
    const char       *name;
    api_page_json_fn  fn;
} api_page_json_entry_t;

/* ── HOME ───────────────────────────────────────────────────────── */

#ifdef CONFIG_UI_MODD
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
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "palette", d.palette);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "palette_name", d.palette_name);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "bpm", d.bpm);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "playing", d.playing);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "recording", d.rec);
}
#endif

/* ── SETTINGS ───────────────────────────────────────────────────── */

#if defined(CONFIG_UI_MODD) && defined(CONFIG_SYS_MENU)
static void api_page_settings_json(struct rpc_tx *tx)
{
    rpc_tx_key_str(tx, "tab", SMTAB_NAME[mset.tab]);

#ifdef CONFIG_SYS_MENU_NETWORK
    if (mset.tab == SMTAB_NETWORK) {
        net_tab_data_t d;
        char buf[20];
        net_tab_read(&d);
        rpc_tx_char(tx, ',');
        rpc_tx_key_str(tx, "link", d.link);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "ip", d.ip);
        rpc_tx_char(tx, ',');
        if (d.ip) { mset_fmt_ip(buf, d.ip); } else { buf[0] = 0; }
        rpc_tx_key_str(tx, "ip_str", buf);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "gw", d.gw);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "dns", d.dns);
        rpc_tx_char(tx, ',');
        rpc_tx_key_bool(tx, "connected", d.connected);
        return;
    }
#endif
    {
        uint32_t n = mset_tab_count(mset.tab), i;
        rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "\"items\":[");
        for (i = 0; i < n; i++) {
            const sys_menu_item_t *item = mset_tab_item(mset.tab, i);
            if (!item) break;
            if (i) rpc_tx_char(tx, ',');
            rpc_tx_raw(tx, "{");
            rpc_tx_key_str(tx, "name", item->name);
            rpc_tx_char(tx, ',');
            rpc_tx_key_str(tx, "value", item->values[item->get()]);
            rpc_tx_raw(tx, "}");
        }
        rpc_tx_raw(tx, "]");
    }
}
#endif

/* ── WEATHER ────────────────────────────────────────────────────── */

#if defined(CONFIG_UI_MODD) && defined(CONFIG_LWIP)
static void api_page_weather_json(struct rpc_tx *tx)
{
    wx_page_data_t d;
    wx_page_read(&d);
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
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "condition", weather_condition_name(d.w->wmo_code));
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

/* ── Lookup table ───────────────────────────────────────────────── */

static const api_page_json_entry_t api_page_json_table[] = {
#ifdef CONFIG_UI_MODD
    { "HOME", api_page_home_json },
#endif
#if defined(CONFIG_UI_MODD) && defined(CONFIG_SYS_MENU)
    { "MENU", api_page_settings_json },
#endif
#if defined(CONFIG_UI_MODD) && defined(CONFIG_LWIP)
    { "WX",   api_page_weather_json },
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
