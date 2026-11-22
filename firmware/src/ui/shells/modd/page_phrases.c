/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* PHRASES: pattern loader. */

static int lvt_phrases_idx;

static void lvt_phrases_show(void)
{
    char b[8];
    lv_snprintf(b, sizeof b, "%d", lvt_phrases_idx);
    lvt_card_set(0, "PAT", b, "", lvt_pal.primary, lvt_phrases_idx * 1000 / 127);
    lvt_card_set(1, "LOAD", up_has_pat((uint32_t)lvt_phrases_idx) ? "OK" : "EMPTY", "", lvt_pal.primary, -1);
    lvt_card_set(2, "", "", "", lvt_pal.dim, -1);
    lvt_card_set(3, "", "", "", lvt_pal.dim, -1);
}

static void lvt_phrases_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "PHRASES", "");
    lvt_phrases_show();
}

static int lvt_phrases_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_K1) {
        int32_t v = lvt_phrases_idx + clicks;
        lvt_phrases_idx = v < 0 ? 0 : (v > 127 ? 127 : v);
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_K2 && clicks > 0) {
        up_load((uint32_t)lvt_phrases_idx);
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static const modd_page_t PAGE_PHRASES = {
    .name = "PHRASES", .title = "Phrases", .view = VIEW_SAVE, .flags = 0,
    .enter = lvt_phrases_enter, .status = lvt_phrases_show, .knob = lvt_phrases_knob,
    .help_text = "K1  Pattern\nK2  Load",
};
