// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* LVGL on the FM-1: the display, the tick and the one input device.
 *
 * Display: Melodee's ST7789 driver (ui/themes/melodee/lcd.c). lcd_blit() starts a DMA
 * transfer and returns; lcd_sync() waits for it. LVGL draws in strips ("partial" render
 * mode) into two buffers, so it draws the next strip while the last one is still on the
 * bus; when it needs a buffer back it calls the flush-wait callback, which is lcd_sync().
 * That is the hook LVGL's manual names for a driver without a transfer-done interrupt
 * (docs/lvgl/docs/src/main-modules/display/setup.mdx, "Flush-Wait Callback").
 *
 * Input: none of LVGL's. The panel's knobs and buttons go to the pages through
 * shell.c: a page changes what its widgets show, LVGL draws it. */

#define LVT_W 240
#define LVT_H 240
#define LVT_BUF_ROWS 24u                              /* a tenth of the screen: LVGL's recommended minimum */
#define LVT_BUF_BYTES (LVT_W * LVT_BUF_ROWS * 2u)     /* RGB565: 11,520 bytes each */

/* The density LVGL's default theme sizes its paddings, borders and corners by
 * (pixels = dp * dpi / 160). LVGL's own default, 130, makes finger-sized targets: a
 * drop-down 37 pixels high on this 240-pixel screen. The FM-1 has no touch, only the
 * encoder, so the theme is told a lower density and everything packs closer. */
#define LVT_DPI 80

/* In .pool like every big buffer (the .bss region is nearly full); RAM, as the DMA needs. */
static uint8_t lvt_buf[2][LVT_BUF_BYTES] __attribute__((section(".pool"), aligned(4)));

static lv_display_t *lvt_disp;

static uint32_t lvt_tick(void) { return fm1_ms; }

static void lvt_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)d;
    lcd_blit((uint32_t)a->x1, (uint32_t)a->y1,
             (uint32_t)lv_area_get_width(a), (uint32_t)lv_area_get_height(a),
             (const uint16_t *)(const void *)px);
}

static void lvt_flush_wait(lv_display_t *d)
{
    (void)d;
    lcd_sync();
}

static void lvt_port_init(void)
{
    lv_init();
    lv_tick_set_cb(lvt_tick);

    lvt_disp = lv_display_create(LVT_W, LVT_H);       /* LV_COLOR_FORMAT_DEFAULT: the panel's byte order */
    lv_display_set_dpi(lvt_disp, LVT_DPI);            /* before the theme is initialised (shell.c) */
    lv_display_set_buffers(lvt_disp, lvt_buf[0], lvt_buf[1], LVT_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(lvt_disp, lvt_flush);
    lv_display_set_flush_wait_cb(lvt_disp, lvt_flush_wait);

}
