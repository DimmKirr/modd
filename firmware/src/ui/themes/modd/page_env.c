/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* ENV: ADSR envelope as 4 cards + an envelope curve. */

static struct {
    lv_obj_t *env;
    lv_point_precise_t pts[5];
    uint32_t sig;
} lvt_env;

static void lvt_env_show(void)
{
    env_data_t d;
    char b[8];
    uint32_t k;
    env_read(&d);

    lv_snprintf(b, sizeof b, "%d", (int)d.atk);
    lvt_card_set(0, "ATK", b, "", lvt_pal.theme, d.atk * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.dec);
    lvt_card_set(1, "DEC", b, "", lvt_pal.theme, d.dec * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.sus);
    lvt_card_set(2, "SUS", b, "%", lvt_pal.theme, d.sus * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.rel);
    lvt_card_set(3, "REL", b, "", lvt_pal.theme, d.rel * 10);

    k = (uint32_t)d.atk | (uint32_t)d.dec << 8 | (uint32_t)d.sus << 16 | (uint32_t)d.rel << 24;
    if (k != lvt_env.sig) {
        int32_t bw = 220, bh = 60;
        int32_t aw = d.atk * bw / 400, dw = d.dec * bw / 400, sw = bw / 4;
        int32_t sh = bh * d.sus / 100, rw = d.rel * bw / 400;
        if (aw < 4) aw = 4;
        if (dw < 4) dw = 4;
        if (rw < 4) rw = 4;
        if (aw + dw + sw + rw > bw) aw = dw = sw = rw = bw / 4;
        lvt_env.sig = k;
        lvt_env.pts[0].x = 0;                 lvt_env.pts[0].y = bh;
        lvt_env.pts[1].x = aw;                lvt_env.pts[1].y = 0;
        lvt_env.pts[2].x = aw + dw;           lvt_env.pts[2].y = bh - sh;
        lvt_env.pts[3].x = aw + dw + sw;      lvt_env.pts[3].y = bh - sh;
        lvt_env.pts[4].x = aw + dw + sw + rw; lvt_env.pts[4].y = bh;
        lv_line_set_points(lvt_env.env, lvt_env.pts, 5);
    }
}

static void lvt_env_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel;
    lvt_cards(body);
    panel = lvt_panel(body);

    lvt_env.env = lv_line_create(panel);
    lv_obj_set_size(lvt_env.env, 220, 62);
    lv_obj_set_style_line_width(lvt_env.env, 2, 0);
    lv_obj_add_style(lvt_env.env, &lvt_st_theme, 0);
    lv_obj_set_style_margin_top(lvt_env.env, 10, 0);
    lvt_env.sig = 0xFFFFFFFFu;

    lvt_footer("", "ENV", "");
    lvt_env_show();
}

static const param_slot_t ENV_KNOB_MAP[] = {
    { ENC_K1, P_ATK, 0, 100, 1, 0, "ATK" },
    { ENC_K2, P_DEC, 0, 100, 1, 0, "DEC" },
    { ENC_K3, P_SUS, 0, 100, 1, 0, "SUS" },
    { ENC_K4, P_REL, 0, 100, 1, 0, "REL" },
};

static const modd_page_t PAGE_ENV = {
    .name      = "ENV",
    .title     = "Envelope",
    .view      = VIEW_ENV,
    .flags     = 0,
    .enter     = lvt_env_enter,
    .status    = lvt_env_show,
    .knob_map  = ENV_KNOB_MAP,
    .knob_count = 4,
    .help_text = "K1  Attack\nK2  Decay\nK3  Sustain\nK4  Release",
};
