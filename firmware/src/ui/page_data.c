// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Page data: what a page shows, read from the firmware's state, for every theme.
 *
 * A data struct and a _read() function per subject (the "controller" of the REST API's
 * MVC pattern). A theme's page fills its widgets or canvas from them, and the REST API
 * (rpc/api_pages.c) serializes the same structs, so a theme and the API cannot disagree
 * about a value. Nothing here draws or knows a theme; theme state (the palette, a
 * settings tab) stays with the theme.
 *
 * Included from modd.c after ui_nav.c, before the theme root, for the themes with the
 * shell_* entry points (UI_SHELL: the modd theme). Readers are cheap: a theme may call them
 * every frame.
 *
 *   home_read()      tracks, transport, the selected track's sound
 *   net_read()       the network link and its addresses (stubs read 0 without lwIP)
 *   wx_read()        the weather report */

/* ----------------------------------------------------------------- home --- */
typedef struct {
    const char *engine_name[NTRK];
    uint8_t     engine_id[NTRK];
    uint8_t     preset[NTRK];
    uint8_t     selected;
    uint8_t     playing;
    uint8_t     rec;
    uint16_t    bpm;
    int16_t     oct;
    uint8_t     root;               /* 0..11, C = 0 */
    uint8_t     scale;
    int16_t     level;
    int16_t     atk;
    int16_t     dec;
    int16_t     sus;
    int16_t     rel;
} home_data_t;

static void home_read(home_data_t *d)
{
    uint32_t k;
    const track_t *t;
    d->selected = song.sel < NTRK ? song.sel : 0;
    d->playing  = song.playing != 0;
    d->rec      = song.rec != 0;
    d->bpm      = song.bpm;
    for (k = 0; k < NTRK; k++) {
        uint8_t eng = trk[k].engine;
        d->engine_id[k]   = eng;
        d->engine_name[k] = (eng < NENGINES && ENGINES[eng]->name) ? ENGINES[eng]->name : "---";
        d->preset[k]      = trk[k].preset;
    }
    t = &trk[d->selected];
    d->oct   = t->p[P_OCT];
    d->root  = (uint8_t)((t->p[P_ROOT] % 12 + 12) % 12);
    d->scale = (uint8_t)t->p[P_SCALE];
    d->level = t->p[P_LEVEL];
    d->atk   = t->p[P_ATK];
    d->dec   = t->p[P_DEC];
    d->sus   = t->p[P_SUS];
    d->rel   = t->p[P_REL];
}

/* -------------------------------------------------------------- network --- */
typedef struct {
    uint32_t    ip, gw, dns;        /* IPv4, first octet in the low byte; 0: none */
    const char *link;               /* "ETH", "iPHONE", "WIFI" or "-" */
    uint8_t     connected;          /* has an address */
} net_data_t;

/* the link the address came from */
static const char *net_link_name(void)
{
    if (ecm_is_up()) return "ETH";
    if (usbnet_is_up()) return "iPHONE";
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
    if (wifi_get_ip()) return "WIFI";
#endif
    return "-";
}

static void net_read(net_data_t *d)
{
    d->link      = net_link_name();
    d->ip        = net_get_ip();
    d->gw        = net_get_gw();
    d->dns       = net_get_dns();
    d->connected = d->ip != 0;
}

/* ------------------------------------------------------------- weather --- */
typedef struct {
    const struct weather_data *w;   /* w->valid: a report has arrived */
    uint8_t fetching;
} wx_data_t;

static void wx_read(wx_data_t *d)
{
    d->w        = weather_get();
    d->fetching = weather_fetching() ? 1 : 0;
}
