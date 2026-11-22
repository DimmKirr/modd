/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Select mode: one card, K1 adjusts. */

static const char *const LVT_SELMODES[3] = { "PAGE", "MODW", "VEL" };
static uint8_t lvt_select_mode;   /* 0 PAGE, 1 MODW, 2 VEL */

static void lvt_select_mode_show(void)
{
    char b[12];
    lvt_card_set(0, "SELECT", LVT_SELMODES[lvt_select_mode], "", lvt_pal.primary, lvt_select_mode * 500);
    lvt_card_set(1, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(2, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
    (void)b;
}

static void lvt_select_mode_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "SELECT", "");
    lvt_select_mode_show();
}

static int lvt_select_mode_knob(uint32_t id, int32_t clicks)
{
    int32_t v;
    if (id != ENC_K1 || !clicks) return 0;
    v = (int32_t)lvt_select_mode + (clicks > 0 ? 1 : -1);
    if (v < 0) v = 2;
    if (v > 2) v = 0;
    lvt_select_mode = (uint8_t)v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_SELECT_MODE = {
    .name   = "SELECT",
    .title  = "Select mode",
    .view   = VIEW_SETTINGS,
    .flags  = 0,
    .enter  = lvt_select_mode_enter,
    .status = lvt_select_mode_show,
    .knob   = lvt_select_mode_knob,
    .help_text = "K1  Select mode",
};
