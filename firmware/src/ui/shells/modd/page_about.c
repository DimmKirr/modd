// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* ABOUT: firmware name, version, uptime. */

static struct {
    lv_obj_t *name, *version, *uptime;
} lvt_about;

static void lvt_about_show(void)
{
    about_data_t d;
    char b[32];
    uint32_t h, m, s;
    about_read(&d);

    lvt_text(lvt_about.name, d.fw_name);
    lvt_text(lvt_about.version, d.fw_version);

    h = d.uptime_s / 3600u;
    m = (d.uptime_s / 60u) % 60u;
    s = d.uptime_s % 60u;
    lv_snprintf(b, sizeof b, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    lvt_text(lvt_about.uptime, b);
}

static void lvt_about_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel;

    panel = lvt_panel(body);
    lv_obj_set_style_pad_top(panel, 20, 0);

    lvt_about.name = lv_label_create(panel);
    lv_obj_set_style_text_font(lvt_about.name, &lv_font_montserrat_20, 0);
    lv_obj_add_style(lvt_about.name, &lvt_st_theme, 0);
    lv_label_set_text_static(lvt_about.name, "");

    lvt_about.version = lv_label_create(panel);
    lv_obj_add_style(lvt_about.version, &lvt_st_mid, 0);
    lv_label_set_text_static(lvt_about.version, "");

    lvt_rule(panel);

    lvt_caption(panel, "UPTIME");
    lvt_about.uptime = lv_label_create(panel);
    lv_label_set_text_static(lvt_about.uptime, "");

    lvt_footer("", "ABOUT", "");
    lvt_about_show();
}

static const modd_page_t PAGE_ABOUT = {
    .name      = "ABOUT",
    .title     = "About",
    .view      = VIEW_GLO,
    .flags     = PAGEF_SYSTEM,
    .enter     = lvt_about_enter,
    .status    = lvt_about_show,
};
