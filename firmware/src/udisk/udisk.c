// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* UDisk component: mounts FAT32 from USB mass storage, provides directory
 * listing for the file browser page. */

static FATFS udisk_fs;
/* When the disk is used: the DISK setting allows it (udisk_enabled, a lock: off = the port never
 * goes host for a disk) and somebody needs it right now (udisk_needs: a page while it is open, a
 * copy while it runs). Both together are udisk_active, which holds the port in host mode
 * (modd_otg.h), lets the hub and enumeration code set the drive up, and lets udisk_poll() mount.
 * With no need left the port goes back to device mode, so USB-MIDI and SysEx work again.
 * A need only ever comes from something the user opened or started: nothing takes one at boot. */
static uint8_t udisk_enabled = 1;
static uint8_t udisk_needs;
static uint8_t udisk_active;
static uint8_t udisk_is_active(void) { return udisk_active; }
static uint8_t udisk_is_enabled(void) { return udisk_enabled; }
static void udisk_fmt_size(char *buf, uint32_t bytes);

static void udisk_update(void)
{
    uint8_t on = (udisk_enabled && udisk_needs) ? 1 : 0;
    if (on == udisk_active) return;
    udisk_active = on;                          /* (the unmount is udisk_poll's, in the USB task) */
    modd_log(on ? "udisk: activate" : "udisk: deactivate");
}

static void udisk_init(void)
{
    udisk_mounted = 0;
    udisk_nentry = 0;
    udisk_cursor = 0;
    udisk_scroll = 0;
    udisk_needs = 0;
    udisk_active = 0;
    udisk_cwd[0] = '/';
    udisk_cwd[1] = 0;
}

/* Take / give back a need. Returns 0 when the DISK setting is off: the need is still counted, so
 * every udisk_need() pairs with one udisk_release(), and the disk comes up if the setting is
 * turned on meanwhile. */
static int udisk_need(void)
{
    if (udisk_needs < 255u) udisk_needs++;
    udisk_update();
    return udisk_enabled;
}

static void udisk_release(void)
{
    if (udisk_needs) udisk_needs--;
    udisk_update();
}

static void udisk_set_enabled(uint8_t on)       /* the DISK setting (modd_sys_menu.h, and at boot) */
{
    udisk_enabled = on ? 1 : 0;
    udisk_update();
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
    if (!udisk_active) {
        if (udisk_mounted) {                    /* the last need went, or the setting was turned off */
            f_mount(0, "", 0);
            udisk_mounted = 0;
            udisk_nentry = 0;
            modd_log("udisk: unmounted");
        }
        return;
    }
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
static const char *udisk_list_status(void)
{
    if (udisk_mounted) return 0;
    return udisk_enabled ? "No USB Disk" : "USB Disk Off";
}
static const char *udisk_list_title(void) { return udisk_cwd; }

static void udisk_list_open(void)                /* the list on screen needs the disk */
{
    udisk_need();
    udisk_list_was_mounted = udisk_mounted;
    if (udisk_mounted)
        udisk_opendir(udisk_cwd);
}

static void udisk_list_close(void) { udisk_release(); }

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
