/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* CLOCK: tempo, swing and clock source (song.g[]) as 3 cards. */

static void lvt_clock_show(void)
{
    char b[8];
    lv_snprintf(b, sizeof b, "%d", (int)song.g[G_BPM]);
    lvt_card_set(0, "BPM", b, "", lvt_pal.primary, (song.g[G_BPM] - 20) * 1000 / 280);
    lv_snprintf(b, sizeof b, "%d", (int)song.g[G_SWING]);
    lvt_card_set(1, "SWING", b, "%", lvt_pal.primary, song.g[G_SWING] * 10);
    lv_snprintf(b, sizeof b, "%d", (int)song.g[G_CLOCK]);
    lvt_card_set(2, "CLOCK", b, "", lvt_pal.primary, song.g[G_CLOCK] * 1000 / 127);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_clock_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "CLOCK", "");
    lvt_clock_show();
}

static int lvt_clock_knob(uint32_t id, int32_t clicks)
{
    int16_t *target = 0;
    int16_t mn = 0, mx = 127, v;
    if (id == ENC_K1)      { target = &song.g[G_BPM]; mn = 20; mx = 300; }
    else if (id == ENC_K2) { target = &song.g[G_SWING]; mx = 100; }
    else if (id == ENC_K3) { target = &song.g[G_CLOCK]; }
    if (!target) return 0;
    v = *target + (int16_t)clicks;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    *target = v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_CLOCK = {
    .name   = "CLOCK",
    .title  = "Clock",
    .view   = VIEW_HOME,
    .flags  = 0,
    .enter  = lvt_clock_enter,
    .status = lvt_clock_show,
    .knob   = lvt_clock_knob,
    .help_text = "K1  BPM\nK2  Swing\nK3  Clock",
};
