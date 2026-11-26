// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd shell: the entry points modd.c's main loop calls (the UI_SHELL set).
 *
 * Navigation is the shared ui_nav.c and the page contract is modd_page.h, (the
 * same for every shell). A page builds its widgets in .enter and LVGL keeps
 * them drawn, so .draw is unused: shell_draw() lets the page update its widgets (.status),
 * follows the palette and the selected track, the header icons, the hints and the toasts,
 * then runs LVGL's timers (rendering, animations).
 *
 * Input:
 *   knob     the page's .controls map; else SELECT goes to the next/previous page of the view
 *   button   an error toast takes the next release; a hold opens a view (controls.c), GLO
 *            held opens LOG and returns; a GLO tap shows/hides the hints; then the page's
 *            handler; then a tap opens a view
 *   key      the page's handler
 * With a dock module running (CONFIG_DOCK), HOME's K1-K4, OCT-/OCT+ and the keys are the
 * module's. */

static uint32_t lvt_dbg_idx = 0xFFFFFFFFu;  /* LOG's page index (CONFIG_DEBUG_LOG_BUF) ... */
static uint32_t lvt_dbg_return;         /* ... and where GLO returns to */
static uint32_t lvt_btn_held;           /* id + 1 of the button going down, 0: none */
static uint32_t lvt_btn_us;
static uint32_t lvt_draw_max_ms;        /* the longest lv_timer_handler() call */

static void shell_draw(void)
{
    const modd_page_t *p;
    nav_take_request();                 /* a page asked for by the REST API: built here, in the UI task */
    p = nav_active();
    sched_mark(10);                     /* page status, chrome, toast */
    if (lvt_pal.trk != (modd_sel() & 3u)) {   /* THEME is the selected track's colour */
        lvt_pal.trk = (uint8_t)(modd_sel() & 3u);
        lvt_pal.primary = lvt_pal.trk_c[lvt_pal.trk];
        lvt_pal_apply();
    }
    if (p && p->status)
        p->status();
    lvt_head_poll();
    lvt_hints_poll(p);
    lvt_toast_tick();
    sched_mark(13);                     /* LVGL: draw what changed */
    {
        uint32_t t0 = fm1_ms, ms;
        lv_timer_handler();
        ms = fm1_ms - t0;
        if (ms > lvt_draw_max_ms)
            lvt_draw_max_ms = ms;
    }
    nav_dirty = 0;
}

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
/* Layer overlays (upstream ui_layer.c): while a layer button is held (or
 * locked via double-tap), K1-K4 remap to the layer's knobs and keys are
 * silenced (layer-specific actions on SCL and GLO keys).
 *
 * GLO:  K1-K4 = T1-T4 LEVEL; keys: black = T1-T4 mute toggle
 * SCL:  K1-K4 = ROOT / SCALE / CHRD / VOIC; keys: set ROOT
 * SEQ:  K1-K4 = SLEN / SDIV / SSWING / SGATE
 * EDIT: K1 = cycle engine, K2 = cycle preset
 * FX:   K1-K4 = perform macros (needs perform.c port)
 * REC:  K1-K4 = CLEAR / CLICK / COUNT-IN / CLICK LEVEL (needs click.c port) */
static uint32_t shell_layer_btn;
static uint32_t shell_layer_lock;
static uint32_t shell_layer_tap_ms;
static uint32_t shell_layer_tap_btn;
#define SHELL_DTAP_MS 300u

static int shell_layer_knob(uint32_t id, int32_t clicks)
{
    uint32_t k;
    uint32_t btn = shell_layer_btn ? shell_layer_btn : shell_layer_lock;
    if (!btn || id < ENC_K1 || id > ENC_K4)
        return 0;
    k = id - ENC_K1;
    if (btn == BTN_GLO + 1u) {
        track_t *t = modd_trk(k < NTRK ? k : 0);
        int16_t v = t->p[P_LEVEL] + (int16_t)clicks;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        t->p[P_LEVEL] = v;
        nav_dirty = 1;
        return 1;
    }
    if (btn == BTN_SCL + 1u) {
        static const uint8_t SCL_LAYER[4] = { P_ROOT, P_SCALE, P_CHRD, P_VOIC };
        track_t *t = modd_trk(modd_sel());
        uint32_t pid = SCL_LAYER[k];
        const param_desc_t *d = &TP[pid];
        int16_t v = t->p[pid] + (int16_t)clicks;
        if (v < d->min) v = d->min;
        if (v > d->max) v = d->max;
        t->p[pid] = v;
        nav_dirty = 1;
        return 1;
    }
    if (btn == BTN_SEQ + 1u) {
        static const uint8_t SEQ_LAYER[4] = { P_SLEN, P_SDIV, P_SSWING, P_SGATE };
        track_t *t = modd_trk(modd_sel());
        uint32_t pid = SEQ_LAYER[k];
        const param_desc_t *d = &TP[pid];
        int16_t v = t->p[pid] + (int16_t)clicks;
        if (v < d->min) v = d->min;
        if (v > d->max) v = d->max;
        t->p[pid] = v;
        nav_dirty = 1;
        return 1;
    }
    if (btn == BTN_EDIT + 1u) {
        track_t *t = modd_trk(modd_sel());
        if (k == 0) {
            uint32_t ei = (t->eng_req + (clicks > 0 ? 1u : NENGINES - 1u)) % NENGINES;
            modd_engine_select(t, ei);
        } else if (k == 1) {
            modd_preset_step(clicks);
        }
        nav_dirty = 1;
        return 1;
    }
    if (btn == BTN_FX + 1u) {
        int32_t v = (int32_t)perf_k[k] + clicks;
        int32_t lo = (k == 0) ? -100 : 0;
        if (v < lo) v = lo;
        if (v > 100) v = 100;
        perf_k[k] = (int8_t)v;
        nav_dirty = 1;
        return 1;
    }
    if (btn == BTN_REC + 1u && k < 3u) {
        static const uint8_t REC_MAX[] = {2, 2, 2};
        uint8_t *dst = k == 0 ? &rec_click : k == 1 ? &rec_countin : &rec_level;
        int32_t v = (int32_t)*dst + (clicks > 0 ? 1 : -1);
        if (v < 0) v = (int32_t)REC_MAX[k];
        if (v > (int32_t)REC_MAX[k]) v = 0;
        *dst = (uint8_t)v;
        nav_dirty = 1;
        return 1;
    }
    return 0;
}
#endif

/* ------------------------------------------------------------- input --- */
static void shell_knob(uint32_t id, int32_t clicks)
{
    const modd_page_t *p = nav_active();
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
    if (shell_layer_knob(id, clicks))
        return;
#endif
#ifdef CONFIG_DOCK
    /* on HOME the four knobs are the module's (its sound); the rest stays modd's navigation */
    if (dock_active && p && p->view == VIEW_HOME && id >= ENC_K1 && id <= ENC_K4) {
        dock_enc(id, clicks);
        return;
    }
#endif
    if (p && p->controls) {
        uint32_t i;
        for (i = 0; i < p->ncontrols; i++) {
            const ctl_slot_t *s = &p->controls[i];
            if (s->enc_id != (uint8_t)id)
                continue;
            if (s->action) {
                if (s->action(clicks))
                    nav_dirty = 1;
                return;
            }
            if ((s->flags & CTLF_GLOBAL) ? (s->pid >= G_COUNT) : !MODD_P_VALID(s->pid))
                continue;
            track_t *t = NULL;
            int16_t v;
            if (s->flags & CTLF_GLOBAL) {
                v = modd_gp(s->pid);
            } else {
                t = (s->flags & CTLF_TRACK)
                    ? modd_trk(i < NTRK ? i : 0)
                    : modd_trk(modd_sel());
                v = modd_param(t, s->pid);
            }
            v += (int16_t)(clicks * s->step);
            if (s->flags & CTLF_WRAP) {
                if (v < s->min) v = s->max;
                if (v > s->max) v = s->min;
            } else {
                if (v < s->min) v = s->min;
                if (v > s->max) v = s->max;
            }
            if (s->flags & CTLF_GLOBAL)
                modd_gp_set(s->pid, v);
            else
                modd_param_set(t, s->pid, v);
            nav_dirty = 1;
            return;
        }
    }

    if (id == ENC_SELECT && clicks) {
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
        uint32_t smode = sys_settings.sel_mode;
        static const uint8_t SEL_MAP[] = {
            CTL_SELECT_BPM, CTL_SELECT_PAGE, CTL_SELECT_MODW, CTL_SELECT_VEL
        };
        uint32_t sel = smode < 4u ? SEL_MAP[smode] : CTL_SELECT_BPM;
#else
        uint32_t sel = modd_controls.select_default;
#endif
        if (sel == CTL_SELECT_PAGE) {
            nav_cycle(clicks > 0 ? 1 : -1);
        } else if (sel == CTL_SELECT_BPM
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
                   && (!sys_settings.bpm_lock || shell_layer_btn == BTN_GLO + 1u
                       || shell_layer_lock == BTN_GLO + 1u)
#endif
                   ) {
            int32_t bpm = modd_gp(G_BPM) + clicks;
            if (bpm < 20) bpm = 20;
            if (bpm > 300) bpm = 300;
            modd_gp_set(G_BPM, (int16_t)bpm);
            nav_dirty = 1;
        }
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
        else if (sel == CTL_SELECT_MODW) {
            track_t *t = modd_trk(modd_sel());
            int32_t mw = (int32_t)t->mw + clicks;
            if (mw < 0) mw = 0;
            if (mw > 127) mw = 127;
            t->mw = (uint8_t)mw;
            nav_dirty = 1;
        } else if (sel == CTL_SELECT_VEL) {
            static uint8_t kb_vel_local = 100;
            int32_t v = (int32_t)kb_vel_local + clicks;
            if (v < 1) v = 1;
            if (v > 127) v = 127;
            kb_vel_local = (uint8_t)v;
            nav_dirty = 1;
        }
#endif
    }

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
    if (id == ENC_ALGO && clicks) {
        modd_track_step(clicks);
        nav_dirty = 1;
    }
    if (id == ENC_PRESET && clicks) {
        modd_preset_step(clicks);
        nav_dirty = 1;
    }
#endif
}

static void shell_button(uint32_t id, uint32_t down)
{
    const modd_page_t *p;
    if (id >= BTN_COUNT)
        return;
    if (toast.kind == TOAST_ERROR && !down) {   /* the error is read: the release only clears it */
        toast_clear();
        return;
    }
    if (down) {
        lvt_btn_held = id + 1u;
        lvt_btn_us = fm1_input_btn_press_us(id);
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
        if (id == BTN_GLO || id == BTN_SCL || id == BTN_SEQ ||
            id == BTN_FX  || id == BTN_EDIT || id == BTN_REC) {
            if (shell_layer_lock == id + 1u) {
                shell_layer_lock = 0;
                shell_layer_btn = 0;
            } else if (shell_layer_tap_btn == id + 1u &&
                       fm1_ms - shell_layer_tap_ms < SHELL_DTAP_MS) {
                shell_layer_lock = id + 1u;
                shell_layer_btn = id + 1u;
                shell_layer_tap_btn = 0;
            } else {
                shell_layer_btn = id + 1u;
            }
        }
#endif
    }
    if (!down && lvt_btn_held == id + 1u) {
        uint32_t held_us = fm1_micros() - lvt_btn_us;
        lvt_btn_held = 0;
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
        if (shell_layer_btn == id + 1u) {
            shell_layer_btn = 0;
            if (!shell_layer_lock) {
                shell_layer_tap_btn = id + 1u;
                shell_layer_tap_ms = fm1_ms;
            }
        }
#endif
        if (held_us > modd_controls.hold_us) {
            if (id == BTN_GLO && lvt_dbg_idx < nav_npage) {
                if (nav_cur == lvt_dbg_idx) {
                    nav_set_page(lvt_dbg_return);
                } else {
                    lvt_dbg_return = nav_cur;
                    nav_set_page(lvt_dbg_idx);
                }
                return;
            }
            if (modd_controls.btn_hold[id] != VIEW_NONE) {
                nav_open_view(modd_controls.btn_hold[id]);
                return;
            }
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
            if (id == BTN_SAVE) {
                undo_swap();
                nav_dirty = 1;
                return;
            }
#endif
        }
    }
#ifndef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
    /* GLO is the shell's: a tap toggles the hints, the press and a hold never reach a page */
    if (id == BTN_GLO) {
        if (!down && lvt_btn_held == 0)
            hints_visible = !hints_visible;
        return;
    }
#endif
    p = nav_active();
#ifdef CONFIG_DOCK
    /* on HOME the octave buttons are the module's (a panel module shifts its own octave) */
    if (dock_active && p && p->view == VIEW_HOME && (id == BTN_OCTDN || id == BTN_OCTUP)) {
        dock_btn(id, down);
        return;
    }
#endif
    if (p && p->button && p->button(id, down))
        return;
    if (!down && modd_controls.btn_tap[id] != VIEW_NONE)
        nav_open_view(modd_controls.btn_tap[id]);
}

static void shell_key(uint32_t note, uint32_t vel)
{
    const modd_page_t *p;
#ifdef CONFIG_DOCK
    if (dock_active) {                          /* the module is the instrument while it runs */
        if (note >= 53u && note < 53u + 27u)
            dock_key(note - 53u, vel != 0);
        return;
    }
#endif
    p = nav_active();
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
    if (shell_layer_btn || shell_layer_lock) {
        uint32_t btn = shell_layer_btn ? shell_layer_btn : shell_layer_lock;
        if (vel && btn == BTN_SCL + 1u) {
            track_t *t = modd_trk(modd_sel());
            t->p[P_ROOT] = (int16_t)(note % 12u);
            nav_dirty = 1;
        }
        if (vel && btn == BTN_GLO + 1u) {
            static const uint32_t MUTE_KEYS[] = {54, 56, 58, 59};
            uint32_t mk;
            for (mk = 0; mk < 4u && mk < NTRK; mk++) {
                if (note == MUTE_KEYS[mk]) {
                    track_t *t = modd_trk(mk);
                    t->p[P_MUTE] = t->p[P_MUTE] ? 0 : 1;
                    nav_dirty = 1;
                    break;
                }
            }
            if (note == 65u) {
                uint32_t ti;
                for (ti = 0; ti < NTRK; ti++)
                    modd_trk(ti)->p[P_MUTE] = 0;
                nav_dirty = 1;
            }
        }
        if (vel && btn == BTN_EDIT + 1u) {
            uint32_t black = (note % 12u == 1 || note % 12u == 3 || note % 12u == 6 ||
                              note % 12u == 8 || note % 12u == 10);
            if (!black) {
                uint32_t oct = note / 12u, semi = note % 12u;
                static const uint8_t WHITE_MAP[] = {0, 0, 1, 0, 2, 3, 0, 4, 0, 5, 0, 6};
                uint32_t pos = (oct > 4 ? oct - 4 : 0) * 7u + WHITE_MAP[semi];
                if (pos < NENGINES) {
                    track_t *t = modd_trk(modd_sel());
                    modd_engine_select(t, pos);
                    nav_dirty = 1;
                }
            }
        }
        if (vel && btn == BTN_SEQ + 1u) {
            uint32_t black = (note % 12u == 1 || note % 12u == 3 || note % 12u == 6 ||
                              note % 12u == 8 || note % 12u == 10);
            if (!black && note == 53u) {
                track_t *t = modd_trk(modd_sel());
                uint32_t si, ni;
                for (si = 0; si < NSTEP; si++) {
                    for (ni = 0; ni < 4u; ni++) t->step[si].note[ni] = 0;
                    t->step[si].n = 0;
                    t->step[si].time = ST_REST;
                    t->step[si].flags = 0;
                    t->step[si].vel = 0;
                    t->step[si].hit = 0;
                }
                nav_dirty = 1;
            }
        }
        return;
    }
#endif
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
    lvt_pal.trk = (uint8_t)(modd_sel() & 3u);
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
