/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* TOOLS: clear sequences, init sound. */

static void lvt_tools_show(void)
{
    lvt_card_set(0, "CLRSEQ", "", "", lvt_pal.primary, -1);
    lvt_card_set(1, "INITSND", "", "", lvt_pal.primary, -1);
    lvt_card_set(2, "", "", "", lvt_pal.dim, -1);
    lvt_card_set(3, "", "", "", lvt_pal.dim, -1);
}

static void lvt_tools_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "TOOLS", "");
    lvt_tools_show();
}

static int lvt_tools_knob(uint32_t id, int32_t clicks)
{
    uint32_t i;
    if (clicks <= 0) return 0;
    if (id == ENC_K1) {
        for (i = 0; i < NTRK; i++) seq_pattern_clear(modd_trk(i));
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_K2) {
        track_t *t = modd_trk(modd_sel());
        t->preset = 0;
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static const modd_page_t PAGE_TOOLS = {
    .name = "TOOLS", .title = "Tools", .view = VIEW_SAVE, .flags = 0,
    .enter = lvt_tools_enter, .status = lvt_tools_show, .knob = lvt_tools_knob,
    .help_text = "K1  Clear sequences\nK2  Init sound",
};
