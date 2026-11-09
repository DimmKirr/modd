// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
// Origin: keremimo/melodee firmware/src/ui_boot.c @ v1.4.5 (398ada1e)
//         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/ui_boot.c
/* Boot maintenance progress screen.  Synchronous, no timer IRQ or audio needed.
 * Uses only boot-level primitives (boot_text_1x, lcd_fill, lcd_sync) which are
 * available before the main UI is loaded.  Included before boot/ota.c so the
 * BOOT_WORK_* macros override the no-op defaults. */

#define BOOT_WORK_BG    0x0000u  /* black */
#define BOOT_WORK_FG    0xFFFFu  /* white */
#define BOOT_WORK_ACC   0xFD20u  /* orange accent */
#define BOOT_WORK_DIM   0x4208u  /* dark grey (progress track) */

static struct {
    const char *title;
    uint16_t done, total;
    uint8_t state;   /* 0 complete, 1 working, 2 failed */
} boot_work;

static void boot_work_draw(void)
{
    lcd_fill(0, 0, 240, 240, BOOT_WORK_BG);

    boot_text_1x(80, boot_work.title, BOOT_WORK_FG, BOOT_WORK_BG);

    if (boot_work.state == 2u) {
        boot_text_1x(110, "Could not finish", BOOT_WORK_ACC, BOOT_WORK_BG);
        boot_text_1x(130, "Restart to retry", BOOT_WORK_DIM, BOOT_WORK_BG);
    } else {
        char count[24];
        fmt_int(count, boot_work.done);
        uint32_t n = str_len(count);
        str_cpy(count + n, " / ", sizeof count - n);
        fmt_int(count + str_len(count), boot_work.total);
        boot_text_1x(110, count, BOOT_WORK_ACC, BOOT_WORK_BG);

        uint32_t bar_w = boot_work.total ? 200u * boot_work.done / boot_work.total : 0u;
        lcd_fill(20, 128, 200, 4, BOOT_WORK_DIM);
        if (bar_w)
            lcd_fill(20, 128, bar_w, 4, BOOT_WORK_ACC);

        const char *status = boot_work.state ? "Keep power on" : "Complete";
        boot_text_1x(145, status, BOOT_WORK_DIM, BOOT_WORK_BG);
    }

    lcd_sync();
#ifdef FM1_IRQ_TARGET
    fm1_wdt_feed();
#endif
}

static void boot_work_begin(const char *title, uint32_t total)
{
    boot_work.title = title;
    boot_work.done = 0;
    boot_work.total = (uint16_t)total;
    boot_work.state = 1;
    boot_work_draw();
}

static void boot_work_progress(uint32_t done)
{
    boot_work.done = (uint16_t)(done > boot_work.total ? boot_work.total : done);
    boot_work_draw();
}

static void boot_work_end(int failed)
{
    boot_work.state = failed ? 2u : 0u;
    boot_work_draw();
}

#define BOOT_WORK_BEGIN(title, total)  boot_work_begin(title, total)
#define BOOT_WORK_PROGRESS(done)       boot_work_progress(done)
#define BOOT_WORK_END(failed)          boot_work_end(failed)
