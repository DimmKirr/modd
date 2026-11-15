// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Button-action tooltip overlay: a modal that shows the current control map.
 * Opened by GLO tap, dismissed by any button press. Draws a translucent
 * panel over the page listing each button's tap and hold action. */

/* button display names: use the shared CTL_BTN_NAME from modd_controls.h */
#define TT_BTN_NAME CTL_BTN_NAME

static const char *const TT_VIEW_NAME[VIEW_COUNT] = {
    "home", "env", "lfo", "fx", "scl", "edit", "glo", "save",
    "arp", "seq", "conn", "settings", "demo",
};

static const char *tt_action(uint8_t view)
{
    if (view == VIEW_NONE) return 0;
    if (view < VIEW_COUNT) return TT_VIEW_NAME[view];
    return "?";
}

/* draw page-local help: each line is "CONTROL  Description" */
static void tt_draw_help(const char *text)
{
    int32_t y = 0;
    const char *p = text;
    cv_begin(UI_W, UI_PANEL_H, T_BG);

    cv_text_fit(UI_INSET, ty_y(y + UI_ROW_H / 2, TY_LB), TY_LB,
                "PAGE CONTROLS", T_DIM, T_BG, UI_W - UI_INSET * 2);
    y += UI_ROW_H;
    cv_rect(UI_INSET, y - 1, UI_W - UI_INSET * 2, UI_RULE_H, T_LINE);

    while (*p && y + UI_ROW_H_XS <= (int32_t)UI_PANEL_H) {
        char line[48];
        uint32_t n = 0;
        while (*p && *p != '\n' && n < sizeof(line) - 1u)
            line[n++] = *p++;
        line[n] = 0;
        if (*p == '\n') p++;
        if (n == 0) continue;
        cv_text_fit(UI_INSET, ty_y(y + UI_ROW_H_XS / 2, TY_LB), TY_LB,
                    line, T_TEXT, T_BG, UI_W - UI_INSET * 2);
        y += UI_ROW_H_XS + 1;
    }

    mt_panel_end();
    ui_footer("", "CONTROLS", "");
}

/* draw the global button-to-view map (fallback when page has no help_text) */
static void tt_draw_global(void)
{
    int32_t y = 0, col2x = UI_W / 2;
    uint32_t i;

    cv_begin(UI_W, UI_PANEL_H, T_BG);

    cv_text_fit(UI_INSET, ty_y(y + UI_ROW_H / 2, TY_LB), TY_LB,
                "BUTTON", T_DIM, T_BG, col2x - UI_INSET - 4);
    cv_text_fit(col2x, ty_y(y + UI_ROW_H / 2, TY_LB), TY_LB,
                "TAP / HOLD", T_DIM, T_BG, UI_W - col2x - UI_INSET);
    y += UI_ROW_H;
    cv_rect(UI_INSET, y - 1, UI_W - UI_INSET * 2, UI_RULE_H, T_LINE);

    for (i = 0; i < BTN_COUNT && y + UI_ROW_H <= (int32_t)UI_PANEL_H; i++) {
        const char *tap  = tt_action(modd_controls.btn_tap[i]);
        const char *hold = tt_action(modd_controls.btn_hold[i]);
        if (!tap && !hold) continue;

        int32_t ty = ty_y(y + UI_ROW_H / 2, &AF_XS);
        cv_text_fit(UI_INSET, ty, &AF_XS, TT_BTN_NAME[i], T_TEXT, T_BG, col2x - UI_INSET - 4);

        char desc[32];
        uint32_t d = 0;
        if (tap) { while (tap[d] && d < 14) { desc[d] = tap[d]; d++; } }
        if (hold) {
            if (d) { desc[d++] = ' '; desc[d++] = '/'; desc[d++] = ' '; }
            uint32_t h = 0;
            while (hold[h] && d < 30) desc[d++] = hold[h++];
        }
        desc[d] = 0;
        cv_text_fit(col2x, ty, &AF_XS, desc, T_MID, T_BG, UI_W - col2x - UI_INSET);

        y += UI_ROW_H_XS + 1;
    }

#ifdef CONFIG_DEBUG_LOG
    if (y + UI_ROW_H_XS + 2 <= (int32_t)UI_PANEL_H) {
        y += 2;
        cv_rect(UI_INSET, y, UI_W - UI_INSET * 2, UI_RULE_H, T_LINE);
        y += UI_RULE_H + 2;
        cv_text_fit(UI_INSET, ty_y(y + UI_ROW_H_XS / 2, &AF_XS), &AF_XS,
                    "hold GLO: debug log", T_DIM, T_BG, UI_W - UI_INSET * 2);
    }
#endif

    mt_panel_end();
    ui_footer("", "CONTROLS", "");
}

static void tt_draw(void)
{
    const modd_page_t *p = nav_active();
    if (p && p->help_text)
        tt_draw_help(p->help_text);
    else
        tt_draw_global();
}

static int tt_button(uint32_t id, uint32_t down)
{
    if (!down) {
        nav_modal_close();
        return 1;
    }
    return 1;
}

static int tt_knob(uint32_t id, int32_t clicks)
{
    (void)id; (void)clicks;
    nav_modal_close();
    return 1;
}

static const modd_modal_t tooltip_modal = {
    .draw   = tt_draw,
    .button = tt_button,
    .knob   = tt_knob,
};

static void tooltip_toggle(void)
{
    if (nav_modal == &tooltip_modal)
        nav_modal_close();
    else
        nav_modal_open(&tooltip_modal);
}
