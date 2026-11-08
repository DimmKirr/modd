// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd WEATHER: stub. The weather screen goes here (data: wifi_weather.c). */

static void weather_pg_draw(void)
{
    mt_panel_begin();
    cv_text_c(MT_W / 2, ty_y(MT_H_PANEL / 2 - 10, TY_VL), TY_VL, "Weather", T_TEXT, T_BG);
    cv_text_c(MT_W / 2, ty_y(MT_H_PANEL / 2 + 22, TY_LB), TY_LB, "not yet", T_DIM, T_BG);
    mt_panel_end();
    ui_footer("wifi", "", "WEATHER");
}

static const modd_page_t PAGE_WEATHER = {
    .name  = "WEATHER",
    .title = "Weather",
    .view  = VIEW_CONN,
    .draw  = weather_pg_draw,
};
