// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* The instruments list (modd_list.h): row 0 modd's engines, then the installed dock modules
 * (dock.c), then the .dock files on the USB disk (dock_disk.c). Everything answers from RAM:
 * the disk is scanned when it mounts and the result is kept across leaving the page, so the
 * list is back at once while the disk re-mounts behind it. OCT+ stops, starts, or installs
 * then starts. */

#ifdef CONFIG_DOCK_DISK
static uint8_t dock_list_was_mounted;
#endif

static uint32_t dock_list_disk_n(void)
{
#ifdef CONFIG_DOCK_DISK
    return dock_disk_n;
#else
    return 0;
#endif
}

static uint32_t dock_list_count(void) { return 1u + dock_n_mods + dock_list_disk_n(); }

static void dock_list_entry(uint32_t i, modd_list_entry_t *e)
{
    if (i == 0) {
        e->name = "MODD ENGINES";
        e->detail = dock_running() ? "" : "ACTIVE";
        e->kind = dock_running() ? LIST_INSTALLED : LIST_ACTIVE;
    } else if (i <= dock_n_mods) {
        const dock_entry_t *m = &dock_mods[i - 1u];
        e->name = m->name;
        if (i - 1u == dock_cur) { e->detail = "ACTIVE"; e->kind = LIST_ACTIVE; }
        else if (m->state == DOCK_OK) { e->detail = "FLASH"; e->kind = LIST_INSTALLED; }
        else { e->detail = DOCK_NAME[m->state]; e->kind = LIST_INFO; }
    } else {
#ifdef CONFIG_DOCK_DISK
        e->name = dock_disk[i - 1u - dock_n_mods].name;
#else
        e->name = "?";
#endif
        e->detail = "DISK";
        e->kind = LIST_FILE;
    }
}

static const char *dock_list_act(uint32_t i)
{
    uint32_t rc = DOCK_OK;
    if (i == 0) {
        dock_stop();
    } else if (i <= dock_n_mods) {
        rc = dock_start(i - 1u);
    } else {
#ifdef CONFIG_DOCK_DISK
        uint32_t idx;
        rc = dock_disk_install(i - 1u - dock_n_mods, &idx);
        if (rc == DOCK_OK)
            rc = dock_start(idx);
        dock_disk_scan();                       /* (the disk may have changed under the install) */
#else
        rc = DOCK_NO_DISK;
#endif
    }
    return DOCK_NAME[rc < DOCK_N_STATES ? rc : DOCK_BAD_HEAD];
}

static const char *dock_list_act2(uint32_t i)   /* OCT+ held on an installed module: delete it */
{
    uint32_t rc;
    if (i == 0 || i > dock_n_mods)
        return 0;
    rc = dock_delete(i - 1u);
    return rc == DOCK_OK ? "DELETED" : DOCK_NAME[rc < DOCK_N_STATES ? rc : DOCK_BAD_HEAD];
}

static const char *dock_list_status(void)
{
#ifdef CONFIG_DOCK_DISK
    if (!udisk_is_mounted())
        return dock_disk_n ? "DISK OUT: LIST KEPT" : "INSERT USB DISK";
    if (dock_disk_n == 0)
        return "NO .DOCK ON DISK";
#endif
    return 0;                                   /* the widget shows the hints */
}

static void dock_list_open(void)
{
#ifdef CONFIG_DOCK_DISK
    udisk_activate();
    dock_list_was_mounted = udisk_is_mounted() ? 1 : 0;
    if (dock_list_was_mounted)
        dock_disk_scan();
#endif
}

static void dock_list_close(void)
{
#ifdef CONFIG_DOCK_DISK
    udisk_deactivate();                         /* (dock_disk[] stays: the cache) */
#endif
}

static int dock_list_poll(void)
{
#ifdef CONFIG_DOCK_DISK
    uint8_t m = udisk_is_mounted() ? 1 : 0;
    if (m != dock_list_was_mounted) {
        dock_list_was_mounted = m;
        if (m) dock_disk_scan();
        return 1;
    }
#endif
    return 0;
}

static const modd_list_src_t LIST_INSTR = {
    .count  = dock_list_count,
    .entry  = dock_list_entry,
    .actions = {
        [LIST_SHORT_PLUS] = {0, "select", dock_list_act},
        [LIST_LONG_PLUS]  = {0, "delete", dock_list_act2},
    },
    .status = dock_list_status,
    .open   = dock_list_open,
    .close  = dock_list_close,
    .poll   = dock_list_poll,
};
