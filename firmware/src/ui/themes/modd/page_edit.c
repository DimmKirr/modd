// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* EDIT: engine-specific parameters E0..E7. */

static struct {
    lv_obj_t *eng_name, *eng_detail;
    lv_obj_t *elbl[4], *eval[4];
} lvt_edit;

static void lvt_edit_show(void)
{
    edit_data_t d;
    char b[8];
    uint32_t k;
    edit_read(&d);

    for (k = 0; k < 4u; k++) {
        lv_snprintf(b, sizeof b, "E%u", (unsigned)k);
        lvt_card_set(k, b, "", "", lvt_pal.theme, -1);
        lv_snprintf(b, sizeof b, "%d", (int)d.e[k]);
        lvt_text(lvt_card[k].value, b);
    }

    lv_snprintf(b, sizeof b, "P%02u %s", (unsigned)(d.preset % 100u), d.engine_name);
    lvt_text(lvt_edit.eng_name, b);

    for (k = 0; k < 4u; k++) {
        lv_snprintf(b, sizeof b, "E%u", (unsigned)(k + 4u));
        lvt_text(lvt_edit.elbl[k], b);
        lv_snprintf(b, sizeof b, "%d", (int)d.e[k + 4]);
        lvt_text(lvt_edit.eval[k], b);
    }
}

static void lvt_edit_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel, *row;
    uint32_t k;

    lvt_cards(body);
    panel = lvt_panel(body);

    lvt_edit.eng_name = lv_label_create(panel);
    lv_obj_add_style(lvt_edit.eng_name, &lvt_st_theme, 0);
    lv_label_set_text_static(lvt_edit.eng_name, "");

    lvt_rule(panel);

    for (k = 0; k < 4u; k++) {
        row = lvt_box(panel, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, 0);

        lvt_edit.elbl[k] = lv_label_create(row);
        lv_obj_add_style(lvt_edit.elbl[k], &lvt_st_small, 0);
        lv_obj_add_style(lvt_edit.elbl[k], &lvt_st_mid, 0);
        lv_obj_set_width(lvt_edit.elbl[k], 24);
        lv_label_set_text_static(lvt_edit.elbl[k], "");

        lvt_edit.eval[k] = lv_label_create(row);
        lv_obj_add_style(lvt_edit.eval[k], &lvt_st_small, 0);
        lv_label_set_text_static(lvt_edit.eval[k], "");
    }

    lvt_footer("", "EDIT", "");
    lvt_edit_show();
}

static const param_slot_t EDIT_KNOB_MAP[] = {
    { ENC_K1, P_E0, -128, 127, 1, 0, "E0" },
    { ENC_K2, P_E1, -128, 127, 1, 0, "E1" },
    { ENC_K3, P_E2, -128, 127, 1, 0, "E2" },
    { ENC_K4, P_E3, -128, 127, 1, 0, "E3" },
};

static const modd_page_t PAGE_EDIT = {
    .name      = "EDIT",
    .title     = "Edit",
    .view      = VIEW_EDIT,
    .flags     = 0,
    .enter     = lvt_edit_enter,
    .status    = lvt_edit_show,
    .knob_map  = EDIT_KNOB_MAP,
    .knob_count = 4,
    .help_text = "K1-K4  Engine params E0-E3",
};
