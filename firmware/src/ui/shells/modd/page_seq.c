// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* SEQ: 16-step sequencer view — velocity bars and step cursor. */

static struct {
    lv_obj_t *bar[NSTEP];
    lv_obj_t *cursor;
    uint32_t pos;
} lvt_seq;

static void lvt_seq_show(void)
{
    const track_t *t = &trk[song.sel];
    uint32_t k;
    char b[8];

    for (k = 0; k < 4u && k < NSTEP; k++) {
        uint32_t si = lvt_seq.pos + k;
        if (si >= NSTEP) si = 0;
        lv_snprintf(b, sizeof b, "%u", (unsigned)(si + 1u));
        lvt_card_set(k, b, "", "", lvt_pal.primary, -1);
        lv_snprintf(b, sizeof b, "%u", (unsigned)t->step[si].vel);
        lvt_text(lvt_card[k].value, b);
    }

    for (k = 0; k < NSTEP; k++) {
        int32_t v = t->step[k].vel;
        if (v > 127) v = 127;
        lv_bar_set_value(lvt_seq.bar[k], v, LV_ANIM_OFF);
        lvt_bg_color(lvt_seq.bar[k],
                     k == t->seq_pos ? lvt_pal.accent : lvt_pal.primary,
                     LV_PART_INDICATOR);
    }
}

static void lvt_seq_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel, *row;
    uint32_t k;

    lvt_cards(body);
    panel = lvt_panel(body);
    lvt_seq.pos = 0;

    row = lvt_box(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_style_pad_column(row, 2, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    for (k = 0; k < NSTEP; k++) {
        lvt_seq.bar[k] = lv_bar_create(row);
        lv_obj_set_size(lvt_seq.bar[k], 10, 70);
        lv_obj_set_style_radius(lvt_seq.bar[k], 2, 0);
        lv_obj_set_style_radius(lvt_seq.bar[k], 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(lvt_seq.bar[k], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(lvt_seq.bar[k], LV_OPA_COVER, LV_PART_INDICATOR);
        lvt_bg_color(lvt_seq.bar[k], lvt_pal.line, 0);
        lv_bar_set_range(lvt_seq.bar[k], 0, 127);
    }

    lvt_footer("", "SEQ", "");
    lvt_seq_show();
}

static int lvt_seq_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        int32_t p = (int32_t)lvt_seq.pos + clicks;
        if (p < 0) p = 0;
        if (p >= (int32_t)NSTEP) p = (int32_t)NSTEP - 1;
        lvt_seq.pos = (uint32_t)p;
        return 1;
    }
    return 0;
}

static const modd_page_t PAGE_SEQ = {
    .name      = "SEQ",
    .title     = "Sequencer",
    .view      = VIEW_SEQ,
    .flags     = 0,
    .enter     = lvt_seq_enter,
    .status    = lvt_seq_show,
    .knob      = lvt_seq_knob,
    .help_text = "SELECT  Step cursor\nK1-K4  Step velocity",
};
