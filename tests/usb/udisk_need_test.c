// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test for when the USB disk is used (firmware/src/udisk/udisk.c): the DISK setting as a
 * permission, the counted need, and the unmount that udisk_poll() does in the USB task.
 * FatFs, the USB host state and the log are stubs; udisk.c itself is the firmware's file. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CONFIG_UDISK 1

/* ---- what udisk.c takes from the rest of the firmware ---- */
typedef int FRESULT;
enum { FR_OK = 0 };
typedef struct { int fs_type; } FATFS;
typedef struct { int pos; } DIR;
typedef struct { char fname[13]; uint8_t fattrib; uint32_t fsize; } FILINFO;
#define AM_HID 0x02
#define AM_DIR 0x10

static int mounts, unmounts;
static FRESULT f_mount(FATFS *fs, const char *path, uint8_t opt)
{
    (void)path; (void)opt;
    if (fs) mounts++; else unmounts++;
    return FR_OK;
}
static FRESULT f_opendir(DIR *dp, const char *path) { (void)path; dp->pos = 0; return FR_OK; }
static FRESULT f_readdir(DIR *dp, FILINFO *fno)     /* two files, then the end */
{
    memset(fno, 0, sizeof *fno);
    if (dp->pos < 2) { strcpy(fno->fname, dp->pos ? "B.DOC" : "A.DOC"); fno->fsize = 1024; }
    dp->pos++;
    return FR_OK;
}
static FRESULT f_closedir(DIR *dp) { (void)dp; return FR_OK; }

enum { UHOST_DISCONNECTED, UHOST_ATTACHED, UHOST_ENUMERATED, UHOST_MSC_READY, UHOST_ERROR };
static struct { int state; } uhost;

static void modd_log(const char *s) { (void)s; }
static void modd_log_int(const char *s, int32_t v) { (void)s; (void)v; }

#include "modd_list.h"
#include "modd_udisk.h"
#include "udisk/udisk.c"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void reset(void)
{
    udisk_init();
    udisk_set_enabled(1);
    uhost.state = UHOST_DISCONNECTED;
    mounts = unmounts = 0;
}

/* The USB task: a drive is there as soon as the port is held in host mode for the disk. */
static void usb_task_pass(void)
{
    uhost.state = udisk_is_active() ? UHOST_MSC_READY : UHOST_DISCONNECTED;
    udisk_poll();
}

int main(void)
{
    /* The setting alone holds nothing: on, but nobody needs the disk. */
    reset();
    CHECK(udisk_is_enabled());
    CHECK(!udisk_is_active());
    usb_task_pass();
    CHECK(!udisk_is_mounted() && mounts == 0);

    /* A need brings it up, the release takes it down, and the unmount is the USB task's. */
    reset();
    CHECK(udisk_need() == 1);
    CHECK(udisk_is_active());
    usb_task_pass();
    CHECK(udisk_is_mounted() && mounts == 1);
    CHECK(udisk_nentry == 2);
    udisk_release();
    CHECK(!udisk_is_active());
    CHECK(udisk_is_mounted());                  /* not unmounted from the caller's task */
    usb_task_pass();
    CHECK(!udisk_is_mounted() && unmounts == 1);

    /* Two consumers: the disk stays until the last one lets go. */
    reset();
    udisk_need();
    udisk_need();
    usb_task_pass();
    udisk_release();
    usb_task_pass();
    CHECK(udisk_is_active() && udisk_is_mounted() && unmounts == 0);
    udisk_release();
    usb_task_pass();
    CHECK(!udisk_is_active() && !udisk_is_mounted() && unmounts == 1);

    /* One page hands over to the next (leave, then enter, no USB task pass between): no remount. */
    reset();
    udisk_need();
    usb_task_pass();
    udisk_release();
    udisk_need();
    usb_task_pass();
    CHECK(udisk_is_mounted() && mounts == 1 && unmounts == 0);
    udisk_release();

    /* Setting off: a need is counted but nothing comes up. */
    reset();
    udisk_set_enabled(0);
    CHECK(udisk_need() == 0);
    CHECK(!udisk_is_active());
    usb_task_pass();
    CHECK(!udisk_is_mounted() && mounts == 0);
    udisk_set_enabled(1);                       /* turned on while the consumer still waits */
    CHECK(udisk_is_active());
    usb_task_pass();
    CHECK(udisk_is_mounted() && mounts == 1);

    /* Setting turned off while a consumer holds the disk: it goes down. */
    udisk_set_enabled(0);
    CHECK(!udisk_is_active());
    usb_task_pass();
    CHECK(!udisk_is_mounted() && unmounts == 1);
    udisk_release();
    CHECK(!udisk_is_active());

    /* A release without a need does not wrap the count. */
    reset();
    udisk_release();
    CHECK(!udisk_is_active());
    CHECK(udisk_need() == 1 && udisk_is_active());
    udisk_release();
    CHECK(!udisk_is_active());

    /* The list source (the Files page) takes the need in open() and gives it back in close(). */
    reset();
    LIST_UDISK.open();
    CHECK(udisk_is_active());
    usb_task_pass();
    CHECK(LIST_UDISK.poll() == 1 && LIST_UDISK.count() == 2);
    CHECK(LIST_UDISK.status() == 0);
    LIST_UDISK.close();
    CHECK(!udisk_is_active());
    usb_task_pass();
    CHECK(strcmp(LIST_UDISK.status(), "No USB Disk") == 0);
    udisk_set_enabled(0);
    CHECK(strcmp(LIST_UDISK.status(), "USB Disk Off") == 0);

    /* The drive pulled out while it is needed: unmounted, still wanted. */
    reset();
    udisk_need();
    usb_task_pass();
    uhost.state = UHOST_DISCONNECTED;
    udisk_poll();
    CHECK(!udisk_is_mounted() && udisk_is_active());

    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
