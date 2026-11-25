/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* USER: user preset slots (slot scroll, load, erase, save). */

static void lvt_user_show(void)
{
    char b[8];
    int32_t s = modd_gp(G_SLOT);
    lv_snprintf(b, sizeof b, "%d", (int)s);
    lvt_card_set(0, "SLOT", b, "", lvt_pal.primary, s * 1000 / 127);
    lvt_card_set(1, "LOAD", up_used((uint32_t)s) ? "USED" : "FREE", "", lvt_pal.primary, -1);
    lvt_card_set(2, "ERASE", "", "", lvt_pal.dim, -1);
    lvt_card_set(3, "SAVE", "", "", lvt_pal.dim, -1);
}

static void lvt_user_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "USER", "");
    lvt_user_show();
}

static int user_load_action(int32_t clicks)
{
    if (clicks <= 0) return 0;
    (void)up_load((uint32_t)modd_gp(G_SLOT));
    return 1;
}

static int user_erase_action(int32_t clicks)
{
    if (clicks <= 0) return 0;
    up_ui(1, (uint32_t)modd_gp(G_SLOT));
    return 1;
}

static int user_save_action(int32_t clicks)
{
    if (clicks <= 0) return 0;
    up_ui(2, (uint32_t)modd_gp(G_SLOT));
    return 1;
}

static const ctl_slot_t USER_CONTROLS[] = {
    { ENC_K1, G_SLOT, 0, 127, 1, CTLF_GLOBAL, "SLOT",  NULL },
    { ENC_K2, 0,      0,   0, 0, 0,           "LOAD",  user_load_action },
    { ENC_K3, 0,      0,   0, 0, 0,           "ERASE", user_erase_action },
    { ENC_K4, 0,      0,   0, 0, 0,           "SAVE",  user_save_action },
};

static const modd_page_t PAGE_USER = {
    .name = "USER", .title = "User", .view = VIEW_SAVE, .flags = 0,
    .enter = lvt_user_enter, .status = lvt_user_show,
    .controls = USER_CONTROLS, .ncontrols = 4,
    .help_text = "K1  Slot\nK2  Load\nK3  Erase\nK4  Save",
};
