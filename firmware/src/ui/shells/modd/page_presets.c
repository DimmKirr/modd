/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* PRESETS: factory preset browser for the selected track's engine. */

static void lvt_presets_show(void)
{
    track_t *t = modd_trk(modd_sel());
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

static int presets_action(int32_t clicks)
{
    track_t *t = modd_trk(modd_sel());
    const engine_t *e = lvt_cur_engine();
    int32_t v;
    if (!e || e->npresets < 1) return 0;
    v = t->preset + clicks;
    if (v < 0) v = 0;
    if (v > e->npresets - 1) v = e->npresets - 1;
    t->preset = (uint8_t)v;
    nav_dirty = 1;
    return 1;
}

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
static const ctl_slot_t PRESETS_CONTROLS[] = {
    { ENC_K1, 0, 0, 127, 1, 0, "PRESET", presets_action },
};
#define PRESETS_NCONTROLS 1
#else
static const ctl_slot_t PRESETS_CONTROLS[] = {
    { ENC_K1,     0, 0, 127, 1, 0, "PRESET", presets_action },
    { ENC_SELECT, 0, 0, 127, 1, 0, "PRESET", presets_action },
};
#define PRESETS_NCONTROLS 2
#endif

static const modd_page_t PAGE_PRESETS = {
    .name = "PRESETS", .title = "Presets", .view = VIEW_SAVE, .flags = 0,
    .enter = lvt_presets_enter, .status = lvt_presets_show,
    .controls = PRESETS_CONTROLS, .ncontrols = PRESETS_NCONTROLS,
    .help_text = "K1  Browse presets",
};
