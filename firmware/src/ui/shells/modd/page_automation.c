/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* AUTOMATION: per-step parameter locks for the selected track.
 * Lock storage (auto_locks, auto_params, AUTO_NPARAMS) lives in seq.c
 * so the sequencer can apply locks during playback. */

static const char *auto_pnames[AUTO_NPARAMS] = {
    "FLT", "RES", "SHP", "DIST", "DLY", "REV", "CHO", "LVL",
};

static uint32_t auto_step;
static uint32_t auto_pidx;

static void lvt_automation_show(void)
{
    auto_ensure_init();
    char sb[8], vb[8];
    uint32_t t = modd_sel() < NTRK ? modd_sel() : 0;
    uint8_t lv = auto_locks[t][auto_step][auto_pidx];
    int locked = (lv != 0xFF);

    lv_snprintf(sb, sizeof sb, "%u", (unsigned)(auto_step + 1));
    lvt_card_set(0, "STEP", sb, "", lvt_pal.primary, (int32_t)auto_step * 1000 / 63);

    lvt_card_set(1, "PARAM", auto_pnames[auto_pidx], "", lvt_pal.primary, -1);

    if (locked) {
        lv_snprintf(vb, sizeof vb, "%u", (unsigned)lv);
        lvt_card_set(2, "VALUE", vb, "", lvt_pal.primary, lv * 1000 / 127);
    } else {
        lvt_card_set(2, "VALUE", "--", "", lvt_pal.dim, -1);
    }

    lvt_card_set(3, "LOCK", locked ? "ON" : "OFF", "", locked ? lvt_pal.primary : lvt_pal.dim, -1);
}

static void lvt_automation_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "AUTOMATION", "");
    lvt_automation_show();
}

static int lvt_automation_knob(uint32_t id, int32_t clicks)
{
    auto_ensure_init();
    uint32_t t = modd_sel() < NTRK ? modd_sel() : 0;

    if (id == ENC_K1) {
        int32_t v = (int32_t)auto_step + clicks;
        if (v < 0) v = 0;
        if (v > 63) v = 63;
        auto_step = (uint32_t)v;
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_K2) {
        int32_t v = (int32_t)auto_pidx + clicks;
        if (v < 0) v = AUTO_NPARAMS - 1;
        if (v >= AUTO_NPARAMS) v = 0;
        auto_pidx = (uint32_t)v;
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_K3) {
        uint8_t lv = auto_locks[t][auto_step][auto_pidx];
        if (lv == 0xFF) lv = 64;
        int32_t v = (int32_t)lv + clicks;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        auto_locks[t][auto_step][auto_pidx] = (uint8_t)v;
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_K4 && clicks != 0) {
        uint8_t lv = auto_locks[t][auto_step][auto_pidx];
        if (lv == 0xFF)
            auto_locks[t][auto_step][auto_pidx] = (uint8_t)modd_tpi(t, auto_params[auto_pidx]);
        else
            auto_locks[t][auto_step][auto_pidx] = 0xFF;
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static const modd_page_t PAGE_AUTOMATION = {
    .name = "AUTO", .title = "Automation", .view = VIEW_SEQ, .flags = 0,
    .enter = lvt_automation_enter, .status = lvt_automation_show,
    .knob = lvt_automation_knob,
    .help_text = "K1  Step\nK2  Param\nK3  Value\nK4  Lock on/off",
};
