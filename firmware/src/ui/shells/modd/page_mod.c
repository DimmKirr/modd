/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* MOD: 4-slot modulation matrix. K1 picks the slot, K2/K3/K4 edit SRC/DST/AMT. */

#ifndef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED   /* the stack's params.c owns mod_ui_slot */
static uint32_t mod_ui_slot;
#endif

static void lvt_mod_show(void)
{
    uint32_t sel = modd_sel();
    uint32_t base = mod_ui_slot * 3u;
    char b[8], s[2];
    int16_t amt = modd_tpi(sel, P_M1AMT + base);
    s[0] = (char)('A' + mod_ui_slot);
    s[1] = 0;
    lvt_card_set(0, "SLOT", s, "", lvt_pal.primary, -1);
    lvt_card_set(1, "SRC", modd_mod_src_name((uint32_t)modd_tpi(sel, P_M1SRC + base)), "", lvt_pal.primary, -1);
    lvt_card_set(2, "DST", modd_mod_dst_name(modd_trk(sel), (uint32_t)modd_tpi(sel, P_M1DST + base)), "", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%+d", (int)amt);
    lvt_card_set(3, "AMT", b, "", lvt_pal.primary, (amt + 64) * 1000 / 127);
}

static void lvt_mod_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "MOD", "");
    lvt_mod_show();
}

static int mod_edit(uint32_t pid_base, int32_t clicks, int16_t mn, int16_t mx)
{
    uint32_t sel = modd_sel();
    uint32_t pid = pid_base + mod_ui_slot * 3u;
    int16_t v = modd_tpi(sel, pid) + (int16_t)clicks;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    modd_tpi_set(sel, pid, v);
    nav_dirty = 1;
    return 1;
}

static int mod_slot_action(int32_t clicks)
{
    int32_t s = (int32_t)mod_ui_slot + clicks;
    if (s < 0) s = 3;
    if (s > 3) s = 0;
    mod_ui_slot = (uint32_t)s;
    nav_dirty = 1;
    return 1;
}

static int mod_src_action(int32_t clicks) { return mod_edit(P_M1SRC, clicks, 0, 127); }
static int mod_dst_action(int32_t clicks) { return mod_edit(P_M1DST, clicks, 0, 127); }
static int mod_amt_action(int32_t clicks) { return mod_edit(P_M1AMT, clicks, -64, 63); }

static const ctl_slot_t MOD_CONTROLS[] = {
    { ENC_K1, 0, 0,   3, 1, CTLF_WRAP, "SLOT", mod_slot_action },
    { ENC_K2, 0, 0, 127, 1, 0,         "SRC",  mod_src_action },
    { ENC_K3, 0, 0, 127, 1, 0,         "DST",  mod_dst_action },
    { ENC_K4, 0, -64, 63, 1, 0,        "AMT",  mod_amt_action },
};

static const modd_page_t PAGE_MOD = {
    .name   = "MOD",
    .title  = "Mod",
    .view   = VIEW_LFO,
    .flags  = 0,
    .enter  = lvt_mod_enter,
    .status = lvt_mod_show,
    .controls  = MOD_CONTROLS,
    .ncontrols = 4,
    .help_text = "K1  Slot\nK2  Source\nK3  Dest\nK4  Amount",
};
