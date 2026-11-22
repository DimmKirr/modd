/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* REVERB: global reverb parameters (song.g[]) as 3 cards. */

static void lvt_reverb_show(void)
{
    static const char *const T[3] = { "ROOM", "SPRING", "HALL" };
    char b[8];
    int t = song.g[G_RTYPE];
    if (t < 0 || t > 2) t = 0;
    lvt_card_set(0, "TYPE", T[t], "", lvt_pal.primary, t * 500);
    lv_snprintf(b, sizeof b, "%d", (int)song.g[G_RSIZE]);
    lvt_card_set(1, "SIZE", b, "", lvt_pal.primary, song.g[G_RSIZE] * 1000 / 127);
    lv_snprintf(b, sizeof b, "%d", (int)song.g[G_RDAMP]);
    lvt_card_set(2, "DAMP", b, "", lvt_pal.primary, song.g[G_RDAMP] * 1000 / 127);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_reverb_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "REVERB", "");
    lvt_reverb_show();
}

static int lvt_reverb_knob(uint32_t id, int32_t clicks)
{
    int16_t *target = 0;
    int16_t mn = 0, mx = 127, v;
    if (id == ENC_K1)      { target = &song.g[G_RTYPE]; mx = 2; }
    else if (id == ENC_K2) { target = &song.g[G_RSIZE]; }
    else if (id == ENC_K3) { target = &song.g[G_RDAMP]; }
    if (!target) return 0;
    v = *target + (int16_t)clicks;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    *target = v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_REVERB = {
    .name   = "REVERB",
    .title  = "Reverb",
    .view   = VIEW_FX,
    .flags  = 0,
    .enter  = lvt_reverb_enter,
    .status = lvt_reverb_show,
    .knob   = lvt_reverb_knob,
    .help_text = "K1  Type\nK2  Size\nK3  Damp",
};
