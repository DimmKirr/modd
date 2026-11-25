/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* MIDI channel: one card, K1 adjusts. */

static uint8_t lvt_midi_ch = 1;   /* song has no channel field */

static void lvt_midi_ch_show(void)
{
    char b[12];
    lv_snprintf(b, sizeof b, "%d", (int)lvt_midi_ch);
    lvt_card_set(0, "CH", b, "", lvt_pal.primary, (lvt_midi_ch - 1) * 1000 / 15);
    lvt_card_set(1, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(2, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_midi_ch_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "MIDI CH", "");
    lvt_midi_ch_show();
}

static int lvt_midi_ch_k1(int32_t clicks)
{
    int32_t v;
    if (!clicks) return 0;
    v = lvt_midi_ch + clicks;
    if (v < 1) v = 1;
    if (v > 16) v = 16;
    lvt_midi_ch = (uint8_t)v;
    nav_dirty = 1;
    return 1;
}

static const ctl_slot_t MIDI_CH_CONTROLS[] = {
    { ENC_K1, P_COUNT, 1, 16, 1, 0, "CH", lvt_midi_ch_k1 },
};

static const modd_page_t PAGE_MIDI_CH = {
    .name   = "MIDI CH",
    .title  = "MIDI channel",
    .view   = VIEW_SETTINGS,
    .flags  = 0,
    .enter  = lvt_midi_ch_enter,
    .status = lvt_midi_ch_show,
    .controls  = MIDI_CH_CONTROLS,
    .ncontrols = 1,
    .help_text = "K1  MIDI channel",
};
