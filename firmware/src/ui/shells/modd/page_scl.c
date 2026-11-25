/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* SCL: scale root, type, octave and transpose. */

static const char *const SCL_NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const char *const SCL_SCALE_NAMES[] = {
    "Chrom", "Major", "Minor", "Penta", "Blues", "Dorian",
    "Mixol", "Lydian", "Phryg", "Locr", "Harm", "Melod"
};
#define SCL_SCALE_COUNT 12

static struct {
    lv_obj_t *desc;
} lvt_scl;

static void lvt_scl_show(void)
{
    scl_data_t d;
    char b[32];
    uint32_t ri, si;
    scl_read(&d);

    ri = (uint32_t)((d.root % 12 + 12) % 12);
    lvt_card_set(0, "ROOT", SCL_NOTE_NAMES[ri], "", lvt_pal.primary, -1);

    si = (uint32_t)d.scale < SCL_SCALE_COUNT ? (uint32_t)d.scale : 0;
    lvt_card_set(1, "SCALE", SCL_SCALE_NAMES[si], "", lvt_pal.primary, -1);

    lv_snprintf(b, sizeof b, "%s%d", d.octave > 0 ? "+" : "", (int)d.octave);
    lvt_card_set(2, "OCT", b, "", lvt_pal.primary, -1);

    lv_snprintf(b, sizeof b, "%d", (int)d.transpose);
    lvt_card_set(3, "TRANS", b, "st", lvt_pal.primary, -1);

    lv_snprintf(b, sizeof b, "%s %s", SCL_NOTE_NAMES[ri], SCL_SCALE_NAMES[si]);
    lvt_text(lvt_scl.desc, b);
}

static void lvt_scl_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel;
    lvt_cards(body);
    panel = lvt_panel(body);

    lvt_scl.desc = lv_label_create(panel);
    lv_obj_add_style(lvt_scl.desc, &lvt_st_theme, 0);
    lv_obj_set_style_text_font(lvt_scl.desc, &lv_font_montserrat_20, 0);
    lv_obj_set_style_margin_top(lvt_scl.desc, 20, 0);
    lv_label_set_text_static(lvt_scl.desc, "");

    lvt_footer("", "SCL", "");
    lvt_scl_show();
}

static const ctl_slot_t SCL_CONTROLS[] = {
    { ENC_K1, P_ROOT,  0, 11, 1, CTLF_WRAP, "ROOT" },
    { ENC_K2, P_SCALE, 0, 11, 1, CTLF_WRAP, "SCALE" },
    { ENC_K3, P_OCT,   -4, 4, 1, 0, "OCT" },
    { ENC_K4, P_TRANS, -12, 12, 1, 0, "TRANS" },
};

static const modd_page_t PAGE_SCL = {
    .name      = "SCL",
    .title     = "Scale",
    .view      = VIEW_SCL,
    .flags     = 0,
    .enter     = lvt_scl_enter,
    .status    = lvt_scl_show,
    .controls  = SCL_CONTROLS,
    .ncontrols = 4,
    .help_text = "K1  Root note\nK2  Scale\nK3  Octave\nK4  Transpose",
};
