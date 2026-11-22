/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Palette: one card, K1 adjusts. */


static void lvt_palette_show(void)
{
    char b[12];
    lv_snprintf(b, sizeof b, "%d", (int)lvt_pal.idx);
    lvt_card_set(0, "PAL", b, "", lvt_pal.primary, (int32_t)lvt_pal.idx * 1000 / 9);
    lvt_card_set(1, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(2, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_palette_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "PALETTE", "");
    lvt_palette_show();
}

static int lvt_palette_knob(uint32_t id, int32_t clicks)
{
    int32_t v;
    if (id != ENC_K1 || !clicks) return 0;
    v = (int32_t)lvt_pal.idx + (clicks > 0 ? 1 : -1);
    if (v < 0) v = 9;
    if (v > 9) v = 0;
    if ((uint32_t)v >= UI_NPALETTES) v = 0;
    lvt_pal_set((uint32_t)v);
    lvt_pal_apply();
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_PALETTE = {
    .name   = "PALETTE",
    .title  = "Palette",
    .view   = VIEW_SETTINGS,
    .flags  = 0,
    .enter  = lvt_palette_enter,
    .status = lvt_palette_show,
    .knob   = lvt_palette_knob,
    .help_text = "K1  Palette",
};
