// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Dock disk source: .dock files on the mounted USB disk (udisk.c, FatFs) for dock_install().
 * The root directory is scanned for files whose header says HRBR (modd_dock.h), whatever their
 * 8.3 name: the list shows the module's own name. A file is streamed to the installer through a
 * dock_src_t over f_read. */

#define DOCK_DISK_MAX 16

typedef struct {
    char fname[13];                             /* the 8.3 name, for f_open */
    char name[24];                              /* hb_file_t.name */
    uint32_t size;
} dock_disk_entry_t;

static dock_disk_entry_t dock_disk[DOCK_DISK_MAX];
static uint32_t dock_disk_n;
static FIL dock_fil;

static void dock_disk_scan(void)
{
    DIR dp;
    FILINFO fno;
    dock_disk_n = 0;
    if (!udisk_is_mounted() || f_opendir(&dp, "/") != FR_OK)
        return;
    while (dock_disk_n < DOCK_DISK_MAX) {
        hb_file_t h;
        UINT br;
        char path[16];
        uint32_t i;
        if (f_readdir(&dp, &fno) != FR_OK || fno.fname[0] == 0)
            break;
        if ((fno.fattrib & (AM_DIR | AM_HID)) || fno.fsize < sizeof h || fno.fsize > HB_MOD_RAM_SIZE)
            continue;
        path[0] = '/';
        for (i = 0; i < 12u && fno.fname[i]; i++) path[i + 1u] = fno.fname[i];
        path[i + 1u] = 0;
        if (f_open(&dock_fil, path, FA_READ) != FR_OK)
            continue;
        if (f_read(&dock_fil, &h, sizeof h, &br) == FR_OK && br == sizeof h &&
            h.magic == HB_MAGIC && h.abi == HB_ABI && !(h.flags & (HB_F_PROBE | HB_F_BOOT))) {
            dock_disk_entry_t *e = &dock_disk[dock_disk_n++];
            dock_str_cpy(e->fname, path + 1, sizeof e->fname);
            dock_str_cpy(e->name, h.name, sizeof e->name);
            e->size = (uint32_t)fno.fsize;
        }
        f_close(&dock_fil);
    }
    f_closedir(&dp);
    modd_log_int("dock: disk modules ", (int32_t)dock_disk_n);
}

static int dock_disk_read(void *ctx, uint8_t *dst, uint32_t n)
{
    UINT br;
    (void)ctx;
    return f_read(&dock_fil, dst, n, &br) == FR_OK && br == n ? 0 : -1;
}

/* install disk entry i; *idx: its place in dock_mods when DOCK_OK */
static uint32_t dock_disk_install(uint32_t i, uint32_t *idx)
{
    dock_src_t src;
    char path[16];
    uint32_t rc, k;
    *idx = DOCK_NONE;
    if (!udisk_is_mounted())
        return DOCK_NO_DISK;
    if (i >= dock_disk_n)
        return DOCK_READ;
    path[0] = '/';
    for (k = 0; k < 12u && dock_disk[i].fname[k]; k++) path[k + 1u] = dock_disk[i].fname[k];
    path[k + 1u] = 0;
    if (f_open(&dock_fil, path, FA_READ) != FR_OK)
        return DOCK_READ;
    src.read = dock_disk_read;
    src.ctx = 0;
    src.size = dock_disk[i].size;
    rc = dock_install(&src, idx);
    f_close(&dock_fil);
    return rc;
}
