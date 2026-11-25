/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/gfx.c @ v1.6.1 (165d177a425944f3d30b0b669255a6ed0050c3a5)
 *         https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/gfx.c */
/* Small-canvas renderer: upstream file included unchanged (INCLUDED, no patch).
 * MELODEE_CANVAS_ROWS 124u selects the whole 240 x 124 canvas strip (upstream's option; its default is 64 rows in
 * bands); with it upstream's DF_N / DF_SLOTS come out at 9 / 76, the values modd used. ui_pal_t, ui_fonts.h and
 * ui_palettes.h come from upstream's own definitions and the generated headers (-I build/<defconfig>/gen/melodee). */
#define MELODEE_CANVAS_ROWS 124u

#include "upstream/melodee/firmware/src/gfx.c"
