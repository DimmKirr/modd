// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* WX: the weather report as
 * LVGL widgets.
 *
 *   cards   TEMP, HUM, WIND, WMO; "---" without a report
 *   sky     a gradient in the selected track's colour: temperature, condition, and
 *           "City, CC  82%  7.2 KM/H"
 *   state   "Fetching..." before the first report, "No Data" / "check WiFi" when a fetch failed
 *   footer  the city, "---" without a report
 *
 * Data: wx_read() (ui/page_data.c), what the REST API's WX data is made of. Opening the
 * page starts a fetch when there is no report; PLAY starts one at any time. */

#include "ui/themes/felucca/weather_colors.h"   /* weather_condition_name() */

static struct {
    lv_obj_t *sky, *temp, *cond, *info;
    lv_obj_t *state, *state_sub;
    uint32_t  shown_ms;             /* updated_ms of the report on screen */
    uint8_t   shown;                /* 0 nothing yet, 1 fetching, 2 no data, 3 report */
    uint8_t   shown_trk;
} lvt_wx;

static void lvt_wx_show(void)
{
    static const char *const CAP[4] = { "TEMP", "HUM", "WIND", "WMO" };
    wx_data_t d;
    const struct weather_data *w;
    char v[4][12], b[48];
    uint8_t now;
    uint32_t i;

    wx_read(&d);
    w = d.w;
    now = w->valid ? 3 : d.fetching ? 1 : 2;
    if (now == lvt_wx.shown && lvt_wx.shown_trk == lvt_pal.trk &&
        (now != 3 || w->updated_ms == lvt_wx.shown_ms))
        return;
    lvt_wx.shown = now;
    lvt_wx.shown_ms = w->updated_ms;
    lvt_wx.shown_trk = lvt_pal.trk;

    lv_obj_set_hidden(lvt_wx.sky, now != 3);
    lv_obj_set_hidden(lvt_wx.state, now == 3);
    lv_obj_set_hidden(lvt_wx.state_sub, now != 2);
    lvt_footer("", now == 3 ? w->city : "---", "");
    if (now != 3) {
        for (i = 0; i < 4u; i++)
            lvt_card_set(i, CAP[i], "---", "", lvt_pal.dim, -1);
        lvt_text(lvt_wx.state, now == 1 ? "Fetching..." : "No Data");
        return;
    }

    {
        int32_t t = w->temp_c, a = t < 0 ? -t : t;
#ifdef CONFIG_WEATHER_UNIT_F
        const char *unit = "F", *speed = "MPH";
#else
        const char *unit = "C", *speed = "KM/H";
#endif
        lv_snprintf(v[0], sizeof v[0], "%s%d.%d", t < 0 ? "-" : "", (int)(a / 10), (int)(a % 10));
        lv_snprintf(b, sizeof b, "%s %s", v[0], unit);
        lvt_text(lvt_wx.temp, b);
        lv_snprintf(v[1], sizeof v[1], "%u%%", (unsigned)w->humidity);
        lv_snprintf(v[2], sizeof v[2], "%u.%u", (unsigned)(w->wind_kmh / 10u), (unsigned)(w->wind_kmh % 10u));
        lv_snprintf(v[3], sizeof v[3], "%u", (unsigned)w->wmo_code);
        if (w->city[0] && w->country[0])
            lv_snprintf(b, sizeof b, "%s, %s  %s  %s %s", w->city, w->country, v[1], v[2], speed);
        else
            lv_snprintf(b, sizeof b, "%s%s%s  %s %s", w->city, w->city[0] ? "  " : "", v[1], v[2], speed);
        lvt_text(lvt_wx.info, b);
    }
    for (i = 0; i < 4u; i++)
        lvt_card_set(i, CAP[i], v[i], "", i ? lvt_pal.text : lvt_pal.theme, -1);
    lvt_text(lvt_wx.cond, weather_condition_name(w->wmo_code));
    lvt_bg_color(lvt_wx.sky, lvt_mix(lvt_pal.bg, lvt_pal.theme, 40), 0);
    if (!lv_color_eq(lv_obj_get_style_bg_grad_color(lvt_wx.sky, 0), lvt_mix(lvt_pal.surf, lvt_pal.theme, 20)))
        lv_obj_set_style_bg_grad_color(lvt_wx.sky, lvt_mix(lvt_pal.surf, lvt_pal.theme, 20), 0);
}

static void lvt_wx_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel;

    lvt_cards(body);
    panel = lvt_panel(body);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lvt_wx.sky = lvt_box(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(lvt_wx.sky, lv_pct(100));
    lv_obj_set_style_radius(lvt_wx.sky, 6, 0);
    lv_obj_set_style_bg_opa(lvt_wx.sky, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(lvt_wx.sky, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(lvt_wx.sky, 8, 0);
    lv_obj_set_style_pad_row(lvt_wx.sky, 4, 0);
    lv_obj_set_flex_align(lvt_wx.sky, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lvt_wx.temp = lv_label_create(lvt_wx.sky);
    lv_obj_set_style_text_font(lvt_wx.temp, &lv_font_montserrat_20, 0);
    lv_label_set_text_static(lvt_wx.temp, "");
    lvt_wx.cond = lv_label_create(lvt_wx.sky);
    lv_label_set_text_static(lvt_wx.cond, "");
    lvt_wx.info = lv_label_create(lvt_wx.sky);
    lv_obj_add_style(lvt_wx.info, &lvt_st_small, 0);
    lv_obj_add_style(lvt_wx.info, &lvt_st_mid, 0);
    lv_obj_set_flex_grow(lvt_wx.info, 1);      /* at the bottom of the sky */
    lv_obj_set_width(lvt_wx.info, lv_pct(100));
    lv_label_set_long_mode(lvt_wx.info, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text_static(lvt_wx.info, "");

    lvt_wx.state = lv_label_create(panel);
    lv_obj_add_style(lvt_wx.state, &lvt_st_dim, 0);
    lv_label_set_text_static(lvt_wx.state, "");
    lvt_wx.state_sub = lvt_caption(panel, "check WiFi");

    net_need();                         /* the page needs the network while it is open */
    lvt_wx.shown = 0;
    lvt_wx_show();
    if (!weather_get()->valid && !weather_fetching())
        weather_fetch_start();
}

static void lvt_wx_leave(void)
{
    net_release();
}

static int lvt_wx_button(uint32_t id, uint32_t down)
{
    if (!down && id == BTN_PLAY) {
        weather_fetch_start();
        return 1;
    }
    return 0;
}

static const modd_page_t PAGE_WEATHER = {
    .name   = "WX",
    .title  = "Weather",
    .view   = VIEW_CONN,
    .flags  = PAGEF_SYSTEM,
    .enter  = lvt_wx_enter,
    .leave  = lvt_wx_leave,
    .status = lvt_wx_show,
    .button = lvt_wx_button,
    .help_text =
        "PLAY    Refresh",
};
