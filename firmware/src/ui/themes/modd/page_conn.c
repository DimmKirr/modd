// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd CONNECTIVITY: wifi on/off toggle, last page under VIEW_SETTINGS. */

#define MCONN_ROW_H   20
#define MCONN_X_PAD   8

static uint8_t mconn_wifi_on;

static void conn_draw(void)
{
    const char *val = mconn_wifi_on ? "ON" : "OFF";
    uint16_t vc = mconn_wifi_on ? T_THEME : T_DIM;

    mt_panel_begin();
    cv_text_c(MT_W / 2, ty_y(20, TY_TX), TY_TX, "Connectivity", T_TEXT, T_BG);

    /* wifi row */
    cv_rrect(2, 50, MT_W - 4, MCONN_ROW_H, 4, T_SURF, T_BG);
    cv_text_fit(MCONN_X_PAD, 53, TY_LB, "WIFI", T_DIM, T_SURF, 100);
    cv_text_r(MT_W - MCONN_X_PAD, 53, TY_LB, val, vc, T_SURF);

    cv_text_c(MT_W / 2, ty_y(90, TY_LB), TY_LB, "turn SELECT to toggle", T_DIM, T_BG);

    mt_panel_end();
    ui_footer("settings", "", "CONN");
}

static int conn_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        mconn_wifi_on = mconn_wifi_on ? 0 : 1;
        nav_dirty = 1;
        return 1;
    }
    (void)clicks;
    return 0;
}

static const modd_page_t PAGE_CONN = {
    .name  = "CONN",
    .title = "Connectivity",
    .view  = VIEW_SETTINGS,
    .flags = PAGEF_SYSTEM,
    .draw  = conn_draw,
    .knob  = conn_knob,
};
