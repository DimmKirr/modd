/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* MOD: 4-slot modulation matrix. K1 picks the slot, K2/K3/K4 edit SRC/DST/AMT. */

#ifndef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED   /* the stack's params.c owns mod_ui_slot */
static uint32_t mod_ui_slot;
#endif

static void lvt_mod_show(void)
{
    const track_t *t = &trk[song.sel];
    uint32_t base = mod_ui_slot * 3u;
    char b[8], s[2];
    s[0] = (char)('A' + mod_ui_slot);
    s[1] = 0;
    lvt_card_set(0, "SLOT", s, "", lvt_pal.primary, -1);
    lvt_card_set(1, "SRC", modd_mod_src_name((uint32_t)t->p[P_M1SRC + base]), "", lvt_pal.primary, -1);
    lvt_card_set(2, "DST", modd_mod_dst_name(t, (uint32_t)t->p[P_M1DST + base]), "", lvt_pal.primary, -1);
    lv_snprintf(b, sizeof b, "%+d", (int)t->p[P_M1AMT + base]);
    lvt_card_set(3, "AMT", b, "", lvt_pal.primary, (t->p[P_M1AMT + base] + 64) * 1000 / 127);
}

static void lvt_mod_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "MOD", "");
    lvt_mod_show();
}

static int lvt_mod_knob(uint32_t id, int32_t clicks)
{
    track_t *t = &trk[song.sel];
    uint32_t base;
    int16_t *target = 0;
    int16_t mn = 0, mx = 127, v;
    if (id == ENC_K1) {
        int32_t s = (int32_t)mod_ui_slot + clicks;
        if (s < 0) s = 3;
        if (s > 3) s = 0;
        mod_ui_slot = (uint32_t)s;
        nav_dirty = 1;
        return 1;
    }
    base = mod_ui_slot * 3u;
    if (id == ENC_K2)      target = &t->p[P_M1SRC + base];
    else if (id == ENC_K3) target = &t->p[P_M1DST + base];
    else if (id == ENC_K4) { target = &t->p[P_M1AMT + base]; mn = -64; mx = 63; }
    if (!target) return 0;
    v = *target + (int16_t)clicks;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    *target = v;
    nav_dirty = 1;
    return 1;
}

static const modd_page_t PAGE_MOD = {
    .name   = "MOD",
    .title  = "Mod",
    .view   = VIEW_LFO,
    .flags  = 0,
    .enter  = lvt_mod_enter,
    .status = lvt_mod_show,
    .knob   = lvt_mod_knob,
    .help_text = "K1  Slot\nK2  Source\nK3  Dest\nK4  Amount",
};
