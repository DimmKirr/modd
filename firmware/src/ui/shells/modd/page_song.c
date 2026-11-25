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

static int lvt_song_knob(uint32_t id, int32_t clicks)
{
    int32_t v;
    if (id != ENC_K1) return 0;
    v = modd_gp(G_BPM) + clicks;
    if (v < 20) v = 20;
    if (v > 300) v = 300;
    modd_gp_set(G_BPM, (int16_t)v);
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_SONG = {
    .name   = "SONG",
    .title  = "Song",
    .view   = VIEW_GLO,
    .flags  = 0,
    .enter  = lvt_song_enter,
    .status = lvt_song_show,
    .knob   = lvt_song_knob,
    .help_text = "K1  BPM",
};
