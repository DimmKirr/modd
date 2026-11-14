// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd DISK INFO: USB disk stats page under VIEW_DEMO.
 * Shows volume info, capacity, free space, device VID/PID. */

#define MDISK_X_PAD    8
#define MDISK_HDR_H   30
#define MDISK_NROWS_MOUNTED   7
#define MDISK_NROWS_UNMOUNTED 3

static void mdisk_fmt_u32(char *out, uint32_t v)
{
    char tmp[12];
    uint32_t i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v && i < 11) { tmp[i++] = '0' + (char)(v % 10u); v /= 10u; }
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

static void mdisk_fmt_mb(char *out, uint32_t sectors)
{
    uint32_t mb = sectors / 2048u;
    if (mb >= 1024u) {
        uint32_t gb = mb / 1024u;
        uint32_t frac = (mb % 1024u) * 10u / 1024u;
        mdisk_fmt_u32(out, gb);
        uint32_t i = 0;
        while (out[i]) i++;
        out[i++] = '.';
        out[i++] = '0' + (char)(frac % 10u);
        out[i++] = ' ';
        out[i++] = 'G';
        out[i++] = 'B';
        out[i] = 0;
    } else {
        mdisk_fmt_u32(out, mb);
        uint32_t i = 0;
        while (out[i]) i++;
        out[i++] = ' ';
        out[i++] = 'M';
        out[i++] = 'B';
        out[i] = 0;
    }
}

static void mdisk_fmt_hex16(char *out, uint16_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    out[2] = hex[(v >> 12) & 0xF];
    out[3] = hex[(v >> 8) & 0xF];
    out[4] = hex[(v >> 4) & 0xF];
    out[5] = hex[v & 0xF];
    out[6] = 0;
}

static void mdisk_row(int32_t y, const char *label, const char *val,
                      uint16_t val_color)
{
    cv_text_fit(MDISK_X_PAD, y, TY_LB, label, T_DIM, T_BG, 80);
    cv_text_r(UI_W - MDISK_X_PAD, y, TY_LB, val, val_color, T_BG);
}

static void mdisk_draw(void)
{
    mt_panel_begin();

    cv_text_fit(MDISK_X_PAD, 0, TY_LB, "DISK INFO", T_ACCENT, T_BG, 100);
    cv_rect(0, 14, UI_W, 1, T_LINE);

    char buf[24];
    uint32_t row = 0;

    if (!udisk_is_mounted()) {
        uint32_t rh = (UI_PANEL_H - MDISK_HDR_H) / MDISK_NROWS_UNMOUNTED;

        const char *st = "Not mounted";
        if (uhost.state == UHOST_ERROR) st = "Error";
        else if (uhost.state == UHOST_ATTACHED) st = "Enumerating";
        else if (udisk_active) st = "Scanning";
        mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Status", st, T_MID);
        row++;

        if (uhost.vid) {
            mdisk_fmt_hex16(buf, uhost.vid);
            mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "VID", buf, T_TEXT);
            row++;
            mdisk_fmt_hex16(buf, uhost.pid);
            mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "PID", buf, T_TEXT);
        }
    } else {
        uint32_t rh = (UI_PANEL_H - MDISK_HDR_H) / MDISK_NROWS_MOUNTED;

        mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Status", "Mounted", T_THEME);
        row++;

        const char *fsname = "FAT??";
        if (udisk_fs.fs_type == FS_FAT32) fsname = "FAT32";
        else if (udisk_fs.fs_type == FS_FAT16) fsname = "FAT16";
        else if (udisk_fs.fs_type == FS_FAT12) fsname = "FAT12";
        mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Type", fsname, T_TEXT);
        row++;

        mdisk_fmt_mb(buf, uhost.capacity);
        mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Capacity", buf, T_TEXT);
        row++;

        {
            uint32_t total_clust = udisk_fs.n_fatent - 2u;
            uint32_t total_sec = total_clust * (uint32_t)udisk_fs.csize;
            mdisk_fmt_mb(buf, total_sec);
            mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Volume", buf, T_TEXT);
            row++;
        }

        mdisk_fmt_u32(buf, udisk_nentry);
        mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Files", buf, T_TEXT);
        row++;

        mdisk_fmt_hex16(buf, uhost.vid);
        {
            uint32_t i = 0;
            while (buf[i]) i++;
            buf[i++] = ':';
            mdisk_fmt_hex16(buf + i, uhost.pid);
        }
        mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Device", buf, T_TEXT);
        row++;

        mdisk_fmt_u32(buf, (uint32_t)udisk_fs.csize);
        {
            uint32_t i = 0;
            while (buf[i]) i++;
            buf[i++] = ' ';
            buf[i++] = 's';
            buf[i++] = 'e';
            buf[i++] = 'c';
            buf[i] = 0;
        }
        mdisk_row((int32_t)(MDISK_HDR_H + row * rh), "Cluster", buf, T_DIM);
    }

    mt_panel_end();

    {
        const char *mid = "---";
        if (udisk_is_mounted()) mid = "FAT32";
        else if (uhost.state == UHOST_ERROR) mid = "ERR";
        else if (uhost.state == UHOST_ATTACHED) mid = "WAIT";
        else if (udisk_active) mid = "SCAN";
        ui_footer("save", mid, "DISK");
    }
}

static uint8_t mdisk_was_mounted;

static void mdisk_status(void)
{
    uint8_t m = udisk_is_mounted() ? 1 : 0;
    if (m != mdisk_was_mounted) {
        mdisk_was_mounted = m;
        nav_dirty = 1;
    }
}

static void mdisk_enter(void)
{
    udisk_activate();
    mdisk_was_mounted = udisk_is_mounted() ? 1 : 0;
}

static void mdisk_leave(void)
{
    udisk_deactivate();
}

static const modd_page_t PAGE_DISK = {
    .name   = "DISK",
    .title  = "Disk",
    .view   = VIEW_DEMO,
    .flags  = PAGEF_SYSTEM,
    .enter  = mdisk_enter,
    .leave  = mdisk_leave,
    .draw   = mdisk_draw,
    .status = mdisk_status,
};
