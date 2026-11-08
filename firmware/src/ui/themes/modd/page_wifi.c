// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd WIFI settings: stub. The WiFi configuration screen goes here. */

static void wifi_pg_draw(void)
{
    mt_panel_begin();
    cv_text_c(MT_W / 2, ty_y(MT_H_PANEL / 2 - 10, TY_VL), TY_VL, "WiFi", T_TEXT, T_BG);
    cv_text_c(MT_W / 2, ty_y(MT_H_PANEL / 2 + 22, TY_LB), TY_LB, "not yet", T_DIM, T_BG);
    mt_panel_end();
    ui_footer("settings", "", "WIFI");
}

static const modd_page_t PAGE_WIFI = {
    .name  = "WIFI",
    .title = "WiFi",
    .view  = VIEW_SETTINGS,
    .flags = PAGEF_SYSTEM,
    .draw  = wifi_pg_draw,
};
