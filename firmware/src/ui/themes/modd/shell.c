// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd UI shell: input dispatch and frame drawing for the modd theme.
 * Navigation is the shared ui_nav.c; chrome comes from theme.c via the element layer; pixels
 * come from Melodee's renderer (gfx.c: cv_* canvas, gfx_frame, the palette). */

/* ----------------------------------------------------------- drawing --- */

/* zone-based hints: defer footer badges above each pill */
static void shell_draw_zones(void)
{
    if (mt_foot[0].w)
        hint_defer_badge(mt_foot[0].x, UI_Y_FOOT + 4 - HINT_BADGE_H,
                         BTN_OCTDN, 1, ui_hint_btn_color(BTN_OCTDN));
    if (mt_foot[1].w)
        hint_defer_badge(mt_foot[1].x, UI_Y_FOOT + 4 - HINT_BADGE_H,
                         BTN_OCTUP, 1, ui_hint_btn_color(BTN_OCTUP));
}

/* text fallback for pages without zone widgets (HOME, demo, debug) */
static void shell_draw_hints(const modd_page_t *p)
{
    if (!p->help_text) return;
    const char *s = p->help_text;
    int32_t y = 2;
    cv_begin(UI_W, UI_PANEL_H, T_BG);
    cv_text_fit(UI_INSET, ty_y(y + UI_ROW_H / 2, TY_LB), TY_LB,
                "PAGE CONTROLS", T_DIM, T_BG, UI_W - UI_INSET * 2);
    y += UI_ROW_H;
    cv_rect(UI_INSET, y - 1, UI_W - UI_INSET * 2, UI_RULE_H, T_LINE);
    while (*s && y + UI_ROW_H_XS <= (int32_t)UI_PANEL_H) {
        char line[48];
        uint32_t n = 0;
        while (*s && *s != '\n' && n < sizeof(line) - 1u)
            line[n++] = *s++;
        line[n] = 0;
        if (*s == '\n') s++;
        if (n == 0) continue;
        cv_text_fit(UI_INSET, ty_y(y + UI_ROW_H_XS / 2, TY_LB), TY_LB,
                    line, T_TEXT, T_BG, UI_W - UI_INSET * 2);
        y += UI_ROW_H_XS + 2;
    }
    mt_panel_end();
}

static void shell_draw(void)
{
    const modd_page_t *p = nav_active();
    if (!p)
        return;

    if (p->status)
        p->status();
    toast_tick();

    if (!nav_dirty)
        return;

    gfx_frame(1);
    gfx_fill(0, 0, UI_W, UI_H, T_BG);
    mt_zone_count = 0;
    mt_badge_count = 0;
    hint_n_rects = 0;
    mt_foot[0].w = mt_foot[1].w = 0;
    ui_header();
    if (p->draw)
        p->draw();
    if (hints_visible && !nav_modal) {
        shell_draw_zones();
        if (mt_zone_count == 0 && p->help_text)
            shell_draw_hints(p);
        hint_draw_deferred_badges();
    }
    if (nav_modal && nav_modal->draw)
        nav_modal->draw();
    gfx_frame(0);
    toast_draw();
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
    if (toast.kind == TOAST_ERROR && !down) {
        toast_clear();
        return;
    }
    if (down) {
        shell_btn_held = id + 1u;
        shell_btn_us = fm1_input_btn_press_us(id);
    }
    if (!down && shell_btn_held == id + 1u) {
        uint32_t held_us = fm1_micros() - shell_btn_us;
        shell_btn_held = 0;
        if (held_us > modd_controls.hold_us) {
#ifdef CONFIG_DEBUG_LOG
            if (id == BTN_GLO) {
                if (nav_cur == nav_dbg_idx)
                    nav_set_page(nav_dbg_return);
                else {
                    nav_dbg_return = nav_cur;
                    nav_set_page(nav_dbg_idx);
                }
                return;
            }
#endif
            if (modd_controls.btn_hold[id] != VIEW_NONE) {
                nav_open_view(modd_controls.btn_hold[id]);
                return;
            }
        }
    }
    /* GLO: shell owns this button entirely.
     * short tap toggles contextual hints; hold is debug log (above).
     * consume both press and release so the gesture never leaks to the page. */
    if (id == BTN_GLO) {
        if (down) return;                       /* press consumed (held timer already recorded) */
        if (shell_btn_held == 0) {              /* short tap: held was cleared, not a hold action */
            hints_visible = !hints_visible;
            nav_dirty = 1;
        }
        return;
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
