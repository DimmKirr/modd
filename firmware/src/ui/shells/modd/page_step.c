/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* STEP: sequencer settings for the selected track. */

static const lvt_simple_t stepp_SLOTS[4] = {
    { P_SLEN, 1, 64, "LEN" }, { P_SDIV, 0, 15, "DIV" },
    { P_SSWING, 0, 100, "SWING" }, { P_SGATE, 0, 100, "GATE" },
};
static void lvt_stepp_show(void) { lvt_simple_show(stepp_SLOTS, 0); }
static const param_slot_t stepp_KNOB_MAP[4] = {
    { ENC_K1, P_SLEN, 1, 64, 1, 0, "LEN" }, { ENC_K2, P_SDIV, 0, 15, 1, 0, "DIV" },
    { ENC_K3, P_SSWING, 0, 100, 1, 0, "SWING" }, { ENC_K4, P_SGATE, 0, 100, 1, 0, "GATE" },
};
static void lvt_stepp_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "STEP", "");
    lvt_stepp_show();
}

static const modd_page_t PAGE_STEP = {
    .name = "STEP", .title = "Step", .view = VIEW_SEQ, .flags = 0,
    .enter = lvt_stepp_enter, .status = lvt_stepp_show,
    .knob_map = stepp_KNOB_MAP, .knob_count = 4,
    .help_text = "K1  Length\nK2  Division\nK3  Swing\nK4  Gate",
};
