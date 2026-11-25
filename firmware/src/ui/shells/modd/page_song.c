/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* SONG: global song state: tempo, transport, selected track. K1 sets the BPM. */

static void lvt_song_show(void)
{
    char b[8];
    lv_snprintf(b, sizeof b, "%d", (int)modd_gp(G_BPM));
    lvt_card_set(0, "BPM", b, "", lvt_pal.primary, (modd_gp(G_BPM) - 20) * 1000 / 280);
    lvt_card_set(1, "STATE", song.playing ? "PLAY" : "STOP", "", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%d", (int)modd_sel() + 1);
    lvt_card_set(2, "TRACK", b, "", lvt_pal.primary, -1);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_song_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "SONG", "");
    lvt_song_show();
}

static const ctl_slot_t SONG_CONTROLS[] = {
    { ENC_K1, G_BPM, 20, 300, 1, CTLF_GLOBAL, "BPM" },
};

static const modd_page_t PAGE_SONG = {
    .name      = "SONG",
    .title     = "Song",
    .view      = VIEW_GLO,
    .flags     = 0,
    .enter     = lvt_song_enter,
    .status    = lvt_song_show,
    .controls  = SONG_CONTROLS,
    .ncontrols = 1,
    .help_text = "K1  BPM",
};
