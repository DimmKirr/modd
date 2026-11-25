/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* DETAIL: one step's properties. */

static int lvt_detail_idx;

static void lvt_detail_show(void)
{
    const step_t *s = &modd_trk(modd_sel())->step[lvt_detail_idx];
    char b[8];
    lv_snprintf(b, sizeof b, "%d", lvt_detail_idx + 1);
    lvt_card_set(0, "STEP", b, "", lvt_pal.primary, lvt_detail_idx * 1000 / (NSTEP - 1));
    lv_snprintf(b, sizeof b, "%d", (int)s->note[0]);
    lvt_card_set(1, "NOTE", s->note[0] ? b : "--", "", s->note[0] ? lvt_pal.primary : lvt_pal.dim, s->note[0] * 1000 / 127);
    lv_snprintf(b, sizeof b, "%d", (int)s->vel);
    lvt_card_set(2, "VEL", b, "", lvt_pal.primary, s->vel * 1000 / 127);
    lv_snprintf(b, sizeof b, "%d", (int)modd_step_len(s));
    lvt_card_set(3, "LEN", b, "", lvt_pal.primary, -1);
}

static void lvt_detail_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "DETAIL", "");
    lvt_detail_show();
}

static int detail_step_action(int32_t clicks)
{
    int32_t v = lvt_detail_idx + clicks;
    lvt_detail_idx = v < 0 ? 0 : (v > NSTEP - 1 ? NSTEP - 1 : v);
    nav_dirty = 1;
    return 1;
}

static const ctl_slot_t DETAIL_CONTROLS[] = {
    { ENC_K1, 0, 0, NSTEP - 1, 1, 0, "STEP", detail_step_action },
};

static const modd_page_t PAGE_DETAIL = {
    .name = "DETAIL", .title = "Detail", .view = VIEW_SEQ, .flags = 0,
    .enter = lvt_detail_enter, .status = lvt_detail_show,
    .controls = DETAIL_CONTROLS, .ncontrols = 1,
    .help_text = "K1  Step",
};
