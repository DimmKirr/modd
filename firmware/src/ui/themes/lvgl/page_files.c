// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* FILES: the USB disk's files, the modd theme's FILES page (ui/themes/modd/page_files.c)
 * on the list widget (list.c) over the same source, LIST_UDISK (udisk/udisk.c, contract
 * modd_list.h). The disk is needed while the page is open (the source's open and close).
 *
 *   head    "USB DISK" and the folder on screen
 *   list    8 rows and the status line
 *   state   "USB Disk Off" / "SETTINGS HW: DISK", or "No USB Disk" / "OTG:HOST" while
 *           nothing is mounted
 *   footer  BACK, the disk's state (OFF, FAT32, ERR, WAIT, SCAN, ---), OPEN
 *
 * As in the modd theme: ALGO moves the cursor, OCT+ opens a folder, OCT- goes up and, at
 * the top, back HOME; SELECT goes on to DISK. */

static struct {
    lv_obj_t *head, *path, *state, *state_sub, *rows;
    lvt_list_t list;
    uint32_t  disk;                     /* enabled, mounted, host active: what the page shows */
} lvt_files;

static uint32_t lvt_files_disk(void)
{
    return (uint32_t)udisk_is_enabled() | (uint32_t)(udisk_is_mounted() ? 2u : 0u) |
           (uint32_t)(udisk_active ? 4u : 0u) | ((uint32_t)uhost.state << 8);
}

static void lvt_files_show(void)
{
    int mounted = udisk_is_mounted();
    const char *mid = "---";
    lv_obj_set_hidden(lvt_files.head, !mounted);
    lv_obj_set_hidden(lvt_files.path, !mounted);
    lv_obj_set_hidden(lvt_files.rows, !mounted);
    lv_obj_set_hidden(lvt_files.state, mounted);
    lv_obj_set_hidden(lvt_files.state_sub, mounted);
    if (mounted) {
        lvt_text(lvt_files.path, LIST_UDISK.title ? LIST_UDISK.title() : "/");
    } else {
        lvt_text(lvt_files.state, !udisk_is_enabled() ? "USB Disk Off" : "No USB Disk");
        lvt_text(lvt_files.state_sub, !udisk_is_enabled() ? "SETTINGS HW: DISK"
                 : udisk_active ? "OTG:HOST" : "OTG:DEVICE");
    }
    if (!udisk_is_enabled()) mid = "OFF";
    else if (mounted) mid = "FAT32";
    else if (uhost.state == UHOST_ERROR) mid = "ERR";
    else if (uhost.state == UHOST_ATTACHED) mid = "WAIT";
    else if (udisk_active) mid = "SCAN";
    lvt_footer("BACK", mid, "OPEN");
}

static void lvt_files_enter(void)
{
    lv_obj_t *body = lvt_page_begin();

    lv_obj_set_style_pad_row(body, 0, 0);
    lvt_files.head = lv_label_create(body);
    lv_obj_add_style(lvt_files.head, &lvt_st_small, 0);
    lv_obj_set_style_text_color(lvt_files.head, lvt_pal.accent, 0);
    lv_label_set_text_static(lvt_files.head, "USB DISK");
    lvt_files.path = lv_label_create(body);
    lv_obj_add_style(lvt_files.path, &lvt_st_small, 0);
    lv_obj_set_width(lvt_files.path, lv_pct(100));
    lv_label_set_long_mode(lvt_files.path, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text_static(lvt_files.path, "");
    lvt_rule(body);

    lvt_files.state = lv_label_create(body);
    lv_obj_add_style(lvt_files.state, &lvt_st_dim, 0);
    lv_obj_set_width(lvt_files.state, lv_pct(100));
    lv_obj_set_style_text_align(lvt_files.state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_margin_top(lvt_files.state, 40, 0);
    lv_label_set_text_static(lvt_files.state, "");
    lvt_files.state_sub = lv_label_create(body);
    lv_obj_add_style(lvt_files.state_sub, &lvt_st_small, 0);
    lv_obj_add_style(lvt_files.state_sub, &lvt_st_mid, 0);
    lv_obj_set_width(lvt_files.state_sub, lv_pct(100));
    lv_obj_set_style_text_align(lvt_files.state_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(lvt_files.state_sub, "");

    lvt_files.rows = lvt_box(body, LV_FLEX_FLOW_COLUMN);
    lvt_list_init(&lvt_files.list, &LIST_UDISK, ENC_ALGO);
    lvt_list_build(&lvt_files.list, lvt_files.rows, 8, 18);
    lvt_list_open(&lvt_files.list);
    lvt_files.disk = lvt_files_disk();
    lvt_files_show();
}

static void lvt_files_leave(void)
{
    lvt_list_close(&lvt_files.list);
}

static void lvt_files_status(void)
{
    uint32_t disk = lvt_files_disk();
    if (disk != lvt_files.disk) {
        lvt_files.disk = disk;
        lvt_files.list.dirty = 1;
        lvt_files_show();
    }
    if (lvt_files.list.dirty)
        lvt_files_show();               /* the path follows a folder change */
    lvt_list_poll(&lvt_files.list);
}

static int lvt_files_knob(uint32_t id, int32_t clicks)
{
    if (!udisk_is_mounted())
        return 0;                       /* SELECT, or no disk: the shell's (next page) */
    return lvt_list_input(&lvt_files.list, id, clicks);
}

static int lvt_files_button(uint32_t id, uint32_t down)
{
    if (lvt_list_button(&lvt_files.list, id, down))
        return 1;
    if (id == BTN_OCTDN && !down) {     /* nothing to go up to: the page closes */
        nav_open_view(VIEW_HOME);
        return 1;
    }
    return 0;
}

static const modd_page_t PAGE_FILES = {
    .name   = "FILES",
    .title  = "Files",
#ifdef CONFIG_HOME_PAGE_FILES
    .view   = VIEW_HOME,
    .flags  = PAGEF_HOME,
#else
    .view   = VIEW_DEMO,
    .flags  = PAGEF_SYSTEM,
#endif
    .enter  = lvt_files_enter,
    .leave  = lvt_files_leave,
    .status = lvt_files_status,
    .knob   = lvt_files_knob,
    .button = lvt_files_button,
    .help_text =
        "ALGO    Scroll list\n"
        "OCT+    Open / select\n"
        "OCT-    Back / close",
};
