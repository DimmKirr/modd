// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd FILES: USB disk file browser under VIEW_DEMO.
 * Uses the shared udisk.c FAT32 layer.
 * ALGO scrolls files, OCT+ enters dir, OCT- goes up (or closes). */

#define MFILES_ROW_H    18
#define MFILES_X_PAD    8
#define MFILES_VISIBLE  (MT_H_PANEL / MFILES_ROW_H)

static void mfiles_draw_list(void)
{
    uint32_t i;

    for (i = 0; i < MFILES_VISIBLE && (udisk_scroll + i) < udisk_nentry; i++) {
        uint32_t idx = udisk_scroll + i;
        const udisk_entry_t *e = &udisk_entries[idx];
        int32_t y = (int32_t)i * MFILES_ROW_H;
        uint16_t fg = (idx == udisk_cursor) ? T_THEME : T_TEXT;
        uint16_t bg = (idx == udisk_cursor) ? T_SURF : T_BG;

        if (idx == udisk_cursor)
            cv_rrect(2, y, MT_W - 4, MFILES_ROW_H - 2, 3, T_SURF, T_BG);

        if (e->is_dir) {
            cv_text_fit(MFILES_X_PAD, y + 2, TY_LB, "/", T_ACCENT, bg, 8);
            cv_text_fit(MFILES_X_PAD + 8, y + 2, TY_LB, e->name, fg, bg, 160);
        } else {
            cv_text_fit(MFILES_X_PAD, y + 2, TY_LB, e->name, fg, bg, 160);
            char sz[12];
            udisk_fmt_size(sz, e->size);
            cv_text_r(MT_W - MFILES_X_PAD, y + 2, TY_LB, sz, T_DIM, bg);
        }
    }

    if (udisk_nentry == 0)
        cv_text_c(MT_W / 2, ty_y(40, TY_LB), TY_LB, "(empty)", T_DIM, T_BG);
}

static void mfiles_draw(void)
{
    mt_panel_begin();

    if (!udisk_is_mounted()) {
        cv_text_c(MT_W / 2, ty_y(MT_H_PANEL / 2 - 10, TY_TX), TY_TX,
                  "No USB Disk", T_DIM, T_BG);
        cv_text_c(MT_W / 2, ty_y(MT_H_PANEL / 2 + 10, TY_LB), TY_LB,
                  udisk_active ? "OTG:HOST" : "OTG:DEVICE", T_MID, T_BG);
    } else {
        /* path bar at top of panel */
        cv_text_fit(MFILES_X_PAD, 0, TY_LB, "USB DISK", T_ACCENT, T_BG, 80);
        cv_text_fit(MFILES_X_PAD, 14, TY_LB, udisk_cwd, T_TEXT, T_BG, MT_W - 16);
        cv_rect(0, 28, MT_W, 1, T_LINE);
        /* shift list below the path bar */
        /* we draw into the panel canvas, offset rows by 30 px */
        {
            uint32_t i;
            for (i = 0; i < MFILES_VISIBLE - 2u && (udisk_scroll + i) < udisk_nentry; i++) {
                uint32_t idx = udisk_scroll + i;
                const udisk_entry_t *e = &udisk_entries[idx];
                int32_t y = 30 + (int32_t)i * MFILES_ROW_H;
                uint16_t fg = (idx == udisk_cursor) ? T_THEME : T_TEXT;
                uint16_t bg = (idx == udisk_cursor) ? T_SURF : T_BG;

                if (idx == udisk_cursor)
                    cv_rrect(2, y, MT_W - 4, MFILES_ROW_H - 2, 3, T_SURF, T_BG);

                if (e->is_dir) {
                    cv_text_fit(MFILES_X_PAD, y + 2, TY_LB, "/", T_ACCENT, bg, 8);
                    cv_text_fit(MFILES_X_PAD + 8, y + 2, TY_LB, e->name, fg, bg, 160);
                } else {
                    cv_text_fit(MFILES_X_PAD, y + 2, TY_LB, e->name, fg, bg, 160);
                    char sz[12];
                    udisk_fmt_size(sz, e->size);
                    cv_text_r(MT_W - MFILES_X_PAD, y + 2, TY_LB, sz, T_DIM, bg);
                }
            }
            if (udisk_nentry == 0)
                cv_text_c(MT_W / 2, ty_y(60, TY_LB), TY_LB, "(empty)", T_DIM, T_BG);
        }
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
    if (!udisk_is_mounted())
        return 0;
    if (id == ENC_ALGO) {
        int32_t c = (int32_t)udisk_cursor + clicks;
        if (c < 0) c = 0;
        if (c >= (int32_t)udisk_nentry) c = (int32_t)udisk_nentry - 1;
        if (c < 0) c = 0;
        udisk_cursor = (uint32_t)c;
        if (udisk_cursor < udisk_scroll)
            udisk_scroll = udisk_cursor;
        if (udisk_cursor >= udisk_scroll + MFILES_VISIBLE - 2u)
            udisk_scroll = udisk_cursor - (MFILES_VISIBLE - 2u) + 1;
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static int mfiles_button(uint32_t id, uint32_t down)
{
    if (!down) return 0;

    if (id == BTN_OCTDN) {
        if (udisk_is_mounted() && udisk_go_up() == 0) {
            nav_dirty = 1;
            return 1;
        }
        nav_open_view(VIEW_HOME);
        return 1;
    }

    if (!udisk_is_mounted()) return 0;

    if (id == BTN_OCTUP) {
        if (udisk_cursor < udisk_nentry) {
            const udisk_entry_t *e = &udisk_entries[udisk_cursor];
            if (e->is_dir) {
                udisk_chdir(e->name);
                nav_dirty = 1;
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

static uint8_t mfiles_was_mounted;

static void mfiles_status(void)
{
    uint8_t m = udisk_is_mounted() ? 1 : 0;
    if (m != mfiles_was_mounted) {
        mfiles_was_mounted = m;
        if (m) udisk_opendir(udisk_cwd);
        nav_dirty = 1;
    }
}

static void mfiles_enter(void)
{
    udisk_activate();
    mfiles_was_mounted = udisk_is_mounted() ? 1 : 0;
    if (mfiles_was_mounted)
        udisk_opendir(udisk_cwd);
}

static void mfiles_leave(void)
{
    udisk_deactivate();
}

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
