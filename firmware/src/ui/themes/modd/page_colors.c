// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd COLORS settings: pick the palette. Any knob steps through UI_PALETTES (Melodee's set plus
 * MODD and MODD NIGHT from the logo); the choice applies at once and is saved when the settings
 * store is built in (CONFIG_SYS_MENU). */

static void colors_apply(uint32_t i)
{
    palette_set(i);
    gfx_forget();                                  /* every colour changed: the panel is resent whole */
#ifdef CONFIG_SYS_MENU
    sys_settings.ui_pal = (uint8_t)palette_to_stored(ux.pal);
    sys_settings_dirty = 1;
#endif
    nav_dirty = 1;
}

static void colors_draw(void)
{
    char pos[8];
    uint32_t i = 0, n = UI_NPALETTES;
    pos[i++] = (char)('0' + (ux.pal + 1u) / 10u);
    if (pos[0] == '0') i = 0;
    pos[i++] = (char)('0' + (ux.pal + 1u) % 10u);
    pos[i++] = '/';
    if (n >= 10u) pos[i++] = (char)('0' + n / 10u);
    pos[i++] = (char)('0' + n % 10u);
    pos[i] = 0;

    mt_panel_begin();
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 - 10, TY_VL), TY_VL, UI_PALETTES[ux.pal].name, T_THEME, T_BG);
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 + 22, TY_LB), TY_LB, "turn a knob to change", T_DIM, T_BG);
    /* the four track colours, as swatches */
    for (i = 0; i < 4u; i++)
        cv_rrect(72 + 26 * (int32_t)i, UI_PANEL_H - 22, 20, 10, 3, UI_PALETTES[ux.pal].trk[i], T_BG);
    mt_panel_end();
    ui_footer("settings", pos, "COLORS");
}

static int colors_knob(uint32_t id, int32_t clicks)
{
    uint32_t n = UI_NPALETTES;
    if (id == ENC_SELECT || !clicks)
        return 0;
    colors_apply((uint32_t)((int32_t)ux.pal + (clicks > 0 ? 1 : -1) + (int32_t)n) % n);
    return 1;
}

static const modd_page_t PAGE_COLORS = {
    .name  = "COLORS",
    .title = "Colors",
    .view  = VIEW_SETTINGS,
    .flags = PAGEF_SYSTEM,
    .draw  = colors_draw,
    .knob  = colors_knob,
};
