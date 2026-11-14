// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd UI shell: input dispatch and frame drawing for the modd theme.
 * Navigation is the shared ui_nav.c; chrome comes from theme.c via the element layer; pixels
 * come from Melodee's renderer (gfx.c: cv_* canvas, gfx_frame, the palette). */

/* ----------------------------------------------------------- drawing --- */

static void shell_draw(void)
{
    const modd_page_t *p = nav_active();
    if (!p)
        return;

    if (p->status)
        p->status();

    if (!nav_dirty)
        return;

    gfx_frame(1);
    gfx_fill(0, 0, MT_W, MT_H, T_BG);
    ui_header();
    if (p->draw)
        p->draw();
    if (nav_modal && nav_modal->draw)
        nav_modal->draw();
    gfx_frame(0);
    nav_dirty = 0;
}

/* ------------------------------------------------------------- input --- */

static void shell_knob(uint32_t id, int32_t clicks)
{
    if (nav_modal && nav_modal->knob && nav_modal->knob(id, clicks))
        return;
    const modd_page_t *p = nav_active();
#ifdef CONFIG_DOCK
    /* on HOME the four knobs are the module's (its sound); the rest stays modd's navigation */
    if (dock_active && p && p->view == VIEW_HOME && id >= ENC_K1 && id <= ENC_K4) {
        dock_enc(id, clicks);
        return;
    }
#endif
    if (p && p->knob && p->knob(id, clicks))
        return;
    if (id == ENC_SELECT && modd_controls.select_default == CTL_SELECT_PAGE) {
        if (clicks > 0) nav_cycle(1);
        else if (clicks < 0) nav_cycle(-1);
    }
}

#ifdef CONFIG_DEBUG_LOG
static uint32_t nav_dbg_idx;
static uint32_t nav_dbg_return;
#endif

static uint32_t shell_btn_held;
static uint32_t shell_btn_us;

static void shell_button(uint32_t id, uint32_t down)
{
    if (id >= BTN_COUNT) return;
    if (nav_modal && nav_modal->button && nav_modal->button(id, down))
        return;
    if (down) {
        shell_btn_held = id + 1u;
        shell_btn_us = fm1_input_btn_press_us(id);
    }
    if (!down && shell_btn_held == id + 1u) {
        uint32_t held_us = fm1_micros() - shell_btn_us;
        shell_btn_held = 0;
        if (held_us > modd_controls.hold_us &&
            modd_controls.btn_hold[id] != VIEW_NONE) {
            nav_open_view(modd_controls.btn_hold[id]);
            return;
        }
    }
    const modd_page_t *p = nav_active();
#ifdef CONFIG_DOCK
    /* on HOME the octave buttons are the module's (a panel module shifts its own octave) */
    if (dock_active && p && p->view == VIEW_HOME && (id == BTN_OCTDN || id == BTN_OCTUP)) {
        dock_btn(id, down);
        return;
    }
#endif
    if (p && p->button && p->button(id, down))
        return;
#ifdef CONFIG_DEBUG_LOG
    if (!down && id == BTN_GLO) {
        if (nav_cur == nav_dbg_idx) {
            nav_set_page(nav_dbg_return);
        } else {
            nav_dbg_return = nav_cur;
            nav_set_page(nav_dbg_idx);
        }
        return;
    }
#endif
    if (!down && modd_controls.btn_tap[id] != VIEW_NONE) {
        nav_open_view(modd_controls.btn_tap[id]);
    }
}

static void shell_key(uint32_t note, uint32_t vel)
{
#ifdef CONFIG_DOCK
    if (dock_active) {                          /* the module is the instrument while it runs */
        if (note >= 53u && note < 53u + 27u)
            dock_key(note - 53u, vel != 0);
        return;
    }
#endif
    if (nav_modal && nav_modal->key && nav_modal->key(note, vel))
        return;
    const modd_page_t *p = nav_active();
    if (p && p->key)
        p->key(note, vel);
}

/* -------------------------------------------------------------- init --- */

static void shell_init(void)
{
    uint32_t pal = UI_DEFAULT_INDEX;
    nav_init();
    ui_set_theme(&modd_theme);
#ifdef CONFIG_SYS_MENU
    pal = palette_from_stored(sys_settings.ui_pal);
    modd_lights = sys_settings.ui_lights < LIGHTS_N
                ? sys_settings.ui_lights : LIGHTS_HIGH;
#endif
    palette_set(pal);
}

static void shell_boot(void)
{
    nav_boot();
}
