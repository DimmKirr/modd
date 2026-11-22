/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Transpose: one card, K1 adjusts. */


static void lvt_transpose_show(void)
{
    char b[12];
    int32_t t = trk[song.sel].p[P_TRANS];
    lv_snprintf(b, sizeof b, "%+d", (int)t);
    lvt_card_set(0, "TRN", b, "st", lvt_pal.primary, (t + 24) * 1000 / 48);
    lvt_card_set(1, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(2, "---", "---", "", lvt_pal.dim, -1);
    lvt_card_set(3, "---", "---", "", lvt_pal.dim, -1);
}

static void lvt_transpose_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "TRANSPOSE", "");
    lvt_transpose_show();
}

static int lvt_transpose_knob(uint32_t id, int32_t clicks)
{
    int32_t v;
    if (id != ENC_K1 || !clicks) return 0;
    v = trk[song.sel].p[P_TRANS] + clicks;
    if (v < -24) v = -24;
    if (v > 24) v = 24;
    trk[song.sel].p[P_TRANS] = (int16_t)v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_TRANSPOSE = {
    .name   = "TRANSPOSE",
    .title  = "Transpose",
    .view   = VIEW_SETTINGS,
    .flags  = 0,
    .enter  = lvt_transpose_enter,
    .status = lvt_transpose_show,
    .knob   = lvt_transpose_knob,
    .help_text = "K1  Transpose",
};
