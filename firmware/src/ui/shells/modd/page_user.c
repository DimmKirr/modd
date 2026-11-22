/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* USER: user preset slots (slot scroll, load, erase, save). */

/* clamp-add `clicks` to a global param; returns 1 */
static int lvt_gadd(uint32_t g, int32_t clicks, int32_t mn, int32_t mx)
{
    int32_t v = song.g[g] + clicks;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    song.g[g] = (int16_t)v;
    nav_dirty = 1;
    return 1;
}

static void lvt_user_show(void)
{
    char b[8];
    int32_t s = song.g[G_SLOT];
    lv_snprintf(b, sizeof b, "%d", (int)s);
    lvt_card_set(0, "SLOT", b, "", lvt_pal.primary, s * 1000 / 127);
    lvt_card_set(1, "LOAD", up_used((uint32_t)s) ? "USED" : "FREE", "", lvt_pal.primary, -1);
    lvt_card_set(2, "ERASE", "", "", lvt_pal.dim, -1);
    lvt_card_set(3, "SAVE", "", "", lvt_pal.dim, -1);
}

static void lvt_user_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "USER", "");
    lvt_user_show();
}

static int lvt_user_knob(uint32_t id, int32_t clicks)
{
    uint32_t s = (uint32_t)song.g[G_SLOT];
    if (id == ENC_K1) return lvt_gadd(G_SLOT, clicks, 0, 127);
    if (clicks <= 0) return 0;
    if (id == ENC_K2) { (void)up_load(s); return 1; }
    if (id == ENC_K3) { up_ui(1, s); return 1; }
    if (id == ENC_K4) { up_ui(2, s); return 1; }
    return 0;
}

static const modd_page_t PAGE_USER = {
    .name = "USER", .title = "User", .view = VIEW_SAVE, .flags = 0,
    .enter = lvt_user_enter, .status = lvt_user_show, .knob = lvt_user_knob,
    .help_text = "K1  Slot\nK2  Load\nK3  Erase\nK4  Save",
};
