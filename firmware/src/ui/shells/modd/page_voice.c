/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* VOICE: voice count, hold, glide and unison. */

static void lvt_voice_show(void)
{
    voice_data_t d;
    char b[8];
    voice_read(&d);

    lv_snprintf(b, sizeof b, "%d", (int)d.voices);
    lvt_card_set(0, "VCE", b, "", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%d", (int)d.hold);
    lvt_card_set(1, "HOLD", b, "", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%d", (int)d.glide);
    lvt_card_set(2, "GLD", b, "ms", lvt_pal.primary, d.glide * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.unison);
    lvt_card_set(3, "UNI", b, "", lvt_pal.primary, -1);
}

static void lvt_voice_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_panel(body);
    lvt_footer("", "VOICE", "");
    lvt_voice_show();
}

static const param_slot_t VOICE_KNOB_MAP[] = {
    { ENC_K1, P_MONO, 0, 1, 1, 0, "VCE" },
    { ENC_K2, P_HOLD, 0, 1, 1, 0, "HOLD" },
    { ENC_K3, P_GLI,  0, 100, 1, 0, "GLD" },
    { ENC_K4, P_UNI,  0, 8, 1, 0, "UNI" },
};

static const modd_page_t PAGE_VOICE = {
    .name      = "VOICE",
    .title     = "Voice",
    .view      = VIEW_EDIT,
    .flags     = 0,
    .enter     = lvt_voice_enter,
    .status    = lvt_voice_show,
    .knob_map  = VOICE_KNOB_MAP,
    .knob_count = 4,
    .help_text = "K1  Mono / Poly\nK2  Hold\nK3  Glide\nK4  Unison",
};
