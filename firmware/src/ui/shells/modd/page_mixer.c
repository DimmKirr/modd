// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* MIXER: 4 track levels as cards + vertical bar visualisation. */

static struct {
    lv_obj_t *bar[NTRK];
    lv_obj_t *lbl[NTRK];
} lvt_mixer;

static void lvt_mixerer_show(void)
{
    mixer_data_t d;
    char b[8];
    uint32_t k;
    mixer_read(&d);

    for (k = 0; k < NTRK; k++) {
        lv_snprintf(b, sizeof b, "T%u", (unsigned)(k + 1u));
        lvt_card_set(k, b, "", "", k == d.selected ? lvt_pal.trk_c[k] : lvt_pal.mid,
                     d.level[k] * 10);
        lv_snprintf(b, sizeof b, "%d", (int)d.level[k]);
        lvt_text(lvt_card[k].value, b);

        lv_bar_set_value(lvt_mixer.bar[k], d.level[k] > 100 ? 100 : d.level[k], LV_ANIM_OFF);
        lvt_bg_color(lvt_mixer.bar[k], k == d.selected ? lvt_pal.trk_c[k] : lvt_pal.mid,
                     LV_PART_INDICATOR);
    }
}

static void lvt_mixerer_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel, *row;
    uint32_t k;

    lvt_cards(body);
    panel = lvt_panel(body);

    row = lvt_box(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    for (k = 0; k < NTRK; k++) {
        lv_obj_t *col = lvt_box(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, lv_pct(100));
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lvt_mixer.bar[k] = lv_bar_create(col);
        lv_obj_set_size(lvt_mixer.bar[k], 24, 80);
        lv_obj_set_style_radius(lvt_mixer.bar[k], 3, 0);
        lv_obj_set_style_radius(lvt_mixer.bar[k], 3, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(lvt_mixer.bar[k], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(lvt_mixer.bar[k], LV_OPA_COVER, LV_PART_INDICATOR);
        lvt_bg_color(lvt_mixer.bar[k], lvt_pal.line, 0);
        lv_bar_set_range(lvt_mixer.bar[k], 0, 100);

        {
            char lb[4];
            lv_snprintf(lb, sizeof lb, "T%u", (unsigned)(k + 1u));
            lvt_mixer.lbl[k] = lvt_caption(col, "");
            lv_label_set_text(lvt_mixer.lbl[k], lb);
        }
    }

    lvt_footer("", "MIXER", "");
    lvt_mixerer_show();
}

static int lvt_mixerer_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        int32_t s = (int32_t)modd_sel() + clicks;
        if (s < 0) s = 0;
        if (s >= (int32_t)NTRK) s = (int32_t)NTRK - 1;
        modd_sel_set((uint32_t)s);
        return 1;
    }
    return 0;
}

static const param_slot_t MIXER_KNOB_MAP[] = {
    { ENC_K1, P_LEVEL, 0, 100, 1, SLOT_TRACK, "LVL1" },
    { ENC_K2, P_LEVEL, 0, 100, 1, SLOT_TRACK, "LVL2" },
    { ENC_K3, P_LEVEL, 0, 100, 1, SLOT_TRACK, "LVL3" },
    { ENC_K4, P_LEVEL, 0, 100, 1, SLOT_TRACK, "LVL4" },
};

static const modd_page_t PAGE_MIXER = {
    .name      = "MIXER",
    .title     = "Mixer",
    .view      = VIEW_HOME,
    .flags     = 0,
    .enter     = lvt_mixerer_enter,
    .status    = lvt_mixerer_show,
    .knob      = lvt_mixerer_knob,
    .knob_map  = MIXER_KNOB_MAP,
    .knob_count = 4,
    .help_text = "SELECT  Track\nK1-K4  Track 1-4 level",
};
