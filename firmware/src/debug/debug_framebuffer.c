// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Shadow framebuffer: captures every lcd_blit/lcd_fill into a 240x240
 * RGB565 buffer. Emulator-only (112 KB RAM). Served via REST API.
 *
 * Byte order: the buffer holds plain RGB565 values (0xF800 = red), so its
 * bytes read as little-endian RGB565. The two LCD entry points differ:
 * lcd_fill takes a plain value and swaps it for the panel itself, while
 * lcd_blit takes pixels already in panel order (big-endian, see gfx.c).
 * lcdfb_blit therefore swaps back; lcdfb_fill stores as given. */

#ifdef CONFIG_DEBUG_FRAMEBUFFER

#define LCDFB_W 240u
#define LCDFB_H 240u

/* 112 KB: too large for .bss (96 KB RAM region on real HW).
 * Place in the POOL section which has 336 KB. On the emulator,
 * POOL is backed by WASM linear memory and is effectively unlimited. */
static uint16_t lcd_shadow[LCDFB_W * LCDFB_H] __attribute__((section(".framebuffer")));

static void lcdfb_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const uint16_t *px)
{
    uint32_t row, col;
    for (row = 0; row < h && (y + row) < LCDFB_H; row++)
        for (col = 0; col < w && (x + col) < LCDFB_W; col++) {
            uint16_t v = px[row * w + col];
            lcd_shadow[(y + row) * LCDFB_W + (x + col)] = (uint16_t)((v >> 8) | (v << 8));
        }
}

static void lcdfb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint16_t c)
{
    uint32_t row, col;
    for (row = 0; row < h && (y + row) < LCDFB_H; row++)
        for (col = 0; col < w && (x + col) < LCDFB_W; col++)
            lcd_shadow[(y + row) * LCDFB_W + (x + col)] = c;
}

#endif /* CONFIG_DEBUG_FRAMEBUFFER */
