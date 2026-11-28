/* Host-side test harness for the OTA updater's screen rendering.
 * Compiles with the host cc, not the JieLi cross-compiler.
 * Provides a mock LCD that captures pixels to a 240x240 framebuffer,
 * exercises every screen state, and writes PPM screenshots. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==== mock LCD: captures SPI commands and pixel data to a framebuffer ==== */

static uint16_t mock_fb[240][240];

static uint32_t mock_col0, mock_col1, mock_row0, mock_row1;
static uint32_t mock_cx, mock_cy;
static uint8_t mock_last_cmd;
static int mock_collecting_param;  /* 1 = CASET param, 2 = RASET param */

static void fm1_lcd_hw_init(void) {}
static void fm1_lcd_wait(void) {}
static void fm1_lcd_deselect(void) {}
static void fm1_delay_ms(uint32_t ms) { (void)ms; }

static void fm1_lcd_send_cmd(uint8_t c)
{
    mock_last_cmd = c;
    if (c == 0x2A)
        mock_collecting_param = 1;
    else if (c == 0x2B)
        mock_collecting_param = 2;
    else if (c == 0x2C) {
        mock_collecting_param = 0;
        mock_cx = mock_col0;
        mock_cy = mock_row0;
    } else {
        mock_collecting_param = 0;
    }
}

static void fm1_lcd_send_data(const void *p, uint32_t n)
{
    const uint8_t *d = (const uint8_t *)p;

    if (mock_collecting_param == 1 && n == 4) {
        mock_col0 = ((uint32_t)d[0] << 8) | d[1];
        mock_col1 = ((uint32_t)d[2] << 8) | d[3];
        return;
    }
    if (mock_collecting_param == 2 && n == 4) {
        mock_row0 = ((uint32_t)d[0] << 8) | d[1];
        mock_row1 = ((uint32_t)d[2] << 8) | d[3];
        return;
    }
    if (mock_collecting_param) {
        /* Single-byte config data (COLMOD, MADCTL) — ignore */
        return;
    }

    /* Pixel data: RGB565 big-endian pairs */
    uint32_t i;
    for (i = 0; i + 1 < n; i += 2) {
        uint16_t rgb565 = ((uint16_t)d[i] << 8) | d[i + 1];
        if (mock_cy < 240 && mock_cx < 240)
            mock_fb[mock_cy][mock_cx] = rgb565;
        mock_cx++;
        if (mock_cx > mock_col1) {
            mock_cx = mock_col0;
            mock_cy++;
        }
    }
}

/* ==== PPM output ==== */

static void save_ppm(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n240 240\n255\n");
    uint32_t y, x;
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 240; x++) {
            uint16_t c = mock_fb[y][x];
            uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
            uint8_t g = (uint8_t)(((c >>  5) & 0x3F) << 2);
            uint8_t b = (uint8_t)(( c        & 0x1F) << 3);
            uint8_t rgb[3] = { r, g, b };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* ==== screen code copied from firmware/boot/boot.c ==== */

#define SCR_BG    0xFFBDu
#define SCR_FG    0x49C5u
#define SCR_BAR   0xBA43u
#define SCR_TRACK 0xEEF9u
#define SCR_SEL   0xD6BAu

#define SCR_BAR_X 30u
#define SCR_BAR_Y 150u
#define SCR_BAR_W 180u
#define SCR_BAR_H 12u
#define SCR_ROWS  21u

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
    scr_cmd(0x01);
    fm1_delay_ms(150);
    scr_cmd(0x11);
    fm1_delay_ms(120);
    scr_cmd(0x3A);
    scr_small[0] = 0x55;
    scr_data(scr_small, 1);
    scr_cmd(0x36);
    scr_small[0] = 0x00;
    scr_data(scr_small, 1);
    scr_cmd(0x21);
    scr_cmd(0x13);
    scr_fill(0, 0, 240, 240, SCR_BG);
    scr_cmd(0x29);
}

static void boot_screen_status(const char *big, const char *small)
{
    scr_text(60u, big, 3, 8, SCR_FG, SCR_BG);
    scr_text(94u, small, 2, 19, SCR_FG, SCR_BG);
}

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
}

static void boot_screen_flash_begin(void)
{
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

/* ==== file browser data (test only) ==== */

#define BOOT_DIR_MAX   16u
#define BOOT_DIR_VIS   8u
#define BOOT_LIST_Y    50u
#define BOOT_ROW_H     20u

static char boot_dir_names[BOOT_DIR_MAX][13];
static uint32_t boot_dir_count;

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

/* ==== main: exercise each screen state and save PPMs ==== */

static void setup_mock_files(void)
{
    strcpy(boot_dir_names[0], "UPDATE.FWSC");
    strcpy(boot_dir_names[1], "MODD_V12.FWSC");
    strcpy(boot_dir_names[2], "MELODEE.FWSC");
    strcpy(boot_dir_names[3], "FELUCCA.FWSC");
    strcpy(boot_dir_names[4], "BACKUP.FWSC");
    boot_dir_count = 5;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    memset(mock_fb, 0, sizeof mock_fb);

    boot_screen_init();
    save_ppm(dir, "01_init.ppm");

    boot_screen_status("WAITING", "INSERT USB DRIVE");
    save_ppm(dir, "02_waiting.ppm");

    boot_screen_flash_begin();
    save_ppm(dir, "03_flash_begin.ppm");

    boot_screen_progress(45, 100);
    save_ppm(dir, "04_progress_45.ppm");

    boot_screen_progress(100, 100);
    save_ppm(dir, "05_progress_100.ppm");

    boot_screen_done();
    save_ppm(dir, "06_done.ppm");

    /* Reset for error screen */
    boot_screen_init();
    boot_screen_error("FLASH FAILED");
    save_ppm(dir, "07_error.ppm");

    /* File browser */
    boot_screen_init();
    scr_text(10u, "SELECT FILE", 3, 11, SCR_FG, SCR_BG);
    scr_text(35u, "OCT-  CANCEL  OCT+  FLASH", 1, 30, SCR_FG, SCR_BG);
    setup_mock_files();
    boot_dir_draw(0);
    save_ppm(dir, "08_browse.ppm");

    boot_dir_draw(2);
    save_ppm(dir, "09_browse_scroll.ppm");

    /* Progress capping: emulator-sized total (6 MB region) with ~45% of
     * the hardware region done.  The bar should show ~45%, not ~4%. */
    boot_screen_init();
    boot_screen_flash_begin();
    {
        uint32_t hw_cap = 0x8F000u;
        uint32_t emu_total = 0x5FC000u;
        uint32_t done_hw_45pct = hw_cap * 45u / 100u;
        uint32_t total = emu_total > hw_cap ? hw_cap : emu_total;
        uint32_t done = done_hw_45pct > total ? total : done_hw_45pct;
        boot_screen_progress(done, total);
    }
    save_ppm(dir, "10_progress_capped.ppm");

    return 0;
}
