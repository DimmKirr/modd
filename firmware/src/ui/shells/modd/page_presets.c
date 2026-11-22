/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* PRESETS: factory preset browser for the selected track's engine. */

static void lvt_presets_show(void)
{
    const track_t *t = &trk[song.sel];
    const engine_t *e = lvt_cur_engine();
    char b[8];
    lv_snprintf(b, sizeof b, "%d", (int)t->preset);
    lvt_card_set(0, "PRESET", b, "", lvt_pal.primary,
                 e && e->npresets > 1 ? t->preset * 1000 / (e->npresets - 1) : -1);
    lvt_card_set(1, "ENGINE", e && e->name ? e->name : "---", "", lvt_pal.primary, -1);
    lvt_card_set(2, "", "", "", lvt_pal.dim, -1);
    lvt_card_set(3, "", "", "", lvt_pal.dim, -1);
}

static void lvt_presets_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "PRESETS", "");
    lvt_presets_show();
}

static int lvt_presets_knob(uint32_t id, int32_t clicks)
{
    track_t *t = &trk[song.sel];
    const engine_t *e = lvt_cur_engine();
    int32_t v;
    if ((id != ENC_K1 && id != ENC_SELECT) || !e || e->npresets < 1) return 0;
    v = t->preset + clicks;
    if (v < 0) v = 0;
    if (v > e->npresets - 1) v = e->npresets - 1;
    t->preset = (uint8_t)v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_PRESETS = {
    .name = "PRESETS", .title = "Presets", .view = VIEW_SAVE, .flags = 0,
    .enter = lvt_presets_enter, .status = lvt_presets_show, .knob = lvt_presets_knob,
    .help_text = "K1  Browse presets",
};
