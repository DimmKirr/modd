// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* REST API pages: going to a page, and what a page shows as JSON.
 *
 * The data comes from the shared page controllers (ui/page_data.c: home_read,
 * net_read, wx_read), the same structs the shells draw from, so every shell that
 * registers pages with ui_nav.c gets the same JSON for the same page name. A lookup
 * table maps a page's .name to its serializer (pages are const, so they carry no
 * callback). The palette and the settings tab are the shell's own state (lvt_pal, mset).
 *
 *   page     data
 *   HOME     tracks, transport, the selected track's sound, palette
 *   MENU     the settings of the tab showing (NETWORK: the link)
 *   WX       the weather report
 *   In test builds with the modd shell (CONFIG_RPC_INPUT, CONFIG_AUDIO) every page but HOME has
 *   "track", "engine", "title" and "cards":[{"label","value"} x4], the card labels as drawn
 *   (lvt_card, the one place the cards are set, so the data and the screen agree). A page
 *   with no entry in the table has just those; the pages below add their own state:
 *   SEQ STEP DETAIL AUTO (the steps, LEN/DIV/SWING/GATE, playing, the step), MIXER, SONG,
 *   CLOCK, USER, PRESETS, PHRASES, PROJECT. The cards belong to the page on screen: in the
 *   reply of GET /modd/<page> they are not there yet (the UI task enters the page on its next
 *   frame), which says "ready":false; GET /api/page has them. HOME has selected_track and
 *   tracks[].engine instead of "track" and "engine".
 *
 * api_page_goto() is the one page lookup: GET /modd/<page>[/<sub>] (api_ctl.c)
 * and FM1.navigate (rpc_fm1.c) both call it. */

typedef void (*api_page_json_fn)(struct rpc_tx *tx);

typedef struct {
    const char       *name;
    api_page_json_fn  fn;
} api_page_json_entry_t;

#ifdef UI_SHELL
#ifdef CONFIG_UI_SHELL_MODD
#ifdef CONFIG_RPC_INPUT
#ifdef CONFIG_AUDIO
static void api_page_common(struct rpc_tx *tx);    /* below: used by MENU and WX too */
#endif
#endif
#endif
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
#ifdef CONFIG_UI_SHELL_MODD
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
#ifdef CONFIG_UI_SHELL_MODD
#ifdef CONFIG_RPC_INPUT
#ifdef CONFIG_AUDIO
    api_page_common(tx);
    rpc_tx_char(tx, ',');
#endif
#endif
#endif
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
#ifdef CONFIG_UI_SHELL_MODD
#ifdef CONFIG_RPC_INPUT
#ifdef CONFIG_AUDIO
    api_page_common(tx);
    rpc_tx_char(tx, ',');
#endif
#endif
#endif
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
#ifdef WEATHER_COLORS_H    /* the condition names come with a shell's weather page */
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

/* ── what a page shows (test builds, modd shell) ────────────────── */

#ifdef CONFIG_UI_SHELL_MODD
#ifdef CONFIG_RPC_INPUT
#ifdef CONFIG_AUDIO
static uint32_t api_page_idx;      /* the page being described */

/* "track", "engine", "title", "cards" (or "ready":false when the page is not on screen yet);
 * no leading or trailing comma */
static void api_page_common(struct rpc_tx *tx)
{
    uint32_t s = modd_sel() < NTRK ? modd_sel() : 0, k;
    uint8_t e = modd_engine_of(modd_trk(s));
    const modd_page_t *p = nav_pages[api_page_idx];
    rpc_tx_key_uint(tx, "track", s);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "engine", (e < NENGINES && ENGINES[e]->name) ? ENGINES[e]->name : "---");
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "title", p->title ? p->title : p->name);
    if (api_page_idx != nav_cur) {
        rpc_tx_char(tx, ',');
        rpc_tx_key_bool(tx, "ready", 0);       /* the UI task has not entered the page yet: GET /api/page has the cards */
        return;
    }
    {
        modd_card_t c[4];                /* the effect page: its cards come from the build's own data (modd_pages.h) */
        uint32_t n = rpc_streq_nocase(p->name, "INSERT") ? modd_fx_page_read(c, 4u) : 0;
        if (n) {
            rpc_tx_char(tx, ',');
            rpc_tx_raw(tx, "\"cards\":[");
            for (k = 0; k < n; k++) {
                if (k) rpc_tx_char(tx, ',');
                rpc_tx_raw(tx, "{");
                rpc_tx_key_str(tx, "label", c[k].label);
                rpc_tx_char(tx, ',');
                rpc_tx_key_int(tx, "value", c[k].value);
                rpc_tx_char(tx, ',');
                rpc_tx_key_str(tx, "text", c[k].text);
                rpc_tx_char(tx, ',');
                rpc_tx_key_int(tx, "min", c[k].min);
                rpc_tx_char(tx, ',');
                rpc_tx_key_int(tx, "max", c[k].max);
                rpc_tx_raw(tx, "}");
            }
            rpc_tx_char(tx, ']');
            return;
        }
    }
    if (!lvt_card[0].label)         /* a page without cards (lvt_page_begin clears them) */
        return;
    rpc_tx_char(tx, ',');
    rpc_tx_raw(tx, "\"cards\":[");
    for (k = 0; k < 4u; k++) {
        if (k) rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "{");
        rpc_tx_key_str(tx, "label", lv_label_get_text(lvt_card[k].label));
        rpc_tx_char(tx, ',');
        rpc_tx_key_str(tx, "value", lv_label_get_text(lvt_card[k].value));
        rpc_tx_raw(tx, "}");
    }
    rpc_tx_char(tx, ']');
}

/* a page with no serializer of its own */
static void api_page_generic_json(struct rpc_tx *tx)
{
    api_page_common(tx);
}

/* the selected track's sequencer, each field after a comma: "steps" has one '1' (a note) or
 * '0' per step, "playhead" is the step the transport is on */
static void api_seq_fields(struct rpc_tx *tx)
{
    const track_t *t = modd_trk(modd_sel() < NTRK ? modd_sel() : 0);
    char s[NSTEP + 1];
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        s[k] = t->step[k].note[0] ? '1' : '0';
    s[NSTEP] = 0;
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "playing", song.playing != 0);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "len", modd_param(t, P_SLEN));
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "div", modd_param(t, P_SDIV));
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "swing", modd_param(t, P_SSWING));
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "gate", modd_param(t, P_SGATE));
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "playhead", t->seq_idx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "steps", s);
}

static void api_page_seq_json(struct rpc_tx *tx)       /* SEQ */
{
    api_page_common(tx);
    api_seq_fields(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "cursor", lvt_seq.pos);
}

static void api_page_step_json(struct rpc_tx *tx)      /* STEP */
{
    api_page_common(tx);
    api_seq_fields(tx);
}

static void api_page_detail_json(struct rpc_tx *tx)    /* DETAIL: the step the page is on */
{
    const step_t *st = &modd_trk(modd_sel() < NTRK ? modd_sel() : 0)->step[lvt_detail_idx];
    uint32_t k;
    api_page_common(tx);
    api_seq_fields(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "step", (uint32_t)lvt_detail_idx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "note", st->note[0]);
    rpc_tx_char(tx, ',');
    rpc_tx_raw(tx, "\"notes\":[");
    for (k = 0; k < 4u && st->note[k]; k++) {
        if (k) rpc_tx_char(tx, ',');
        rpc_tx_uint(tx, st->note[k]);
    }
    rpc_tx_raw(tx, "]");
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "vel", st->vel);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "len", modd_step_len(st));
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "flags", st->flags);
#ifdef CONFIG_SEQ_FELUCCA      /* step_t.hit CHANCE (0 = always), flags: bit 0 accent, bits 1..2 RATCH; len is NUDGE */
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "chance", st->hit);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "ratch", (st->flags >> 1) & 3u);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "accent", st->flags & 1u);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "nudge", modd_step_nudge(st));
#endif
}

#if defined(CONFIG_SEQ_FELUCCA) && !defined(CONFIG_CORE_FELUCCA)
static void api_page_auto_json(struct rpc_tx *tx)      /* AUTO: the locks of one step */
{
    uint32_t tr = modd_sel() < NTRK ? modd_sel() : 0, k;
    uint8_t lv;
    auto_ensure_init();
    lv = auto_locks[tr][auto_step][auto_pidx];
    api_page_common(tx);
    api_seq_fields(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "step", auto_step);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "param_index", auto_pidx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "param", auto_pnames[auto_pidx]);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "locked", lv != 0xFFu);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "value", lv != 0xFFu ? (int32_t)lv : -1);
    rpc_tx_char(tx, ',');
    rpc_tx_raw(tx, "\"locks\":[");       /* every parameter of the step, -1: not locked */
    for (k = 0; k < AUTO_NPARAMS; k++) {
        uint8_t v = auto_locks[tr][auto_step][k];
        if (k) rpc_tx_char(tx, ',');
        rpc_tx_int(tx, v != 0xFFu ? (int32_t)v : -1);
    }
    rpc_tx_raw(tx, "]");
}
#endif

static void api_page_mixer_json(struct rpc_tx *tx)
{
    mixer_data_t d;
    uint32_t k;
    mixer_read(&d);
    api_page_common(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_raw(tx, "\"tracks\":[");
    for (k = 0; k < NTRK; k++) {
        if (k) rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "{");
        rpc_tx_key_uint(tx, "index", k);
        rpc_tx_char(tx, ',');
        rpc_tx_key_int(tx, "level", d.level[k]);
        rpc_tx_char(tx, ',');
        rpc_tx_key_int(tx, "pan", d.pan[k]);
        rpc_tx_char(tx, ',');
        rpc_tx_key_int(tx, "mute", modd_tpi(k, P_MUTE));
        rpc_tx_raw(tx, "}");
    }
    rpc_tx_raw(tx, "]");
}

static void api_page_song_json(struct rpc_tx *tx)
{
    api_page_common(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "bpm", modd_gp(G_BPM));
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "playing", song.playing != 0);
}

static void api_page_clock_json(struct rpc_tx *tx)
{
    api_page_common(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "bpm", modd_gp(G_BPM));
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "swing", modd_gp(G_SWING));
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "clock", modd_gp(G_CLOCK));
}

/* the text of card k of the page being described when it is on screen, else 0. The storage pages
 * take what they show from here: the flash is read by the UI task only (this task would read it
 * while the UI task is inside a flash read: the scheduler switches tasks in there) */
static const char *api_card_text(uint32_t k)
{
    if (api_page_idx != nav_cur || !lvt_card[0].label)
        return 0;
    return lv_label_get_text(lvt_card[k].value);
}

static void api_page_user_json(struct rpc_tx *tx)      /* a slot of the user presets */
{
    const char *v = api_card_text(1);       /* LOAD: USED or FREE */
    api_page_common(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "slot", (uint32_t)modd_gp(G_SLOT));
    if (v) {
        rpc_tx_char(tx, ',');
        rpc_tx_key_bool(tx, "used", v[0] == 'U');
    }
}

static void api_page_presets_json(struct rpc_tx *tx)
{
    const engine_t *e = lvt_cur_engine();
    api_page_common(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "preset", modd_trk(modd_sel() < NTRK ? modd_sel() : 0)->preset);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "count", e ? e->npresets : 0u);
}

static void api_page_phrases_json(struct rpc_tx *tx)
{
    const char *v = api_card_text(1);       /* LOAD: OK or EMPTY */
    api_page_common(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "pattern", (uint32_t)lvt_phrases_idx);
    if (v) {
        rpc_tx_char(tx, ',');
        rpc_tx_key_bool(tx, "loadable", v[0] == 'O');
    }
}

static void api_page_project_json(struct rpc_tx *tx)
{
    const char *n = api_card_text(1);       /* NAME: the name, EMPTY, or --- for no name */
    api_page_common(tx);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "slot", (uint32_t)modd_gp(G_SLOT));
    if (n) {
        rpc_tx_char(tx, ',');
        rpc_tx_key_bool(tx, "used", !(n[0] == 'E' && n[1] == 'M' && n[2] == 'P' && n[3] == 'T' && n[4] == 'Y' && !n[5]));
        rpc_tx_char(tx, ',');
        rpc_tx_key_str(tx, "name", n);
    }
}
#endif /* CONFIG_AUDIO */
#endif /* CONFIG_RPC_INPUT */
#endif /* CONFIG_UI_SHELL_MODD */
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
#ifdef CONFIG_UI_SHELL_MODD
#ifdef CONFIG_RPC_INPUT
#ifdef CONFIG_AUDIO
    { "SEQ",     api_page_seq_json },
    { "STEP",    api_page_step_json },
    { "DETAIL",  api_page_detail_json },
#if defined(CONFIG_SEQ_FELUCCA) && !defined(CONFIG_CORE_FELUCCA)
    { "AUTO",    api_page_auto_json },
#endif
    { "MIXER",   api_page_mixer_json },
    { "SONG",    api_page_song_json },
    { "CLOCK",   api_page_clock_json },
    { "USER",    api_page_user_json },
    { "PRESETS", api_page_presets_json },
    { "PHRASES", api_page_phrases_json },
    { "PROJECT", api_page_project_json },
#endif
#endif
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
#ifdef CONFIG_UI_SHELL_MODD
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
 * API_GOTO_OK *idx is the page. A shell that changes pages in its own task (the modd
 * shell) shows it on its next frame: /api/page reports it from then on. */
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
#ifdef CONFIG_UI_SHELL_MODD
#ifdef CONFIG_RPC_INPUT
#ifdef CONFIG_AUDIO
    api_page_idx = idx;
    if (!jfn)
        jfn = api_page_generic_json;      /* "track", "engine", "title", "cards" */
#endif
#endif
#endif
    if (jfn) {
        rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "\"data\":{");
        jfn(tx);
        rpc_tx_char(tx, '}');
    }
}
