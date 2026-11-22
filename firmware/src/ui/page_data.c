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
 *   wx_read()        the weather report
 *   env_read()       ADSR envelope of the selected track
 *   lfo_read()       LFO parameters of the selected track
 *   fx_read()        FX send levels (dist, chorus, delay, reverb) of the selected track
 *   voice_read()     voice/hold/glide/unison of the selected track
 *   mixer_read()     level and pan for all tracks
 *   arp_read()       arpeggiator settings of the selected track
 *   scl_read()       scale/root/octave/transpose of the selected track
 *   edit_read()      engine-specific params (P_E0..P_E7) of the selected track
 *   about_read()     firmware name, version and uptime */

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

/* ----------------------------------------------------------------- env --- */
typedef struct {
    int16_t atk, dec, sus, rel;
} env_data_t;

static void env_read(env_data_t *d)
{
    const track_t *t = &trk[song.sel];
    d->atk = t->p[P_ATK];
    d->dec = t->p[P_DEC];
    d->sus = t->p[P_SUS];
    d->rel = t->p[P_REL];
}

/* ----------------------------------------------------------------- lfo --- */
typedef struct {
    int16_t rate, depth, shape, delay;
} lfo_data_t;

static void lfo_read(lfo_data_t *d)
{
    const track_t *t = &trk[song.sel];
    d->rate  = t->p[P_LFRT];
    d->depth = t->p[P_LFO];
    d->shape = t->p[P_LFSH];
    d->delay = t->p[P_LFDL];
}

/* ------------------------------------------------------------------ fx --- */
typedef struct {
    int16_t dist, chorus, delay, reverb;
} fx_data_t;

static void fx_read(fx_data_t *d)
{
    const track_t *t = &trk[song.sel];
    d->dist   = t->p[P_DIST];
    d->chorus = t->p[P_CHOR];
    d->delay  = t->p[P_DLY];
    d->reverb = t->p[P_REV];
}

/* --------------------------------------------------------------- voice --- */
typedef struct {
    int16_t voices, hold, glide, unison;
} voice_data_t;

static void voice_read(voice_data_t *d)
{
    const track_t *t = &trk[song.sel];
    d->voices = t->p[P_MONO];
    d->hold   = t->p[P_HOLD];
    d->glide  = t->p[P_GLI];
    d->unison = t->p[P_UNI];
}

/* --------------------------------------------------------------- mixer --- */
typedef struct {
    int16_t level[NTRK];
    int16_t pan[NTRK];
    uint8_t selected;
} mixer_data_t;

static void mixer_read(mixer_data_t *d)
{
    uint32_t k;
    d->selected = song.sel;
    for (k = 0; k < NTRK; k++) {
        d->level[k] = trk[k].p[P_LEVEL];
        d->pan[k]   = trk[k].p[P_PAN];
    }
}

/* ----------------------------------------------------------------- arp --- */
typedef struct {
    int16_t mode, tempo, range, gate;
} arp_data_t;

static void arp_read(arp_data_t *d)
{
    const track_t *t = &trk[song.sel];
    d->mode  = t->p[P_ARP];
    d->tempo = t->p[P_ARTM];
    d->range = t->p[P_ARNG];
    d->gate  = t->p[P_AGATE];
}

/* ----------------------------------------------------------------- scl --- */
typedef struct {
    int16_t root, scale, octave, transpose;
} scl_data_t;

static void scl_read(scl_data_t *d)
{
    const track_t *t = &trk[song.sel];
    d->root      = t->p[P_ROOT];
    d->scale     = t->p[P_SCALE];
    d->octave    = t->p[P_OCT];
    d->transpose = t->p[P_TRANS];
}

/* ---------------------------------------------------------------- edit --- */
typedef struct {
    int16_t e[8];
    const char *engine_name;
    uint8_t engine_id;
    uint8_t preset;
} edit_data_t;

static void edit_read(edit_data_t *d)
{
    const track_t *t = &trk[song.sel];
    uint32_t k;
    for (k = 0; k < 8u; k++)
        d->e[k] = t->p[P_E0 + k];
    d->engine_id = t->engine;
    d->engine_name = (t->engine < NENGINES && ENGINES[t->engine]->name)
                     ? ENGINES[t->engine]->name : "---";
    d->preset = t->preset;
}

/* --------------------------------------------------------------- about --- */
typedef struct {
    const char *fw_name;
    const char *fw_version;
    uint32_t uptime_s;
} about_data_t;

static void about_read(about_data_t *d)
{
    d->fw_name    = CONFIG_FIRMWARE_NAME;
    d->fw_version = CONFIG_FIRMWARE_VERSION;
    d->uptime_s   = fm1_ms / 1000u;
}
