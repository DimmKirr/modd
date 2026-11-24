// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
// Origin: upstream Felucca ui_input.c + ui_layer.c + ui.c (control flow)
/* Felucca LVGL shell: layer gestures, family cycling, transport.
 *
 * Replaces the modd shell's simple tap/hold dispatch with the upstream
 * Felucca behavior: six layers (FX = HOLD, GLO/SCL/EDIT/SEQ/REC = SET),
 * same-button family cycling (open_family), HOME→MIXER cycling, and
 * double-tap layer lock. Rendering stays LVGL (chrome.c, page files). */

/* ---------------------------------------------------------------- layer --- */

#define LK_HOLD 0
#define LK_SET  1

#define LAYER_NONE 0
#define LAYER_FX   1
#define LAYER_GLO  2
#define LAYER_SCL  3
#define LAYER_EDIT 4
#define LAYER_SEQ  5
#define LAYER_REC  6
#define LAYER_N    7

#define FSH_HOLD_US  500000u
#define FSH_DTAP_MS  300u
#define FSH_QUIET_MS 250u

#define LY_FLAG_OPEN  2u
#define LY_FLAG_COMBO 4u

typedef struct {
    uint8_t    btn;
    uint8_t    kind;
    uint8_t    view;
    const char *name;
} fsh_layer_def_t;

static const fsh_layer_def_t FSH_LAYERS[LAYER_N] = {
    {0,        0,       VIEW_NONE, 0},
    {BTN_FX,   LK_HOLD, VIEW_FX,   "FX HOLD"},
    {BTN_GLO,  LK_SET,  VIEW_GLO,  "GLO SET"},
    {BTN_SCL,  LK_SET,  VIEW_SCL,  "SCL SET"},
    {BTN_EDIT, LK_SET,  VIEW_EDIT, "EDIT SET"},
    {BTN_SEQ,  LK_SET,  VIEW_SEQ,  "SEQ TOOLS"},
    {BTN_REC,  LK_SET,  VIEW_SEQ,  "REC SET"},
};

static struct {
    uint8_t  ly;
    uint32_t ly_us;
    uint8_t  ly_flags;
    uint8_t  lock;

    uint8_t  dt_l;
    uint32_t dt_ms;

    uint32_t quiet_end;
} fsh;

static uint32_t lvt_dbg_idx = 0xFFFFFFFFu;
static uint32_t lvt_dbg_return;
static uint32_t lvt_draw_max_ms;

/* which LAYER_* does this button arm (0 = not a layer button) */
static uint8_t fsh_btn_layer(uint32_t btn)
{
    uint32_t i;
    for (i = 1; i < LAYER_N; i++)
        if (FSH_LAYERS[i].btn == (uint8_t)btn)
            return (uint8_t)i;
    return LAYER_NONE;
}

static int fsh_is_layer_btn_down(uint32_t l)
{
    return l && l < LAYER_N && ((fm1_in.buttons >> FSH_LAYERS[l].btn) & 1u);
}

/* true while a layer is open (armed + passed threshold, or locked) */
static int fsh_layer_open(void)
{
    return (fsh.ly && (fsh.ly_flags & LY_FLAG_OPEN)) || fsh.lock;
}

#ifdef CONFIG_AUDIO
/* parameter snapshot taken when a layer opens; OCT- reverts to it */
static int16_t layer_snap[P_COUNT];
static int16_t layer_gsnap[G_COUNT];
static uint8_t layer_snap_trk;
static uint8_t layer_snap_valid;

static void fsh_snap_take(void)
{
    layer_snap_trk = (uint8_t)song.sel;
    memcpy(layer_snap, trk[layer_snap_trk].p, sizeof layer_snap);
    memcpy(layer_gsnap, song.g, sizeof layer_gsnap);
    layer_snap_valid = 1;
}

static int fsh_snap_undo(void)
{
    if (!layer_snap_valid)
        return 0;
    memcpy(trk[layer_snap_trk].p, layer_snap, sizeof layer_snap);
    memcpy(song.g, layer_gsnap, sizeof layer_gsnap);
    toast_status("UNDO");
    nav_dirty = 1;
    return 1;
}
#endif

static void fsh_layer_let_go(void)
{
#ifdef CONFIG_AUDIO
    layer_snap_valid = 0;
#endif
    if (fsh.ly_flags & LY_FLAG_OPEN)
        fsh.quiet_end = fm1_ms + FSH_QUIET_MS;
    fsh.ly = LAYER_NONE;
    fsh.ly_us = 0;
    fsh.ly_flags = 0;
}

/* per-frame: open the armed layer once the hold threshold passes */
static void fsh_gesture_tick(void)
{
    uint32_t now, l, i;
    int combo;

    if (!fsh.ly || (fsh.ly_flags & LY_FLAG_OPEN))
        return;

    l = fsh.ly;
    if (!fsh_is_layer_btn_down(l)) {
        fsh.ly = LAYER_NONE;
        fsh.ly_us = 0;
        fsh.ly_flags = 0;
        return;
    }

    now = fm1_micros();

    combo = 0;
    for (i = 1; i < LAYER_N; i++) {
        if (i != l && ((fm1_in.buttons >> FSH_LAYERS[i].btn) & 1u)) {
            combo = 1;
            break;
        }
    }

    if (combo || (now - fsh.ly_us >= FSH_HOLD_US)) {
        fsh.ly_flags |= LY_FLAG_OPEN;
        if (combo)
            fsh.ly_flags |= LY_FLAG_COMBO;
        toast_status(FSH_LAYERS[l].name);
    }
}

/* ---------------------------------------------------------- open_family --- */

static void fsh_open_family(uint8_t view)
{
    if (view >= VIEW_COUNT)
        return;
    nav_open_view(view);
}

/* HOME→MIXER→(CLOCK)→HOME */
static void fsh_home_tap(void)
{
    if (nav_view == VIEW_HOME) {
        uint32_t next = nav_find_in_view(VIEW_HOME, nav_cur, 1);
        if (next != nav_cur) {
            nav_set_page(next);
            return;
        }
    }
    nav_open_view(VIEW_HOME);
}

/* layer tap: open the family, or cycle if already on that family */
static void fsh_layer_tap(uint32_t l)
{
    uint8_t view = FSH_LAYERS[l].view;
    if (view < VIEW_COUNT)
        fsh_open_family(view);

    if (fsh.dt_l == (uint8_t)l && fm1_ms - fsh.dt_ms < FSH_DTAP_MS) {
        fsh.lock = (uint8_t)l;
        fsh.dt_l = LAYER_NONE;
        toast_status("LOCKED");
    } else {
        fsh.dt_l = (uint8_t)l;
        fsh.dt_ms = fm1_ms;
    }
}

/* ---------------------------------------------------------- shell entry --- */

static void shell_draw(void)
{
    const modd_page_t *p;
    nav_take_request();
    p = nav_active();
    sched_mark(10);

    if (lvt_pal.trk != (song.sel & 3u)) {
        lvt_pal.trk = (uint8_t)(song.sel & 3u);
        lvt_pal.primary = lvt_pal.trk_c[lvt_pal.trk];
        lvt_pal_apply();
    }

    fsh_gesture_tick();

    if (p && p->status)
        p->status();
    lvt_head_poll();
    lvt_hints_poll(p);
    {
        static const char *const LN[LAYER_N] = { 0, "FX SET", "GLOBAL", "SCALE SET", "EDIT SET", "SEQ TOOLS", "REC ARM" };
        uint32_t l;
        for (l = 1; l < LAYER_N; l++)
            if (fsh_is_layer_btn_down(l)) {
                lvt_footer("", LN[l], "");
                break;
            }
    }
    lvt_toast_tick();
    sched_mark(13);
    {
        uint32_t t0 = fm1_ms, ms;
        lv_timer_handler();
        ms = fm1_ms - t0;
        if (ms > lvt_draw_max_ms)
            lvt_draw_max_ms = ms;
    }
    nav_dirty = 0;
}


/* -------------------------------------------------- layer knobs and keys --- */

#ifdef CONFIG_AUDIO
typedef struct { uint8_t pid; int16_t mn, mx; uint8_t wrap; } fsh_lk_t;

static int fsh_layer_param(const fsh_lk_t *m, track_t *t, int32_t clicks)
{
    int16_t v = modd_tp(t, m->pid) + (int16_t)clicks;
    if (v < m->mn) v = m->wrap ? m->mx : m->mn;
    if (v > m->mx) v = m->wrap ? m->mn : m->mx;
    modd_tp_set(t, m->pid, v);
    nav_dirty = 1;
    return 1;
}

/* K1..K4 while a layer button is held; 1 = handled */
static int fsh_layer_knob(uint32_t id, int32_t clicks)
{
    static const fsh_lk_t SCL_K[4] = {
        { P_ROOT, 0, 11, 1 }, { P_SCALE, 0, 11, 1 },
        { P_CHRD, 0, 127, 0 }, { P_VOIC, 0, 127, 0 } };
    static const fsh_lk_t SEQ_K[4] = {
        { P_SLEN, 1, NSTEP, 0 }, { P_SDIV, 0, 7, 0 },
        { P_SSWING, 0, 100, 0 }, { P_SGATE, 0, 100, 0 } };
    track_t *t = &trk[song.sel];
    uint32_t k;
    if (id < ENC_K1 || id > ENC_K4 || !clicks)
        return 0;
    k = id - ENC_K1;
    if (fsh_is_layer_btn_down(LAYER_EDIT)) {
        fsh_lk_t m = { (uint8_t)(P_E0 + k), 0, 127, 0 };
        return fsh_layer_param(&m, t, clicks);
    }
    if (fsh_is_layer_btn_down(LAYER_SCL))
        return fsh_layer_param(&SCL_K[k], t, clicks);
    if (fsh_is_layer_btn_down(LAYER_SEQ))
        return fsh_layer_param(&SEQ_K[k], t, clicks);
    return 0;
}

/* keys while a layer button is held; 1 = handled (note-offs are swallowed too) */
static int fsh_layer_key(uint32_t note, uint32_t vel)
{
    static const uint8_t SENDS[4] = { P_DIST, P_CHOR, P_DLY, P_REV };
    static int16_t send_stash[4];
    static uint8_t send_muted;
    uint32_t k = note % 12u, i;
    track_t *t = &trk[song.sel];
    int any = 0;

    for (i = 1; i < LAYER_N; i++)
        any |= fsh_is_layer_btn_down(i);
    if (!any)
        return 0;
    if (!vel)
        return 1;

    if (fsh_is_layer_btn_down(LAYER_EDIT)) {
        if (NENGINES) t->eng_req = (uint8_t)(k % NENGINES);
    } else if (fsh_is_layer_btn_down(LAYER_SCL)) {
        t->p[P_ROOT] = (int16_t)k;
    } else if (fsh_is_layer_btn_down(LAYER_SEQ)) {
        if (k == 0) {
            memset(t->step, 0, sizeof t->step);          /* clear */
        } else if (k == 1) {
            track_t *d = &trk[(song.sel + 1u) % NTRK];   /* copy to next track */
            if (d != t) memcpy(d->step, t->step, sizeof t->step);
        }
    } else if (fsh_is_layer_btn_down(LAYER_FX)) {
        if (k < 4u) {                                    /* mute / restore one send */
            uint32_t b = 1u << k;
            if (send_muted & b) { t->p[SENDS[k]] = send_stash[k]; send_muted &= (uint8_t)~b; }
            else { send_stash[k] = t->p[SENDS[k]]; t->p[SENDS[k]] = 0; send_muted |= (uint8_t)b; }
        } else if (k - 4u < NTRK) {                      /* solo track */
            uint32_t s = k - 4u, j;
            int solo = 1;
            for (j = 0; j < NTRK; j++)
                if (j != s && !trk[j].p[P_MUTE]) solo = 0;
            for (j = 0; j < NTRK; j++)
                trk[j].p[P_MUTE] = (j == s || solo) ? 0 : 1;
        }
    } else if (fsh_is_layer_btn_down(LAYER_GLO)) {
        if (k == 0) seq_toggle();                        /* play / stop */
        else if (k == 1) seq_stop();
        else if (k == 2) song.rec = song.rec ? 0u : (1u << (song.sel & 3u));
    } else if (fsh_is_layer_btn_down(LAYER_REC)) {
        if (k < 4u) song.rec ^= (uint8_t)(1u << k);      /* arm / disarm track */
    }
    nav_dirty = 1;
    return 1;
}
#endif

/* ------------------------------------------------------------- knob --- */

static void shell_knob(uint32_t id, int32_t clicks)
{
    const modd_page_t *p;

    if (fsh.quiet_end && fm1_ms < fsh.quiet_end)
        return;
    fsh.quiet_end = 0;

#ifdef CONFIG_DOCK
    p = nav_active();
    if (dock_active && p && p->view == VIEW_HOME && id >= ENC_K1 && id <= ENC_K4) {
        dock_enc(id, clicks);
        return;
    }
#endif

#ifdef CONFIG_AUDIO
    if (fsh_is_layer_btn_down(LAYER_GLO) && id == ENC_SELECT && clicks) {
        int16_t bpm = song.g[G_BPM] + (int16_t)clicks;
        if (bpm < 20) bpm = 20;
        if (bpm > 300) bpm = 300;
        song.g[G_BPM] = bpm;
        nav_dirty = 1;
        return;
    }
#endif

#ifdef CONFIG_AUDIO
    if (fsh_layer_knob(id, clicks))
        return;
#endif

    p = nav_active();
    if (p && p->knob && p->knob(id, clicks))
        return;

    if (p && p->knob_map) {
        uint32_t i;
        for (i = 0; i < p->knob_count; i++) {
            const param_slot_t *s = &p->knob_map[i];
            if (s->enc_id != (uint8_t)id || !MODD_P_VALID(s->pid))
                continue;
            track_t *t = (s->flags & SLOT_TRACK)
                ? &trk[i < NTRK ? i : 0]
                : &trk[song.sel];
            int16_t v = modd_tp(t, s->pid) + (int16_t)(clicks * s->step);
            if (s->flags & SLOT_WRAP) {
                if (v < s->min) v = s->max;
                if (v > s->max) v = s->min;
            } else {
                if (v < s->min) v = s->min;
                if (v > s->max) v = s->max;
            }
            modd_tp_set(t, s->pid, v);
            nav_dirty = 1;
            return;
        }
    }

    if (id == ENC_SELECT && modd_controls.select_default == CTL_SELECT_PAGE && clicks)
        nav_cycle(clicks > 0 ? 1 : -1);
}

/* ----------------------------------------------------------- button --- */

static void shell_button(uint32_t id, uint32_t down)
{
    const modd_page_t *p;
    uint8_t l;

    if (id >= BTN_COUNT)
        return;

    if (toast.kind == TOAST_ERROR && !down) {
        toast_clear();
        return;
    }

    l = fsh_btn_layer(id);

    /* ---- press ---- */
    if (down) {
        if (l && !fsh.ly && !fsh.lock) {
#ifdef CONFIG_AUDIO
            fsh_snap_take();
#endif
            fsh.ly = l;
            fsh.ly_us = fm1_micros();
            fsh.ly_flags = 0;
        }
        goto page_handler;
    }

    /* ---- release ---- */

    /* locked layer: its own button unlocks, other layer buttons close+unlock */
    if (fsh.lock) {
        if (l == fsh.lock) {
            fsh.lock = LAYER_NONE;
#ifdef CONFIG_AUDIO
            layer_snap_valid = 0;
#endif
            toast_status("UNLOCKED");
            return;
        }
        if (l) {
            fsh.lock = LAYER_NONE;
        }
    }

    /* armed layer release */
    if (fsh.ly == l && l) {
        if (fsh.ly_flags & LY_FLAG_OPEN) {
            fsh_layer_let_go();
            toast_clear();
        } else {
            uint8_t armed = fsh.ly;
            fsh_layer_let_go();
            fsh_layer_tap(armed);
        }
        return;
    }

    /* HOME: tap cycles HOME family, hold opens SETTINGS */
    if (id == BTN_HOME) {
        uint32_t held_us = fm1_micros() - fm1_input_btn_press_us(id);
        if (held_us > FSH_HOLD_US) {
            nav_open_view(VIEW_SETTINGS);
        } else {
            fsh_home_tap();
        }
        return;
    }

    /* SAVE: tap opens SAVE family */
    if (id == BTN_SAVE) {
        fsh_open_family(VIEW_SAVE);
        return;
    }

#ifdef CONFIG_AUDIO
    /* PLAY: start/stop transport */
    if (id == BTN_PLAY) {
        if (fsh_layer_open())
            return;
        seq_toggle();
        nav_dirty = 1;
        return;
    }

    /* REC: toggle recording arm */
    if (id == BTN_REC && !fsh.ly) {
        song.rec = song.rec ? 0u : (1u << (song.sel & 3u));
        nav_dirty = 1;
        return;
    }

    /* OCT±: octave shift */
    if (id == BTN_OCTDN || id == BTN_OCTUP) {
        int dir = (id == BTN_OCTUP) ? 1 : -1;
        if (id == BTN_OCTDN && (fsh.ly || fsh.lock) && fsh_snap_undo())
            return;
        int16_t oct = modd_tp(&trk[song.sel], P_OCT) + (int16_t)dir;
        if (oct < -3) oct = -3;
        if (oct > 3) oct = 3;
        if ((fm1_in.buttons >> BTN_OCTDN) & 1u &&
            (fm1_in.buttons >> BTN_OCTUP) & 1u)
            oct = 0;
        modd_tp_set(&trk[song.sel], P_OCT, oct);
        nav_dirty = 1;
        return;
    }
#endif

    /* non-layer page buttons (ENV, LFO, ARP): tap opens the family */
    {
        static const uint8_t PAGE_BTN_VIEW[BTN_COUNT] = {
            [BTN_OCTDN] = VIEW_NONE, [BTN_OCTUP] = VIEW_NONE,
            [BTN_FX]    = VIEW_NONE, [BTN_SCL]   = VIEW_NONE,
            [BTN_ENV]   = VIEW_ENV,  [BTN_LFO]   = VIEW_LFO,
            [BTN_EDIT]  = VIEW_NONE, [BTN_GLO]   = VIEW_NONE,
            [BTN_HOME]  = VIEW_NONE, [BTN_SAVE]  = VIEW_NONE,
            [BTN_ARP]   = VIEW_ARP,  [BTN_SEQ]   = VIEW_NONE,
            [BTN_PLAY]  = VIEW_NONE, [BTN_REC]   = VIEW_NONE,
        };
        if (PAGE_BTN_VIEW[id] != VIEW_NONE) {
            fsh_open_family(PAGE_BTN_VIEW[id]);
            return;
        }
    }

page_handler:
    p = nav_active();
#ifdef CONFIG_DOCK
    if (dock_active && p && p->view == VIEW_HOME && (id == BTN_OCTDN || id == BTN_OCTUP)) {
        dock_btn(id, down);
        return;
    }
#endif
    if (p && p->button)
        p->button(id, down);
}

static void shell_key(uint32_t note, uint32_t vel)
{
    const modd_page_t *p;
#ifdef CONFIG_DOCK
    if (dock_active) {
        if (note >= 53u && note < 53u + 27u)
            dock_key(note - 53u, vel != 0);
        return;
    }
#endif
#ifdef CONFIG_AUDIO
    if (fsh_layer_key(note, vel))
        return;
#endif
    p = nav_active();
    if (p && p->key)
        p->key(note, vel);
}

/* -------------------------------------------------------------- init --- */
static void shell_init(void)
{
    uint32_t pal = UI_DEFAULT_INDEX;
#ifndef CONFIG_AUDIO
    modd_mock_init();
#endif
    nav_init();
#ifdef CONFIG_SYS_MENU
    pal = lvt_pal_from_stored(sys_settings.ui_pal);
    modd_lights = sys_settings.ui_lights < LIGHTS_N ? sys_settings.ui_lights : LIGHTS_HIGH;
#endif
    lvt_pal.trk = (uint8_t)(song.sel & 3u);
    lvt_pal_set(pal);
    lvt_port_init();
    lvt_styles_init();
    lvt_theme_apply();
    lvt_hints_init();
    lvt_toast_init();
}

static void shell_boot(void)
{
    nav_boot();
}
