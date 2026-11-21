// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* DISK: the USB disk's state and volume, one row per field, the disk's state in the
 * footer (OFF, FAT32, ERR, WAIT, SCAN, ---). SELECT goes on to FILES.
 *
 *   mounted   Status, Type, Capacity, Volume, Files, Device (VID:PID), Cluster
 *   not       Status (Off, Error, Enumerating, Scanning, Not mounted), then where to turn
 *             the disk on, or the attached device's VID and PID
 *
 * The page needs the disk while it is open (udisk_need / udisk_release). Values are re-read when the disk's state changes and once a second. */

#define LVT_DISK_ROWS 7u

static struct {
    lv_obj_t *row[LVT_DISK_ROWS], *key[LVT_DISK_ROWS], *val[LVT_DISK_ROWS];
    uint32_t  shown_ms, state;
} lvt_disk;

static void lvt_disk_set(uint32_t i, const char *key, const char *fmt, ...)
{
    va_list ap;
    lv_obj_set_hidden(lvt_disk.row[i], false);
    lv_label_set_text_static(lvt_disk.key[i], key);
    va_start(ap, fmt);
    lv_label_set_text_vfmt(lvt_disk.val[i], fmt, ap);
    va_end(ap);
}

/* sectors of 512 bytes as "512 MB" or "14.9 GB" */
static void lvt_disk_size(uint32_t i, const char *key, uint32_t sectors)
{
    uint32_t mb = sectors / 2048u;
    if (mb >= 1024u)
        lvt_disk_set(i, key, "%u.%u GB", (unsigned)(mb / 1024u), (unsigned)((mb % 1024u) * 10u / 1024u));
    else
        lvt_disk_set(i, key, "%u MB", (unsigned)mb);
}

static uint32_t lvt_disk_state(void)
{
    return (uint32_t)udisk_is_enabled() | (uint32_t)(udisk_is_mounted() ? 2u : 0u) |
           (uint32_t)(udisk_active ? 4u : 0u) | ((uint32_t)uhost.state << 8);
}

static void lvt_disk_show(void)
{
    uint32_t n = 0, i;
    if (!udisk_is_mounted()) {
        const char *st = "Not mounted";
        if (!udisk_is_enabled())                 st = "Off";
        else if (uhost.state == UHOST_ERROR)     st = "Error";
        else if (uhost.state == UHOST_ATTACHED)  st = "Enumerating";
        else if (udisk_active)                   st = "Scanning";
        lvt_disk_set(n++, "Status", "%s", st);
        if (!udisk_is_enabled()) {               /* the DISK setting is off: say where it is */
            lvt_disk_set(n++, "Turn on", "Settings HW");
        } else if (uhost.vid) {
            lvt_disk_set(n++, "VID", "0x%04X", (unsigned)uhost.vid);
            lvt_disk_set(n++, "PID", "0x%04X", (unsigned)uhost.pid);
        }
    } else {
        const char *fs = udisk_fs.fs_type == FS_FAT32 ? "FAT32" : udisk_fs.fs_type == FS_FAT16 ? "FAT16"
                       : udisk_fs.fs_type == FS_FAT12 ? "FAT12" : "FAT??";
        lvt_disk_set(n++, "Status", "Mounted");
        lvt_disk_set(n++, "Type", "%s", fs);
        lvt_disk_size(n++, "Capacity", uhost.capacity);
        lvt_disk_size(n++, "Volume", (udisk_fs.n_fatent - 2u) * (uint32_t)udisk_fs.csize);
        lvt_disk_set(n++, "Files", "%u", (unsigned)udisk_nentry);
        lvt_disk_set(n++, "Device", "0x%04X:0x%04X", (unsigned)uhost.vid, (unsigned)uhost.pid);
        lvt_disk_set(n++, "Cluster", "%u sec", (unsigned)udisk_fs.csize);
    }
    for (i = n; i < LVT_DISK_ROWS; i++)
        lv_obj_set_hidden(lvt_disk.row[i], true);
    lvt_disk.shown_ms = fm1_ms;
    lvt_disk.state = lvt_disk_state();
    {
        const char *mid = "---";
        if (!udisk_is_enabled()) mid = "OFF";
        else if (udisk_is_mounted()) mid = "FAT32";
        else if (uhost.state == UHOST_ERROR) mid = "ERR";
        else if (uhost.state == UHOST_ATTACHED) mid = "WAIT";
        else if (udisk_active) mid = "SCAN";
        lvt_footer("", mid, "");
    }
}

static void lvt_disk_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *l;
    uint32_t i;
    l = lv_label_create(body);
    lv_obj_add_style(l, &lvt_st_small, 0);
    lv_obj_set_style_text_color(l, lvt_pal.accent, 0);
    lv_label_set_text_static(l, "DISK INFO");
    lvt_rule(body);
    for (i = 0; i < LVT_DISK_ROWS; i++) {
        lv_obj_t *row = lvt_row(body, 20);
        lvt_disk.key[i] = lvt_caption(row, "");
        lv_obj_set_flex_grow(lvt_disk.key[i], 1);
        lvt_disk.val[i] = lv_label_create(row);
        lvt_disk.row[i] = row;
    }
    udisk_need();
    lvt_disk_show();
}

static void lvt_disk_leave(void)
{
    udisk_release();
}

static void lvt_disk_status(void)
{
    if (lvt_disk_state() != lvt_disk.state || fm1_ms - lvt_disk.shown_ms >= 1000u)
        lvt_disk_show();
}

static const modd_page_t PAGE_DISK = {
    .name   = "DISK",
    .title  = "Disk",
    .view   = VIEW_DEMO,
    .flags  = PAGEF_SYSTEM,
    .enter  = lvt_disk_enter,
    .leave  = lvt_disk_leave,
    .status = lvt_disk_status,
};
