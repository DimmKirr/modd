// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
// Origin: forked from keremimo/melodee firmware/src/ui_boot.c @ v1.4.5 (398ada1e)
//         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/ui_boot.c
//         modd-owned: OTA progress screen with LED feedback
/* OTA maintenance progress screen.  Synchronous, no timer IRQ or audio needed.
 * Uses only boot-level primitives (boot_text_1x, lcd_fill, lcd_sync) which are
 * available before the main UI is loaded.  Included before the OTA module so the
 * OTA_WORK_* macros override the no-op defaults. */

#define OTA_WORK_BG    0x0000u  /* black */
#define OTA_WORK_FG    0xFFFFu  /* white */
#define OTA_WORK_ACC   0xFD20u  /* orange accent */
#define OTA_WORK_DIM   0x4208u  /* dark grey (progress track) */

static struct {
    const char *title;
    uint16_t done, total;
    uint8_t state;   /* 0 complete, 1 working, 2 failed */
} ota_work;

static void ota_work_leds(void)
{
    uint32_t lit = ota_work.total ? FM1_NOTE_KEY_COUNT * ota_work.done / ota_work.total : 0u;
    uint32_t i;
    for (i = 0; i < FM1_NOTE_KEY_COUNT; i++)
        fm1_led_key(FM1_NOTE_KEY_FIRST + i, i < lit);
}

static void ota_work_draw(void)
{
    lcd_fill(0, 0, 240, 240, OTA_WORK_BG);

    boot_text_1x(80, ota_work.title, OTA_WORK_FG, OTA_WORK_BG);

    if (ota_work.state == 2u) {
        boot_text_1x(110, "Could not finish", OTA_WORK_ACC, OTA_WORK_BG);
        boot_text_1x(130, "Restart to retry", OTA_WORK_DIM, OTA_WORK_BG);
    } else {
        char count[24];
        fmt_int(count, ota_work.done);
        uint32_t n = str_len(count);
        str_cpy(count + n, " / ", sizeof count - n);
        fmt_int(count + str_len(count), ota_work.total);
        boot_text_1x(110, count, OTA_WORK_ACC, OTA_WORK_BG);

        uint32_t bar_w = ota_work.total ? 200u * ota_work.done / ota_work.total : 0u;
        lcd_fill(20, 128, 200, 4, OTA_WORK_DIM);
        if (bar_w)
            lcd_fill(20, 128, bar_w, 4, OTA_WORK_ACC);

        const char *status = ota_work.state ? "Keep power on" : "Complete";
        boot_text_1x(145, status, OTA_WORK_DIM, OTA_WORK_BG);
    }

    ota_work_leds();
    lcd_sync();
#ifdef FM1_IRQ_TARGET
    fm1_wdt_feed();
#endif
}

static void ota_work_begin(const char *title, uint32_t total)
{
    ota_work.title = title;
    ota_work.done = 0;
    ota_work.total = (uint16_t)total;
    ota_work.state = 1;
    ota_work_draw();
}

static void ota_work_progress(uint32_t done)
{
    ota_work.done = (uint16_t)(done > ota_work.total ? ota_work.total : done);
    ota_work_draw();
}

static void ota_work_end(int failed)
{
    ota_work.state = failed ? 2u : 0u;
    if (!failed)
        ota_work.done = ota_work.total;
    ota_work_draw();
    if (!failed) {
        uint32_t i;
        for (i = 0; i < FM1_NOTE_KEY_COUNT; i++)
            fm1_led_key(FM1_NOTE_KEY_FIRST + i, 0);
    }
}

#define OTA_WORK_BEGIN(title, total)  ota_work_begin(title, total)
#define OTA_WORK_PROGRESS(done)       ota_work_progress(done)
#define OTA_WORK_END(failed)          ota_work_end(failed)
