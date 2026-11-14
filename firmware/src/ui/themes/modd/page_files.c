// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd FILES: USB disk file browser under VIEW_DEMO: the list widget (list.c) over the
 * directory source LIST_UDISK (udisk.c). ALGO scrolls, OCT+ enters a folder, OCT- goes up
 * (at the top: closes). */

#define MFILES_X_PAD   8
#define MFILES_ROW_H   18
#define MFILES_LIST_Y  30

static ui_list_t mfiles_list;

static void mfiles_draw(void)
{
    mt_panel_begin();
    if (!udisk_is_mounted()) {
        cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 - 10, TY_TX), TY_TX,
                  "No USB Disk", T_DIM, T_BG);
        cv_text_c(UI_W / 2, ty_y(UI_PANEL_H / 2 + 10, TY_LB), TY_LB,
                  udisk_active ? "OTG:HOST" : "OTG:DEVICE", T_MID, T_BG);
    } else {
        const char *path = LIST_UDISK.title ? LIST_UDISK.title() : "/";
        cv_text_fit(MFILES_X_PAD, 0, TY_LB, "USB DISK", T_ACCENT, T_BG, 80);
        cv_text_fit(MFILES_X_PAD, 14, TY_LB, path, T_TEXT, T_BG, UI_W - 16);
        cv_rect(0, 28, UI_W, 1, T_LINE);
        ui_list_draw(&mfiles_list, MFILES_LIST_Y, UI_PANEL_H - MFILES_LIST_Y);
    }
    mt_panel_end();

    {
        const char *mid = "---";
        if (udisk_is_mounted()) mid = "FAT32";
        else if (uhost.state == UHOST_ERROR) mid = "ERR";
        else if (uhost.state == UHOST_ATTACHED) mid = "WAIT";
        else if (udisk_active) mid = "SCAN";
        ui_footer("save", mid, "FILES");
    }
}

static int mfiles_knob(uint32_t id, int32_t clicks)
{
    if (!udisk_is_mounted() || id != ENC_ALGO)
        return 0;
    ui_list_knob(&mfiles_list, clicks);
    return 1;
}

static int mfiles_button(uint32_t id, uint32_t down)
{
    if (ui_list_button(&mfiles_list, id, down))
        return 1;
    if (id == BTN_OCTDN && !down) {             /* nothing to go up to: the page closes */
        nav_open_view(VIEW_HOME);
        return 1;
    }
    return 0;
}

static void mfiles_status(void) { ui_list_poll(&mfiles_list); }

static void mfiles_enter(void)
{
    ui_list_init(&mfiles_list, &LIST_UDISK, MFILES_ROW_H);
    ui_list_open(&mfiles_list);
}

static void mfiles_leave(void) { ui_list_close(&mfiles_list); }

static const modd_page_t PAGE_FILES = {
    .name   = "FILES",
    .title  = "Files",
    .view   = VIEW_DEMO,
    .flags  = PAGEF_SYSTEM,
    .enter  = mfiles_enter,
    .leave  = mfiles_leave,
    .draw   = mfiles_draw,
    .status = mfiles_status,
    .knob   = mfiles_knob,
    .button = mfiles_button,
};
