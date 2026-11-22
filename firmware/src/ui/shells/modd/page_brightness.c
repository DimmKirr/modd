/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Brightness: one card, K1 adjusts. */

static uint8_t lvt_brightness = 100;   /* no global param for it yet */

static void lvt_brightness_show(void)
{
    char b[12];
    lv_snprintf(b, sizeof b, "%d", (int)lvt_brightness);
    lvt_card_set(0, "BRT", b, "%", lvt_pal.primary, lvt_brightness * 10);
    lvt_card_set(1, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(2, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_brightness_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "BRIGHTNESS", "");
    lvt_brightness_show();
}

static int lvt_brightness_knob(uint32_t id, int32_t clicks)
{
    int32_t v;
    if (id != ENC_K1 || !clicks) return 0;
    v = lvt_brightness + clicks * 5;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    lvt_brightness = (uint8_t)v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_BRIGHTNESS = {
    .name   = "BRIGHTNESS",
    .title  = "Brightness",
    .view   = VIEW_SETTINGS,
    .flags  = 0,
    .enter  = lvt_brightness_enter,
    .status = lvt_brightness_show,
    .knob   = lvt_brightness_knob,
    .help_text = "K1  Brightness",
};
