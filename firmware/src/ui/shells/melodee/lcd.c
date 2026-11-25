/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/lcd.c @ v1.6.1 (165d177a425944f3d30b0b669255a6ed0050c3a5)
 *         https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/lcd.c */
/* ST7789 LCD driver: upstream file included unchanged (PATCHED: debug framebuffer hooks).
 * patches/melodee/0001-lcd-debug-framebuffer-hooks.patch adds lcdfb_fill/lcdfb_blit calls
 * under CONFIG_DEBUG_FRAMEBUFFER so the emulator's shadow framebuffer captures every draw. */
#include "upstream/melodee/firmware/src/lcd.c"
