/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* PROJECT: project slots (slot, load, save). */

static void lvt_project_show(void)
{
    char b[16];
    int32_t s = modd_gp(G_SLOT);
    lv_snprintf(b, sizeof b, "%d", (int)s);
    lvt_card_set(0, "SLOT", b, "", lvt_pal.primary, s * 1000 / 15);

    if (project_used((uint32_t)s)) {
        char name[16];
        project_name((uint32_t)s, name);
        lvt_card_set(1, "NAME", name[0] ? name : "---", "", lvt_pal.primary, -1);
    } else {
        lvt_card_set(1, "NAME", "EMPTY", "", lvt_pal.dim, -1);
    }

    lvt_card_set(2, "LOAD", project_used((uint32_t)s) ? "OK" : "--", "", lvt_pal.primary, -1);
    lvt_card_set(3, "SAVE", "", "", lvt_pal.primary, -1);
}

static void lvt_project_enter(void)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", "PROJECT", "");
    lvt_project_show();
}

static int lvt_project_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_K1) return lvt_gadd(G_SLOT, clicks, 0, 15);
    if (id == ENC_K3 && clicks > 0) {
        uint32_t slot = (uint32_t)modd_gp(G_SLOT);
        if (project_used(slot)) {
            project_load(slot);
            toast_status("LOADED");
        }
        nav_dirty = 1;
        return 1;
    }
    if (id == ENC_K4 && clicks > 0) {
        uint32_t slot = (uint32_t)modd_gp(G_SLOT);
        project_save(slot);
        toast_status("SAVED");
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static const modd_page_t PAGE_PROJECT = {
    .name = "PROJECT", .title = "Project", .view = VIEW_SAVE, .flags = 0,
    .enter = lvt_project_enter, .status = lvt_project_show, .knob = lvt_project_knob,
    .help_text = "K1  Slot\nK3  Load\nK4  Save",
};
