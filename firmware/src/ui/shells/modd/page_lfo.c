/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* LFO: rate, depth, shape and delay as 4 cards.
 * Under CONFIG_AUDIO_FELUCCA_STACK_INCLUDED this page is replaced by a
 * PG_KNOB_CTL entry in page_knob.c that maps upstream P_LRATE / P_LWAVE /
 * P_LPHASE / P_LFADE directly. */
#ifndef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED

static const char *const LFO_SHAPE_NAMES[] = {
    "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random"
};
#define LFO_SHAPE_COUNT 6

static struct {
    lv_obj_t *shape;
} lvt_lfo;

static void lvt_lfo_show(void)
{
    lfo_data_t d;
    char b[8];
    uint32_t si;
    lfo_read(&d);

    lv_snprintf(b, sizeof b, "%d", (int)d.rate);
    lvt_card_set(0, "RATE", b, "", lvt_pal.primary, d.rate * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.depth);
    lvt_card_set(1, "DEPTH", b, "%", lvt_pal.primary, d.depth * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.shape);
    lvt_card_set(2, "SHAPE", b, "", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%d", (int)d.delay);
    lvt_card_set(3, "DELAY", b, "", lvt_pal.primary, d.delay * 10);

    si = (uint32_t)d.shape < LFO_SHAPE_COUNT ? (uint32_t)d.shape : 0;
    lvt_text(lvt_lfo.shape, LFO_SHAPE_NAMES[si]);
}

static void lvt_lfo_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel;
    lvt_cards(body);
    panel = lvt_panel(body);

    lvt_lfo.shape = lv_label_create(panel);
    lv_obj_add_style(lvt_lfo.shape, &lvt_st_theme, 0);
    lv_obj_set_style_text_font(lvt_lfo.shape, &lv_font_montserrat_20, 0);
    lv_obj_set_style_margin_top(lvt_lfo.shape, 20, 0);
    lv_label_set_text_static(lvt_lfo.shape, "");

    lvt_footer("", "LFO", "");
    lvt_lfo_show();
}

static const ctl_slot_t LFO_CONTROLS[] = {
    { ENC_K1, P_LFRT, 0, 100, 1, 0, "RATE" },
    { ENC_K2, P_LFO,  0, 100, 1, 0, "DEPTH" },
    { ENC_K3, P_LFSH, 0, 5, 1, CTLF_WRAP, "SHAPE" },
    { ENC_K4, P_LFDL, 0, 100, 1, 0, "DELAY" },
};

static const modd_page_t PAGE_LFO = {
    .name      = "LFO",
    .title     = "LFO",
    .view      = VIEW_LFO,
    .flags     = 0,
    .enter     = lvt_lfo_enter,
    .status    = lvt_lfo_show,
    .controls  = LFO_CONTROLS,
    .ncontrols = 4,
    .help_text = "K1  Rate\nK2  Depth\nK3  Shape\nK4  Delay",
};
#endif /* !CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */
