/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Transpose: one card, K1 adjusts. */


static void lvt_transpose_show(void)
{
    char b[12];
    int32_t t = modd_tpi(modd_sel(), P_TRANS);
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

static const ctl_slot_t TRANSPOSE_CONTROLS[] = {
    { ENC_K1, P_TRANS, -24, 24, 1, 0, "TRN" },
};

static const modd_page_t PAGE_TRANSPOSE = {
    .name      = "TRANSPOSE",
    .title     = "Transpose",
    .view      = VIEW_SETTINGS,
    .flags     = 0,
    .enter     = lvt_transpose_enter,
    .status    = lvt_transpose_show,
    .controls  = TRANSPOSE_CONTROLS,
    .ncontrols = 1,
    .help_text = "K1  Transpose",
};
