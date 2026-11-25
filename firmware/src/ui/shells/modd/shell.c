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

/* ------------------------------------------------------------- input --- */
static void shell_knob(uint32_t id, int32_t clicks)
{
    const modd_page_t *p = nav_active();
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

    if (id == ENC_SELECT && modd_controls.select_default == CTL_SELECT_PAGE && clicks)
        nav_cycle(clicks > 0 ? 1 : -1);

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
    }
    if (!down && lvt_btn_held == id + 1u) {
        uint32_t held_us = fm1_micros() - lvt_btn_us;
        lvt_btn_held = 0;
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
