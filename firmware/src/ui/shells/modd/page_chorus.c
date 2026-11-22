/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* CHORUS: global chorus parameters (song.g[]) as 2 cards. */

static void lvt_chorus_show(void)
{
    char b[8];
    lv_snprintf(b, sizeof b, "%d", (int)song.g[G_CRATE]);
    lvt_card_set(0, "RATE", b, "", lvt_pal.primary, song.g[G_CRATE] * 1000 / 127);
    lv_snprintf(b, sizeof b, "%d", (int)song.g[G_CDEPTH]);
    lvt_card_set(1, "DEPTH", b, "", lvt_pal.primary, song.g[G_CDEPTH] * 1000 / 127);
    lvt_card_set(2, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_chorus_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "CHORUS", "");
    lvt_chorus_show();
}

static int lvt_chorus_knob(uint32_t id, int32_t clicks)
{
    int16_t *target = 0;
    int16_t mn = 0, mx = 127, v;
    if (id == ENC_K1)      target = &song.g[G_CRATE];
    else if (id == ENC_K2) target = &song.g[G_CDEPTH];
    if (!target) return 0;
    v = *target + (int16_t)clicks;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    *target = v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_CHORUS = {
    .name   = "CHORUS",
    .title  = "Chorus",
    .view   = VIEW_FX,
    .flags  = 0,
    .enter  = lvt_chorus_enter,
    .status = lvt_chorus_show,
    .knob   = lvt_chorus_knob,
    .help_text = "K1  Rate\nK2  Depth",
};
