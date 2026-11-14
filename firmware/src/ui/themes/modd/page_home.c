// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd HOME: hello world, for now. */

static void home_draw(void)
{
    mt_panel_begin();
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 - 18, TY_VL), TY_VL, "Hello, world", T_THEME, T_BG);
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 + 16, TY_LB), TY_LB, "hold HOME: settings", T_DIM, T_BG);
    cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 + 34, TY_LB), TY_LB, "hold GLO: weather", T_DIM, T_BG);
    cv_text_fit(8, ty_y(UI_PANEL_H - 10, TY_LB), TY_LB, CONFIG_FIRMWARE_VERSION, T_DIM, T_BG, UI_W - 16);
    mt_panel_end();
    ui_footer("modd", "", "HOME");
}

static const modd_page_t PAGE_HOME = {
    .name  = "HOME",
    .title = "modd",
    .view  = VIEW_HOME,
    .flags = PAGEF_HOME,
    .draw  = home_draw,
};
