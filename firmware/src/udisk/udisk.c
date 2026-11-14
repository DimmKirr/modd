// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* UDisk component: mounts FAT32 from USB mass storage, provides directory
 * listing for the file browser page. */

static FATFS udisk_fs;
static uint8_t udisk_active;
static void udisk_fmt_size(char *buf, uint32_t bytes);

static void udisk_init(void)
{
    udisk_mounted = 0;
    udisk_nentry = 0;
    udisk_cursor = 0;
    udisk_scroll = 0;
    udisk_active = 0;
    udisk_cwd[0] = '/';
    udisk_cwd[1] = 0;
}

static void udisk_activate(void)
{
    if (udisk_active) return;
    udisk_active = 1;
    modd_log("udisk: activate");
}

static void udisk_deactivate(void)
{
    if (!udisk_active) return;
    if (udisk_mounted) {
        f_mount(0, "", 0);
        udisk_mounted = 0;
        udisk_nentry = 0;
        modd_log("udisk: unmounted");
    }
    udisk_active = 0;
    modd_log("udisk: deactivate");
}

static int udisk_opendir(const char *path);

static void udisk_try_mount(void)
{
    if (uhost.state != UHOST_MSC_READY) return;
    FRESULT r = f_mount(&udisk_fs, "", 1);
    if (r == FR_OK) {
        udisk_mounted = 1;
        udisk_cwd[0] = '/';
        udisk_cwd[1] = 0;
        udisk_opendir("/");
        modd_log_int("udisk: mounted, files=", (int32_t)udisk_nentry);
    } else {
        modd_log_int("udisk: mount fail ", (int32_t)r);
    }
}

static void udisk_poll(void)
{
    if (!udisk_active) return;
    if (!udisk_mounted && uhost.state == UHOST_MSC_READY)
        udisk_try_mount();
    if (udisk_mounted && uhost.state == UHOST_DISCONNECTED) {
        udisk_mounted = 0;
        udisk_nentry = 0;
        f_mount(0, "", 0);
        modd_log("udisk: removed");
    }
}

static int udisk_opendir(const char *path)
{
    DIR dp;
    FILINFO fno;
    FRESULT r;
    udisk_nentry = 0;
    udisk_cursor = 0;
    udisk_scroll = 0;
    r = f_opendir(&dp, path);
    if (r != FR_OK) return -1;
    while (udisk_nentry < UDISK_DIR_MAX) {
        r = f_readdir(&dp, &fno);
        if (r != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_HID) continue;
        udisk_entry_t *e = &udisk_entries[udisk_nentry];
        uint32_t i;
        for (i = 0; i < UDISK_NAME_MAX - 1 && fno.fname[i]; i++)
            e->name[i] = fno.fname[i];
        e->name[i] = 0;
        e->size = (uint32_t)fno.fsize;
        e->is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;
        if (e->is_dir) e->detail[0] = 0; else udisk_fmt_size(e->detail, e->size);
        udisk_nentry++;
    }
    f_closedir(&dp);
    return 0;
}

static void udisk_path_append(char *cwd, const char *name)
{
    uint32_t len = 0;
    while (cwd[len]) len++;
    if (len > 1 && len < UDISK_PATH_MAX - 2) cwd[len++] = '/';
    uint32_t i;
    for (i = 0; name[i] && len < UDISK_PATH_MAX - 1; i++)
        cwd[len++] = name[i];
    cwd[len] = 0;
}

static int udisk_chdir(const char *name)
{
    char path[UDISK_PATH_MAX];
    uint32_t i;
    for (i = 0; i < UDISK_PATH_MAX - 1 && udisk_cwd[i]; i++)
        path[i] = udisk_cwd[i];
    path[i] = 0;
    udisk_path_append(path, name);
    if (udisk_opendir(path) == 0) {
        for (i = 0; i < UDISK_PATH_MAX && path[i]; i++)
            udisk_cwd[i] = path[i];
        udisk_cwd[i] = 0;
        return 0;
    }
    return -1;
}

static int udisk_go_up(void)
{
    if (udisk_cwd[0] == '/' && udisk_cwd[1] == 0) return -1;
    uint32_t len = 0;
    while (udisk_cwd[len]) len++;
    while (len > 1 && udisk_cwd[len - 1] != '/') len--;
    if (len > 1) len--;
    udisk_cwd[len] = 0;
    if (len == 0) { udisk_cwd[0] = '/'; udisk_cwd[1] = 0; }
    return udisk_opendir(udisk_cwd);
}

static void udisk_u32_to_str(char *buf, uint32_t v)
{
    char tmp[12];
    uint32_t j = 0, i = 0;
    do {
        tmp[j++] = '0' + (char)(v % 10u);
        v /= 10u;
    } while (v && j < 11);
    while (j) buf[i++] = tmp[--j];
    buf[i] = 0;
}

static void udisk_fmt_size(char *buf, uint32_t bytes)
{
    if (bytes < 1024u) {
        udisk_u32_to_str(buf, bytes);
        uint32_t i = 0;
        while (buf[i]) i++;
        buf[i++] = 'B'; buf[i] = 0;
    } else if (bytes < 1024u * 1024u) {
        udisk_u32_to_str(buf, bytes / 1024u);
        uint32_t i = 0;
        while (buf[i]) i++;
        buf[i++] = 'K'; buf[i] = 0;
    } else {
        udisk_u32_to_str(buf, bytes / (1024u * 1024u));
        uint32_t i = 0;
        while (buf[i]) i++;
        buf[i++] = 'M'; buf[i] = 0;
    }
}

/* ---- the directory as a list source (modd_list.h): answers from udisk_entries[] ---- */
static uint8_t udisk_list_was_mounted;

static uint32_t udisk_list_count(void) { return udisk_mounted ? udisk_nentry : 0; }

static void udisk_list_entry(uint32_t i, modd_list_entry_t *e)
{
    const udisk_entry_t *u = &udisk_entries[i];
    e->name = u->name;
    e->detail = u->detail;
    e->kind = u->is_dir ? LIST_FOLDER : LIST_FILE;
}

static int udisk_list_enter(uint32_t i)
{
    return udisk_entries[i].is_dir ? udisk_chdir(udisk_entries[i].name) : -1;
}

static int udisk_list_up(void) { return udisk_go_up(); }
static const char *udisk_list_status(void) { return udisk_mounted ? 0 : "No USB Disk"; }
static const char *udisk_list_title(void) { return udisk_cwd; }

static void udisk_list_open(void)
{
    udisk_activate();
    udisk_list_was_mounted = udisk_mounted;
    if (udisk_mounted)
        udisk_opendir(udisk_cwd);
}

static void udisk_list_close(void) { udisk_deactivate(); }

static int udisk_list_poll(void)
{
    if (udisk_mounted != udisk_list_was_mounted) {
        udisk_list_was_mounted = udisk_mounted;
        if (udisk_mounted)
            udisk_opendir(udisk_cwd);
        return 1;
    }
    return 0;
}

static const modd_list_src_t LIST_UDISK = {
    .count  = udisk_list_count,
    .entry  = udisk_list_entry,
    .enter  = udisk_list_enter,
    .up     = udisk_list_up,
    .status = udisk_list_status,
    .title  = udisk_list_title,
    .open   = udisk_list_open,
    .close  = udisk_list_close,
    .poll   = udisk_list_poll,
};
