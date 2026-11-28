/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* modd USB-disk OTA updater: reads a .fwsc firmware package from a USB
 * flash drive and writes it to SPI flash.  Runs from SRAM with interrupts
 * off; SPI flash is a plain block device.
 *
 * Boot modes (the boot init's word at 0x01C7C000):
 *   INIT_REQ_BROWSE (0x42514552): OCT+ held — file browser
 *   anything else: auto-flash "UPDATE.FWSC" from the drive root
 *
 * Single compilation unit: every dependency is #included.  Does not reuse
 * firmware/src/ — the updater must fit in 40 KB (FLASH_OTA_LEN) and runs
 * from SRAM with the main firmware erased; sharing code with a 600 KB
 * application would pull in far more than the slot allows. */
#include <stdint.h>

/* ---- mode word from the boot init ---- */
#define INIT_WORD (*(volatile uint32_t *)0x01C7C000u)
#define INIT_REQ_BROWSE 0x42514552u

/* ---- stubs for app-level dependencies the USB host stack calls ---- */
#define MODD_LOG_LINE_LEN 40
#define CONFIG_UDISK 1

/* Log ring: modd_log / modd_log_int store lines here; boot_log_flush draws them. */
#define BOOT_LOG_LINES 14
#define BOOT_LOG_COLS  40
static char  boot_log_buf[BOOT_LOG_LINES][BOOT_LOG_COLS + 1];
static uint8_t boot_log_wr;
static uint8_t boot_log_dirty;

static void modd_log(const char *msg)
{
    char *dst = boot_log_buf[boot_log_wr % BOOT_LOG_LINES];
    uint32_t i;
    for (i = 0; i < BOOT_LOG_COLS && msg[i]; i++) dst[i] = msg[i];
    for (; i < BOOT_LOG_COLS; i++) dst[i] = ' ';
    dst[BOOT_LOG_COLS] = 0;
    boot_log_wr++;
    boot_log_dirty = 1;
}

static void modd_log_int(const char *prefix, int32_t val)
{
    char *dst = boot_log_buf[boot_log_wr % BOOT_LOG_LINES];
    uint32_t i;
    for (i = 0; i < 30u && prefix[i]; i++) dst[i] = prefix[i];
    if (val < 0) { dst[i++] = '-'; val = -val; }
    char d[11]; int n = 0;
    do { d[n++] = (char)('0' + val % 10); val /= 10; } while (val && n < 10);
    while (n-- > 0 && i < BOOT_LOG_COLS) dst[i++] = d[n];
    for (; i < BOOT_LOG_COLS; i++) dst[i] = ' ';
    dst[BOOT_LOG_COLS] = 0;
    boot_log_wr++;
    boot_log_dirty = 1;
}

static void modd_log_hex(const char *prefix, const uint8_t *data, uint32_t len)
{
    (void)prefix; (void)data; (void)len;
}
#define modd_log_ip(prefix, ip) ((void)0)
#define toast_status(s) ((void)0)
#define usb_settle_ms(ms) fm1_delay_ms(ms)
#define usb_mdelay(ms) fm1_delay_ms(ms)

/* ---- HAL ---- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
#define FL_RANGE_OK(off, n) \
    (FL_IN(off, n, FLASH_APP_LO, FLASH_APP_END) || \
     FL_IN(off, n, FLASH_APP_END, FLASH_GLOB_LO))
#include "fm1_flash.h"
#include "../ports/melodee/libc/libc.c"

/* ---- LCD (SPI1 transport for the ST7789 panel) ---- */
#include "fm1_lcd_hw.h"

/* ==== screen: ST7789 240x240, 5x7 bitmap font, strip-buffer rendering ====
 * Adapted from the fm1-base boot_screen.c pattern (MPL-2.0, Charles Vestal).
 * Modd-native code: same factual colour scheme and glyph bitmaps. */

#define SCR_BG    0xFFBDu   /* #faf4e8 cream */
#define SCR_FG    0x49C5u   /* #48392f brown */
#define SCR_BAR   0xBA43u   /* #b84a1e coral */
#define SCR_GREEN 0x0620u   /* #007700 dark green (confirm) */
#define SCR_TRACK 0xEEF9u   /* #e8dcc8 empty bar */
#define SCR_SEL   0xD6BAu   /* highlight row */

#define SCR_BAR_X 30u
#define SCR_BAR_Y 150u
#define SCR_BAR_W 180u
#define SCR_BAR_H 12u
#define SCR_ROWS  21u       /* tallest line: 7 rows * 3x scale */

static uint16_t scr_strip[240u * SCR_ROWS];
static uint8_t scr_small[8];
static uint32_t scr_shown;
static uint32_t scr_pc;

static const struct { char c; uint8_t rows[7]; } SCR_FONT[] = {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 } },
    { '-', { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 } },
    { '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
};

static void scr_cmd(uint8_t c)
{
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_data(const void *p, uint32_t n)
{
    fm1_lcd_send_data(p, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    scr_small[0] = (uint8_t)(x0 >> 8); scr_small[1] = (uint8_t)x0;
    scr_small[2] = (uint8_t)(x1 >> 8); scr_small[3] = (uint8_t)x1;
    scr_cmd(0x2A);
    scr_data(scr_small, 4);
    scr_small[4] = (uint8_t)(y0 >> 8); scr_small[5] = (uint8_t)y0;
    scr_small[6] = (uint8_t)(y1 >> 8); scr_small[7] = (uint8_t)y1;
    scr_cmd(0x2B);
    scr_data(scr_small + 4, 4);
    scr_cmd(0x2C);
}

static uint16_t scr_swap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static void scr_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n = w * h, k = 240u * SCR_ROWS;
    for (i = 0; i < k && i < n; i++)
        scr_strip[i] = scr_swap(c);
    scr_window(x, y, x + w - 1u, y + h - 1u);
    for (; n; n -= k) {
        if (k > n)
            k = n;
        scr_data(scr_strip, k * 2u);
    }
}

static void scr_text(uint32_t y, const char *s, uint32_t scale, uint32_t cols, uint16_t fg, uint16_t bg)
{
    uint32_t len = 0, i, r, b, sx, sy, x0, w, pad;
    while (s[len])
        len++;
    if (cols < len)
        cols = len;
    w = cols * 6u * scale - scale;
    if (w > 240u)
        w = 240u;
    x0 = (240u - w) / 2u;
    pad = (cols - len) * 6u * scale / 2u;
    for (i = 0; i < w * 7u * scale; i++)
        scr_strip[i] = scr_swap(bg);
    for (i = 0; i < len; i++) {
        const uint8_t *g = 0;
        uint32_t f;
        for (f = 0; f < sizeof SCR_FONT / sizeof SCR_FONT[0]; f++)
            if (SCR_FONT[f].c == s[i])
                g = SCR_FONT[f].rows;
        if (!g)
            continue;
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if (g[r] & (0x10u >> b))
                    for (sy = 0; sy < scale; sy++)
                        for (sx = 0; sx < scale; sx++)
                            scr_strip[(r * scale + sy) * w + pad + (i * 6u + b) * scale + sx] = scr_swap(fg);
    }
    scr_window(x0, y, x0 + w - 1u, y + 7u * scale - 1u);
    scr_data(scr_strip, w * 7u * scale * 2u);
}

static void scr_text_at(uint32_t x0, uint32_t y, const char *s, uint32_t scale, uint32_t box_w,
                        uint16_t fg, uint16_t bg)
{
    uint32_t len = 0, i, r, b, sx, sy, tw, pad;
    while (s[len])
        len++;
    tw = len * 6u * scale;
    if (box_w < tw)
        box_w = tw;
    if (box_w > 240u - x0)
        box_w = 240u - x0;
    pad = (box_w - tw) / 2u;
    for (i = 0; i < box_w * 7u * scale; i++)
        scr_strip[i] = scr_swap(bg);
    for (i = 0; i < len; i++) {
        const uint8_t *g = 0;
        uint32_t f;
        for (f = 0; f < sizeof SCR_FONT / sizeof SCR_FONT[0]; f++)
            if (SCR_FONT[f].c == s[i])
                g = SCR_FONT[f].rows;
        if (!g)
            continue;
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if (g[r] & (0x10u >> b))
                    for (sy = 0; sy < scale; sy++)
                        for (sx = 0; sx < scale; sx++)
                            scr_strip[(r * scale + sy) * box_w + pad + (i * 6u + b) * scale + sx] = scr_swap(fg);
    }
    scr_window(x0, y, x0 + box_w - 1u, y + 7u * scale - 1u);
    scr_data(scr_strip, box_w * 7u * scale * 2u);
}

static void scr_percent(uint32_t pc)
{
    char t[5], *p = t;
    if (pc >= 100u)
        *p++ = '1';
    if (pc >= 10u)
        *p++ = (char)('0' + pc / 10u % 10u);
    *p++ = (char)('0' + pc % 10u);
    *p++ = '%';
    *p = 0;
    scr_text(172u, t, 2, 4, SCR_FG, SCR_BG);
}

static void boot_screen_init(void)
{
    fm1_lcd_hw_init();
    fm1_delay_ms(20);
    scr_cmd(0x01);          /* SWRESET */
    fm1_delay_ms(150);
    scr_cmd(0x11);          /* SLPOUT */
    fm1_delay_ms(120);
    scr_cmd(0x3A);          /* COLMOD: RGB565 */
    scr_small[0] = 0x55;
    scr_data(scr_small, 1);
    scr_cmd(0x36);          /* MADCTL: top-left origin, RGB order */
    scr_small[0] = 0x00;
    scr_data(scr_small, 1);
    scr_cmd(0x21);          /* INVON: IPS panel */
    scr_cmd(0x13);          /* NORON */
    scr_fill(0, 0, 240, 240, SCR_BG);
    scr_cmd(0x29);          /* DISPON */
}

/* ---- USB debug status screen (temporary) ---- */
#define DBG_ROW_H  18u
#define DBG_Y0     4u

static void dbg_hex4(char *p, uint16_t v)
{
    static const char h[] = "0123456789ABCDEF";
    p[0] = h[(v >> 12) & 0xF]; p[1] = h[(v >> 8) & 0xF];
    p[2] = h[(v >> 4) & 0xF];  p[3] = h[v & 0xF];
}

static void dbg_hex2(char *p, uint8_t v)
{
    static const char h[] = "0123456789ABCDEF";
    p[0] = h[(v >> 4) & 0xF]; p[1] = h[v & 0xF];
}

static void dbg_int(char *p, int32_t v, uint32_t *pos)
{
    if (v < 0) { p[(*pos)++] = '-'; v = -v; }
    char d[11]; int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
    while (n-- > 0) p[(*pos)++] = d[n];
}

static void dbg_row(uint32_t row, const char *text)
{
    scr_text(DBG_Y0 + row * DBG_ROW_H, text, 2, 20, SCR_FG, SCR_BG);
}

#define BOOT_LOG_Y0  (DBG_Y0 + 11u * DBG_ROW_H)
#define BOOT_LOG_RH  9u

static void boot_log_flush(void)
{
    if (!boot_log_dirty) return;
    boot_log_dirty = 0;
    uint32_t total = boot_log_wr;
    if (total > BOOT_LOG_LINES) total = BOOT_LOG_LINES;
    uint32_t start = boot_log_wr - total;
    uint32_t row;
    for (row = 0; row < total; row++) {
        uint32_t y = BOOT_LOG_Y0 + row * BOOT_LOG_RH;
        if (y + BOOT_LOG_RH > 240u) break;
        scr_text(y, boot_log_buf[(start + row) % BOOT_LOG_LINES], 1, BOOT_LOG_COLS, SCR_FG, SCR_BG);
    }
}

static void boot_screen_status(const char *big, const char *small)
{
    scr_text(60u, big, 3, 8, SCR_FG, SCR_BG);
    scr_text(94u, small, 2, 19, SCR_FG, SCR_BG);
}

static void boot_led_progress(uint32_t done, uint32_t total);

static void boot_screen_progress(uint32_t done, uint32_t total)
{
    uint32_t px, pc;
    if (!total)
        return;
    px = done >= total ? SCR_BAR_W : done * SCR_BAR_W / total;
    pc = done >= total ? 100u : done * 100u / total;
    if (px > scr_shown) {
        scr_fill(SCR_BAR_X + scr_shown, SCR_BAR_Y, px - scr_shown, SCR_BAR_H, SCR_BAR);
        scr_shown = px;
    }
    if (pc > scr_pc) {
        scr_percent(pc);
        scr_pc = pc;
    }
    boot_led_progress(done, total);
}

static void boot_screen_flash_begin(void)
{
    scr_fill(0, 0, 240, 240, SCR_BG);
    boot_screen_status("UPDATING", "DO NOT UNPLUG");
    scr_fill(SCR_BAR_X, SCR_BAR_Y, SCR_BAR_W, SCR_BAR_H, SCR_TRACK);
    scr_percent(0);
    scr_shown = 0;
    scr_pc = 0;
}

static void boot_screen_done(void)
{
    boot_screen_progress(1, 1);
    boot_screen_status("DONE", "RESTARTING");
}

static void boot_screen_error(const char *msg)
{
    boot_screen_status("ERROR", msg);
}

/* ---- input HAL (polled: no ISR, no PWM dimming needed) ---- */
#include "fm1_input.h"
#include "fm1_led_map.h"

static uint8_t boot_led_rows[FM1_NCOL];      /* solid: the white keys already done */
static uint8_t boot_led_head = 0xFFu;        /* white-key index of the pulsing key, 0xFF none */

static void boot_led_refresh(void)
{
    uint32_t col, rows, hcol = FM1_NCOL, hrow = 0;
    uint32_t last_col = FM1_NCOL;
    fm1__led_lines(0);
    if (boot_led_head < FM1_WHITE_KEY_COUNT) {
        hcol = FM1_WHITE_KEY_HW[boot_led_head][0];
        hrow = FM1_WHITE_KEY_HW[boot_led_head][1];
    }
    for (col = 0; col < FM1_NCOL; col++) {
        rows = boot_led_rows[col] | (col == hcol ? hrow : 0u);
        if (rows) {
            fm1__sr_word(0xFFFFu ^ (1u << col) ^ (col < 2u ? 1u << (11u + col) : 0u));
            fm1__led_lines(rows);
            fm1_delay_us(FM1_LED_US);
            fm1__led_lines(0);
            if (boot_led_rows[col])
                last_col = col;
        }
    }
    if (last_col < FM1_NCOL) {
        fm1__sr_word(0xFFFFu ^ (1u << last_col) ^ (last_col < 2u ? 1u << (11u + last_col) : 0u));
        fm1__led_lines(boot_led_rows[last_col]);
    }
}

/* White keys as a progress bar: the keys done are solid, the next one (the
 * head of the bar) pulses, the rest are dark.  At 100% all are solid. */
static void boot_led_progress(uint32_t done, uint32_t total)
{
    uint32_t lit = total ? FM1_WHITE_KEY_COUNT * done / total : 0u;
    uint32_t i;
    if (lit > FM1_WHITE_KEY_COUNT)
        lit = FM1_WHITE_KEY_COUNT;
    for (i = 0; i < lit; i++)
        boot_led_rows[FM1_WHITE_KEY_HW[i][0]] |= FM1_WHITE_KEY_HW[i][1];
    boot_led_head = lit < FM1_WHITE_KEY_COUNT ? (uint8_t)lit : 0xFFu;
    boot_led_refresh();
}

/* ---- USB HAL ---- */
#include "fm1_usb.h"
#include "fm1_usb_musb.h"

/* ---- USB shared buffer (precedes both device and host stacks) ---- */
static uint8_t ep0buf[64 + 4] __attribute__((aligned(4)));
#define EP0BUF_DEFINED

/* ---- mode flag: 0 = USB-disk, 1 = SysEx ---- */
static int boot_mode;

/* ==== USB-MIDI device stack (SysEx recovery path, OCT-) ====
 * The FM-1 appears as USB-MIDI device "modd Update" (1209:0002) to
 * the host PC.  Included only for the SysEx path; the disk path
 * uses the host stack below instead. */
#define MELODEE_OTA 1
#define MELODEE_CDC 0
#define MELODEE_OTA 1
#define MELODEE_OTA_DRYRUN 0
#define MELODEE_USB_PID 0x0002
#define MODD_OTA_ID "ota-FM-1_900"
#include "../ports/melodee/usb/usb.c"

/* ---- monotonic ms (shared by SysEx and disk paths) ---- */
static uint32_t boot_ms(void)
{
    static uint32_t last, acc, ms;
    uint32_t now = fm1_ticks();
    acc += now - last;
    last = now;
    ms += acc / (1000u * FM1_TICKS_PER_US);
    acc %= 1000u * FM1_TICKS_PER_US;
    return ms;
}

static void boot_poll(void)
{
    fm1_wdt_feed();
    boot_led_refresh();
    if (boot_mode) {
        static uint32_t last;
        uint32_t now = fm1_ticks();
        if (now - last >= 500u * FM1_TICKS_PER_US) {
            last = now;
            usb_poll();
        }
    }
}

/* ==== SysEx OTA handler (Melodee upstream, collision-renamed) ====
 * The 8 utility symbols that collide with the disk path's own copies are
 * #define-renamed to sx_ota_* for the duration of this include.  Hooks
 * (ota_now_ms … ota_commit) keep their original names — Melodee ota.c
 * calls them, and they are NOT defined in the disk path. */

/* hooks called by Melodee ota.c (defined here, before the include) */
static uint32_t ota_now_ms(void) { return boot_ms(); }
static void     ota_idle(void)   { boot_poll(); }
static int      ota_erase(uint32_t off)                      { (void)off; return -1; }
static int      ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int      ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static void     ota_show(uint32_t step, int32_t code)        { (void)step; (void)code; }
static void     ota_commit(const uint8_t *parm)              { (void)parm; }

/* collision renames (8 symbols defined in both boot_disk.c and Melodee ota.c) */
#define ota_rd16   sx_ota_rd16
#define ota_rd24   sx_ota_rd24
#define ota_rd32   sx_ota_rd32
#define ota_crc16  sx_ota_crc16
#define ota_jl_enc sx_ota_jl_enc
#define ota_memeq  sx_ota_memeq
#define ota_read   sx_ota_read
#define ota_ufw    sx_ota_ufw
#include "../ports/melodee/ota/ota.c"
#undef ota_rd16
#undef ota_rd24
#undef ota_rd32
#undef ota_crc16
#undef ota_jl_enc
#undef ota_memeq
#undef ota_read
#undef ota_ufw

/* ==== USB host stack (disk path, OCT+) ==== */
static uint8_t *usb_host_rxbuf;

/* ---- USB host state machine (defines uhost, SIE helpers, SIE_* constants) ---- */
#include "fm1_usb_host.h"

/* ---- USB host transfer engine (needs SIE helpers, CSR0H_*, uhost, ep0buf) ---- */
#include "../src/usb/host/usb_host_xfer.c"

/* ---- USB enumeration ---- */
static uint8_t udisk_is_active(void) { return 1; }

#define CONFIG_USB_HUB_MAX_DEVICES 10
#define USB_HUB_MSC_ONLY 1
#include "../src/usb/host/usb_host_enum.c"
#include "../src/usb/host/usb_hub.c"

/* ---- USB mass storage class ---- */
#include "../src/usb/msc/usb_msc.c"

/* ---- FatFs (read-only config in boot/ffconf.h, found via -Ifirmware/boot) ---- */
#ifdef CONFIG_BOOT_LFN
/* ASCII-only LFN stubs: firmware filenames are 7-bit ASCII, so full Unicode
 * tables (ffunicode.c, ~180 KB) are not needed. */
WCHAR ff_oem2uni(WCHAR oem, WORD cp) { (void)cp; return oem; }
WCHAR ff_uni2oem(DWORD uni, WORD cp) { (void)cp; return (WCHAR)uni; }
DWORD ff_wtoupper(DWORD chr) { return (chr >= 'a' && chr <= 'z') ? chr - 32 : chr; }
#endif
#include "../vendor/fatfs/source/ff.c"

/* ---- FatFs disk I/O glue (reads from USB MSC) ---- */
#include "../src/udisk/diskio.c"

/* ---- UFW package utilities (CRC16-CCITT, byte-order reads, JLFS cipher) ----
 * Re-implemented here; the originals live in the Melodee ota.c SysEx handler
 * which the disk updater does not include. */
static uint32_t ota_rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t ota_rd24(const uint8_t *p)
{
    return ota_rd16(p) | (uint32_t)p[2] << 16;
}

static uint32_t ota_rd32(const uint8_t *p)
{
    return ota_rd24(p) | (uint32_t)p[3] << 24;
}

static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t c)
{
    while (n--) {
        c ^= (uint32_t)*p++ << 8;
        uint32_t i;
        for (i = 0; i < 8u; i++)
            c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1;
        c &= 0xFFFFu;
    }
    return c;
}

static void ota_jl_enc(uint8_t *p, uint32_t n)
{
    uint32_t k = 0xFFFFu, i;
    for (i = 0; i < n; i++) {
        p[i] ^= (uint8_t)k;
        k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
    }
}

static int ota_memeq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

/* ---- mode-aware ota_read: file (disk) or SysEx (USB stream) ---- */
static FIL boot_file;

static int ota_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
    if (boot_mode)
        return sx_ota_read(addr, dst, len);
    if (addr >= 0xF0000000u)
        return 0;
    UINT br;
    if (f_lseek(&boot_file, addr) != FR_OK)
        return -1;
    if (f_read(&boot_file, dst, len, &br) != FR_OK || br != len)
        return -1;
    return 0;
}

#define FWSC_MARKER_BYTES 20u
#define FW_META_MAGIC  (('M') | ('O' << 8) | ('D' << 16) | ('D' << 24))
#define FW_META_OFFSET 0x80u

/* ---- UFW header parser ---- */
/* .fwsc files have a marker byte after every 0x2F (47) data bytes for the
 * first 20 blocks (product identity).  Strip them in place so the logical
 * UFW layout starts at hdr[0]. */
static void ota_fwsc_strip(uint8_t *buf, uint32_t raw_len)
{
    uint32_t src = 0, dst = 0, blk;
    for (blk = 0; blk < 20u && src + 0x2Fu < raw_len; blk++) {
        if (dst != src) {
            uint32_t j;
            for (j = 0; j < 0x2Fu; j++)
                buf[dst + j] = buf[src + j];
        }
        dst += 0x2Fu;
        src += 0x2Fu + 1u;   /* skip marker */
    }
    while (src < raw_len)
        buf[dst++] = buf[src++];
}

static int ota_ufw(uint8_t *hdr, uint32_t *fl_off, uint32_t *fl_len,
                   uint32_t *ota_off, uint32_t *ota_len)
{
    uint32_t nent, i;
    *fl_off = 0; *fl_len = 0; *ota_off = 0; *ota_len = 0;
    int r0 = ota_read(0, hdr, 512);
    int r1 = ota_read(512, hdr + 512, 512);
    if (r0 || r1)
        return -1;
    if (!boot_mode)
        ota_fwsc_strip(hdr, 1024);
    ota_jl_enc(hdr, 0x40);
    nent = ota_rd16(hdr + 8);
    uint32_t hdr_crc = ota_rd16(hdr);
    uint32_t calc_crc = ota_crc16(hdr + 2, 0x3E, 0);
    uint32_t ent_crc_stored = ota_rd16(hdr + 2);
    uint32_t ent_crc_calc = nent <= 11u ? ota_crc16(hdr + 0x40, nent * 0x50u, 0) : 0;
    modd_log_int("NENT ", (int32_t)nent);
    modd_log_int("HCRC ", (int32_t)hdr_crc);
    modd_log_int("CCRC ", (int32_t)calc_crc);
    if (calc_crc != hdr_crc ||
        nent == 0 || nent > 11u ||
        ent_crc_calc != ent_crc_stored)
        return -2;
    for (i = 0; i < nent; i++) {
        uint8_t *e = hdr + 0x40 + i * 0x50;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) {
            *fl_off = ota_rd32(e + 8);
            *fl_len = ota_rd32(e + 12);
        }
        if (ota_rd16(e) == 100) {
            *ota_off = ota_rd32(e + 8);
            *ota_len = ota_rd32(e + 12);
        }
    }
    if (!boot_mode && *fl_off)
        *fl_off += FWSC_MARKER_BYTES;
    if (!boot_mode && *ota_off)
        *ota_off += FWSC_MARKER_BYTES;
    return (*fl_off && *fl_len) ? 0 : -3;
}

/* ---- flash write hooks for boot_core.c ---- */
static int boot_fread(uint32_t off, void *p, uint32_t n)
{
    boot_led_refresh();
    return fl_read_ram(off, p, n);
}

static int boot_erase(uint32_t off)
{
    uint32_t took;
    boot_led_refresh();
    int rc = fl_erase4k_ram(off, &took);
    boot_led_refresh();
    return rc;
}

static int boot_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t took;
    if (!FL_IN(off, n, FLASH_APP_LO, FLASH_APP_END))
        return -1;
    int rc = fl_prog_ram(off, p, n, &took);
    boot_led_refresh();
    return rc;
}

static void boot_record_clear(void)
{
    fm1_updata_parm_clear();
}

/* The hardware app region is 0x8F000 bytes (572 KB); the emulator flash map
 * stretches it to 6 MB.  The actual firmware body never exceeds the hardware
 * limit, so cap the progress bar there for a meaningful reading on both. */
#define BOOT_PROGRESS_CAP 0x8F000u

static void boot_progress(uint32_t done, uint32_t total)
{
    if (total > BOOT_PROGRESS_CAP)
        total = BOOT_PROGRESS_CAP;
    if (done > total)
        done = total;
    boot_screen_progress(done, total);
    fm1_wdt_feed();
}

#include "boot_core.c"

/* ---- USB host init and drive mount ---- */
static FATFS boot_fatfs;

static void boot_usb_show_devices(uint32_t row)
{
    uint32_t i;
    for (i = 0; i < USB_MAX_DEVICES && row < 12u; i++) {
        if (usb_devtab[i].addr == 0) continue;
        usb_dev_entry_t *e = &usb_devtab[i];
        char line[20] = "  DEV ? CLS ?? H?:?";
        line[6] = (char)('0' + e->addr);
        dbg_hex2(line + 12, e->dev_class);
        line[16] = (char)('0' + e->hub_addr);
        line[18] = (char)('0' + e->hub_port);
        dbg_row(row++, line);
    }
    if (usb_hub_msc_count) {
        for (i = 0; i < usb_hub_msc_count && row < 12u; i++) {
            usb_hub_msc_t *m = &usb_hub_msc_list[i];
            char line[20] = "  MSC ????:????    ";
            dbg_hex4(line + 6, m->vid);
            dbg_hex4(line + 11, m->pid);
            dbg_row(row++, line);
        }
    } else {
        dbg_row(row++, "  (no MSC found)");
    }
}

static void boot_dots(uint32_t phase)
{
    char buf[20] = "                   ";
    uint32_t n = (phase % 4u);
    uint32_t i;
    for (i = 0; i < n; i++)
        buf[i] = '.';
    scr_text(94u, buf, 2, 19, SCR_FG, SCR_BG);
}

static int boot_usb_init(void)
{
    boot_screen_status("USB DISK", "CONNECTING");
    fm1_usb_off();
    fm1_delay_ms(50);
    usb_host_init(ep0buf);

    uint32_t t0 = boot_ms(), dots = 0;
    uint8_t saw_connect = 0;
    while (boot_ms() - t0 < 15000u) {
        fm1_wdt_feed();
        if ((boot_ms() - t0) / 500u > dots) {
            dots = (boot_ms() - t0) / 500u;
            boot_dots(dots);
        }
        if (!usb_host_connected()) {
            fm1_delay_ms(50);
            continue;
        }
        if (!saw_connect) {
            saw_connect = 1;
            boot_screen_status("USB DISK", "READING");
        }
        usb_host_poll(boot_ms());
        if (uhost.state == UHOST_MSC_READY)
            break;
        if (uhost.state == UHOST_ERROR)
            return -1;
        fm1_delay_ms(10);
    }
    if (!saw_connect)
        return -2;

    if (uhost.state != UHOST_MSC_READY)
        return -3;

    if (f_mount(&boot_fatfs, "", 1) != FR_OK)
        return -4;
    return 0;
}

/* ==== file browser (OCT+ boot mode) ====
 * Lists .FWS / .FWSC files from the USB drive root.
 * SELECT encoder scrolls, OCT+ confirms, OCT- cancels. */

#define BOOT_DIR_MAX   16u
#define BOOT_DIR_VIS   8u    /* visible rows on screen */
#define BOOT_LIST_Y    50u
#define BOOT_ROW_H     20u   /* 7*2 = 14px text + 6px gap */

static char boot_dir_names[BOOT_DIR_MAX][13];
static uint32_t boot_dir_sizes[BOOT_DIR_MAX];
static uint32_t boot_dir_count;

static int boot_str_ends(const char *s, const char *sfx)
{
    uint32_t sl = 0, xl = 0;
    while (s[sl]) sl++;
    while (sfx[xl]) xl++;
    if (sl < xl) return 0;
    uint32_t i;
    for (i = 0; i < xl; i++) {
        char a = s[sl - xl + i], b = sfx[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return 0;
    }
    return 1;
}

static uint32_t boot_dir_scan(void)
{
    DIR dir;
    FILINFO fno;
    boot_dir_count = 0;
    if (f_opendir(&dir, "/") != FR_OK)
        return 0;
    while (boot_dir_count < BOOT_DIR_MAX) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
            break;
        if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS))
            continue;
        if (!fno.fsize)
            continue;
        if (!boot_str_ends(fno.fname, ".FWS") && !boot_str_ends(fno.fname, ".FWSC"))
            continue;
        uint32_t j;
        for (j = 0; j < 12u && fno.fname[j]; j++)
            boot_dir_names[boot_dir_count][j] = fno.fname[j];
        boot_dir_names[boot_dir_count][j] = 0;
        boot_dir_sizes[boot_dir_count] = (uint32_t)fno.fsize;
        boot_dir_count++;
    }
    f_closedir(&dir);
    return boot_dir_count;
}

static void boot_dir_draw(uint32_t sel)
{
    uint32_t top = 0, i;
    if (sel >= BOOT_DIR_VIS)
        top = sel - BOOT_DIR_VIS + 1u;
    scr_fill(0, BOOT_LIST_Y, 240, BOOT_DIR_VIS * BOOT_ROW_H, SCR_BG);
    for (i = 0; i < BOOT_DIR_VIS && top + i < boot_dir_count; i++) {
        uint32_t idx = top + i;
        uint32_t y = BOOT_LIST_Y + i * BOOT_ROW_H + 3u;
        uint16_t fg = (idx == sel) ? SCR_BG : SCR_FG;
        uint16_t bg = (idx == sel) ? SCR_BAR : SCR_BG;
        if (idx == sel)
            scr_fill(10, BOOT_LIST_Y + i * BOOT_ROW_H, 220, BOOT_ROW_H, SCR_BAR);
        scr_text(y, boot_dir_names[idx], 2, 19, fg, bg);
    }
}

static void boot_fwsc_product(const uint8_t *raw960, char *out, uint32_t out_len)
{
    uint32_t i, p = 0;
    for (i = 0; i < 20u && p < out_len - 1u; i++) {
        uint8_t m = raw960[i * 0x30u + 0x2Fu];
        if (m == 0x7Du)
            break;
        char ch = (char)((m - i - 1u) & 0xFFu);
        if (ch >= 0x20 && ch < 0x7F)
            out[p++] = ch;
    }
    out[p] = 0;
}

struct boot_file_meta {
    char name[16];
    char version[20];
    char product[22];
    uint32_t size_kb;
    uint8_t has_meta;
};

static void boot_read_file_meta(const char *fname, uint32_t fsize, struct boot_file_meta *m)
{
    FIL f;
    uint32_t i;
    UINT br;
    m->name[0] = 0;
    m->version[0] = 0;
    m->product[0] = 0;
    m->size_kb = (fsize + 512u) / 1024u;
    m->has_meta = 0;

    if (f_open(&f, fname, FA_READ) != FR_OK)
        return;

    uint8_t buf[960];
    if (f_read(&f, buf, 960, &br) != FR_OK || br < 960u) {
        f_close(&f);
        return;
    }

    boot_fwsc_product(buf, m->product, sizeof m->product);

    ota_fwsc_strip(buf, 960);
    /* 940 logical bytes: header (0x40) + up to 10 entries (10*0x50=800) */
    ota_jl_enc(buf, 0x40);
    if (ota_crc16(buf + 2, 0x3E, 0) != ota_rd16(buf)) {
        f_close(&f);
        return;
    }
    uint32_t nent = ota_rd16(buf + 8);
    if (nent == 0 || nent > 10u) {
        f_close(&f);
        return;
    }

    uint32_t fl_off = 0;
    for (i = 0; i < nent; i++) {
        uint8_t *e = buf + 0x40 + i * 0x50;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) {
            fl_off = ota_rd32(e + 8) + FWSC_MARKER_BYTES;
            break;
        }
    }
    if (!fl_off) {
        f_close(&f);
        return;
    }

    /* fw_meta_t at flash body base (0x5000) + FW_META_OFFSET (0x80) */
    uint32_t meta_file_off = fl_off + 0x5000u + FW_META_OFFSET;
    uint8_t mb[96];
    if (f_lseek(&f, meta_file_off) != FR_OK ||
        f_read(&f, mb, 96, &br) != FR_OK || br < 96u) {
        f_close(&f);
        return;
    }
    f_close(&f);

    uint32_t key;
    if (boot_chip_key(&key) != 0)
        return;
    /* SFC base_off = flash_addr - FLASH_APP_LO = 0x5080 - 0x4000 = 0x1080 */
    boot_sfc(mb, 96, 0x1000u + FW_META_OFFSET, key);

    if (ota_rd32(mb) != FW_META_MAGIC)
        return;

    m->has_meta = 1;
    for (i = 0; i < 15u && mb[8 + i]; i++)
        m->name[i] = (char)mb[8 + i];
    m->name[i] = 0;
    for (i = 0; i < 19u && mb[24 + i]; i++)
        m->version[i] = (char)mb[24 + i];
    m->version[i] = 0;
}

static void boot_show_file_info(uint32_t sel)
{
    char line1[20] = "                   ";
    char line2[20] = "                   ";
    struct boot_file_meta m;

    boot_read_file_meta(boot_dir_names[sel], boot_dir_sizes[sel], &m);

    if (m.has_meta) {
        uint32_t i;
        for (i = 0; m.name[i] && i < 19u; i++) {
            char c = m.name[i];
            line1[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        for (i = 0; m.version[i] && i < 19u; i++) {
            char c = m.version[i];
            line2[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
    } else {
        uint32_t i;
        if (m.product[0]) {
            for (i = 0; m.product[i] && i < 19u; i++)
                line1[i] = m.product[i];
        }
        char d[12]; int n = 0;
        uint32_t v = m.size_kb;
        do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
        uint32_t p = 0;
        while (n-- > 0) line2[p++] = d[n];
        line2[p++] = ' '; line2[p++] = 'K'; line2[p++] = 'B';
    }

    scr_fill(0, 210u, 240, 16u, SCR_BG);
    scr_text(211u, line1, 1, 19, SCR_FG, SCR_BG);
    scr_text(219u, line2, 1, 19, SCR_FG, SCR_BG);
}

static int boot_browse(void)
{
    uint32_t sel = 0, redraw = 1, info_sel = 0xFFFFFFFFu;

    scr_text(10u, "SELECT FILE", 3, 11, SCR_FG, SCR_BG);
    scr_text_at(0, 226u, "CANCEL", 2, 120, SCR_FG, SCR_BG);
    scr_text_at(120, 226u, "FLASH", 2, 120, SCR_BG, SCR_GREEN);

    if (boot_dir_scan() == 0) {
        boot_screen_status("NO FILES", "INSERT USB DRIVE");
        fm1_delay_ms(3000);
        return -1;
    }

    for (;;) {
        fm1_wdt_feed();
        fm1_input_scan();

        int32_t enc = fm1_enc_take(0);
        if (enc > 0 && sel < boot_dir_count - 1u) {
            sel++;
            redraw = 1;
        } else if (enc < 0 && sel > 0) {
            sel--;
            redraw = 1;
        }

        uint32_t pressed = fm1_input_edges(0);
        if (pressed & (1u << FM1_BTN_OCT_UP)) {
            if (f_open(&boot_file, boot_dir_names[sel], FA_READ) != FR_OK) {
                boot_screen_error("OPEN FAILED");
                fm1_delay_ms(2000);
                return -1;
            }
            return 0;
        }
        if (pressed & (1u << FM1_BTN_OCT_DOWN))
            return -1;

        if (redraw) {
            boot_dir_draw(sel);
            redraw = 0;
        }
        if (sel != info_sel) {
            boot_show_file_info(sel);
            info_sel = sel;
        }
        fm1_delay_ms(1);
    }
}

/* ==== main: combined disk (OCT+) and SysEx (OCT-) paths ==== */
#define BOOT_FWSC_NAME "UPDATE.FWSC"

/* ---- SysEx recovery loop (OCT- / rescue) ---- */
static void boot_sysex_main(void)
{
    boot_mode = 1;
    fm1_input_init();
    boot_screen_status("SYSEX", "CONNECT USB");
    usb_start();
    for (;;) {
        boot_poll();
        usb_retry(boot_ms());
        if (usb.uboot_req) {
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            boot_screen_flash_begin();
            if (boot_session() == 0) {
                boot_screen_done();
                INIT_WORD = 0;
                fm1_delay_ms(200);
                usb_detach();
                fm1_delay_ms(30);
                fm1_core_reset();
            }
            boot_screen_error("FLASH FAILED");
        }
    }
}

/* ---- USB disk update (OCT+ / browse or auto-flash) ---- */
static void boot_disk_main(void)
{
    boot_mode = 0;
    fm1_input_init();

    uint32_t browse = (INIT_WORD == INIT_REQ_BROWSE);

    scr_fill(0, 0, 240, 240, SCR_BG);

    int usb_rc = boot_usb_init();
    if (usb_rc != 0) {
        if (usb_rc == -2)
            boot_screen_error("NO USB DISK");
        else
            boot_screen_error("USB ERROR");
        fm1_delay_ms(3000);
        INIT_WORD = 0;
        fm1_core_reset();
    }

    if (browse) {
        if (boot_browse() != 0) {
            f_mount(0, "", 0);
            INIT_WORD = 0;
            fm1_delay_ms(500);
            fm1_core_reset();
        }
    } else {
        if (f_open(&boot_file, BOOT_FWSC_NAME, FA_READ) != FR_OK) {
            boot_screen_error("NO UPDATE.FWSC");
            f_mount(0, "", 0);
            INIT_WORD = 0;
            fm1_delay_ms(3000);
            fm1_core_reset();
        }
    }

    boot_screen_flash_begin();
    int rc = boot_session();
    f_close(&boot_file);
    f_mount(0, "", 0);

    if (rc == 0) {
        boot_screen_done();
        INIT_WORD = 0;
        fm1_delay_ms(1000);
        fm1_core_reset();
    }

    {
        char msg[20] = "FLASH FAILED RC=   ";
        uint32_t p = 16;
        int v = rc;
        if (v < 0) { msg[p++] = '-'; v = -v; }
        char d[6]; int n = 0;
        do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 5);
        while (n-- > 0) msg[p++] = d[n];
        msg[p] = 0;
        boot_screen_error(msg);
    }
    fm1_delay_ms(5000);
    INIT_WORD = 0;
    fm1_core_reset();
}

extern uint32_t _bss_start[], _bss_end[];

#ifndef BOOT_BUILD_VERSION
#define BOOT_BUILD_VERSION "unknown"
#endif

void boot_main(void)
{
    fm1_wdt_arm(0x0D);
    fm1_time_init();
    boot_screen_init();
    scr_text(224u, BOOT_BUILD_VERSION, 1, 2, SCR_FG, SCR_BG);

    if (INIT_WORD == INIT_REQ_BROWSE)
        boot_disk_main();
    else
        boot_sysex_main();
}

void boot_cstart(void)
{
    uint32_t *p;
    boot_record_clear();
    for (p = _bss_start; p < _bss_end; p++)
        *p = 0;
    boot_main();
}
