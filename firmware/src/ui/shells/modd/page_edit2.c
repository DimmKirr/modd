/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* EDIT 2: engine params E4..E7, labelled by the engine. */

LVT_SIMPLE_DATA(EDIT2,
    (P_E4, 0, 127, "E4"), (P_E5, 0, 127, "E5"), (P_E6, 0, 127, "E6"), (P_E7, 0, 127, "E7"));

static void lvt_edit2_labels_show(void)
{
    const engine_t *e = lvt_cur_engine();
    const char *lab[4];
    uint32_t k;
    for (k = 0; k < 4u; k++)
        lab[k] = (e && e->edit[k + 4].label) ? e->edit[k + 4].label : 0;
    lvt_simple_show(EDIT2_SLOTS, lab);
}

static void lvt_edit2_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "EDIT 2", "");
    lvt_edit2_labels_show();
}

static const modd_page_t PAGE_EDIT2 = {
    .name = "EDIT 2", .title = "Edit 2", .view = VIEW_EDIT, .flags = 0,
    .enter = lvt_edit2_enter, .status = lvt_edit2_labels_show,
    .knob_map = EDIT2_KNOB_MAP, .knob_count = 4,
};
