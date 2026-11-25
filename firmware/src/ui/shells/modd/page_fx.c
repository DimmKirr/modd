/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* FX: per-track send levels (dist, chorus, delay, reverb). */

static void lvt_fx_show(void)
{
    fx_data_t d;
    char b[8];
    fx_read(&d);

    lv_snprintf(b, sizeof b, "%d", (int)d.dist);
    lvt_card_set(0, "DST", b, "%", lvt_pal.primary, d.dist * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.chorus);
    lvt_card_set(1, "CHO", b, "%", lvt_pal.primary, d.chorus * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.delay);
    lvt_card_set(2, "DLY", b, "%", lvt_pal.primary, d.delay * 10);
    lv_snprintf(b, sizeof b, "%d", (int)d.reverb);
    lvt_card_set(3, "REV", b, "%", lvt_pal.primary, d.reverb * 10);
}

static void lvt_fx_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_panel(body);
    lvt_footer("", "FX", "");
    lvt_fx_show();
}

static const ctl_slot_t FX_CONTROLS[] = {
    { ENC_K1, P_DIST, 0, 100, 1, 0, "DST" },
    { ENC_K2, P_CHOR, 0, 100, 1, 0, "CHO" },
    { ENC_K3, P_DLY,  0, 100, 1, 0, "DLY" },
    { ENC_K4, P_REV,  0, 100, 1, 0, "REV" },
};

static const modd_page_t PAGE_FX = {
    .name      = "FX",
    .title     = "Effects",
    .view      = VIEW_FX,
    .flags     = 0,
    .enter     = lvt_fx_enter,
    .status    = lvt_fx_show,
    .controls  = FX_CONTROLS,
    .ncontrols = 4,
    .help_text = "K1  Distortion\nK2  Chorus\nK3  Delay\nK4  Reverb",
};
