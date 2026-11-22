/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* STEP: sequencer settings for the selected track. */

LVT_SIMPLE_DATA(stepp,
    (P_SLEN,   1, 64,  "LEN"),
    (P_SDIV,   0, 15,  "DIV"),
    (P_SSWING, 0, 100, "SWING"),
    (P_SGATE,  0, 100, "GATE"));
LVT_SIMPLE_ENTER(stepp, "STEP")

static const modd_page_t PAGE_STEP = {
    .name = "STEP", .title = "Step", .view = VIEW_SEQ, .flags = 0,
    .enter = lvt_stepp_enter, .status = lvt_stepp_show,
    .knob_map = stepp_KNOB_MAP, .knob_count = 4,
    .help_text = "K1  Length\nK2  Division\nK3  Swing\nK4  Gate",
};
