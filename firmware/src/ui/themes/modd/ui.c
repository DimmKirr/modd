// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd theme root, selected by CONFIG_UI_THEME_MODD.
 *
 * Pages, elements and controls drawn by LVGL 9.6 (firmware/vendor/lvgl, MIT) from its own
 * widgets on its default theme, in the modd palettes. No font headers are generated: the
 * type is LVGL's built-in Montserrat. The LEDs (leds.c) do not depend on who draws the
 * screen.
 *
 * LVGL is built as a library beside the unity build (tools/build/build.py build_lvgl);
 * this file and what it includes are the only LVGL code inside modd.c. Its settings are
 * lv_conf.h here. The manual for the vendored version is docs/lvgl/ (vendir.yml): read
 * it before changing anything here, LVGL 8 differs. Where the manual and the headers in
 * firmware/vendor/lvgl/include disagree, the headers are what compiles.
 *
 * Included from modd.c after ui_nav.c and ui_elements.c.
 *
 *   rt64.c      64-bit division the compiler calls for (LVGL needs it)
 *   port.c      display (two strip buffers, lcd.c's DMA), tick
 *   palette.c   the modd palettes as LVGL colours (ui_palettes.h)
 *   chrome.c    styles; header, body, footer, cards; hints; toasts
 *   controls.c  buttons to views: the button map
 *   list.c      the list widget over a modd_list.h source (FILES, Settings INSTR)
 *   leds.c      ui_leds: the panel LEDs
 *   shell.c     shell_* entry points: draw, input, init
 *   pages.c     HOME, MENU, WX, DEMO, FILES, DISK, LOG */

#include <lvgl/lvgl.h>

#include "ui/themes/modd/rt64.c"
#include "ui/themes/modd/port.c"
#include "ui/themes/modd/palette.c"
#include "ui/themes/modd/chrome.c"
#include "ui/themes/modd/controls.c"
#include "ui/themes/modd/list.c"
#include "ui/themes/modd/leds.c"
#include "ui/themes/modd/shell.c"
#include "ui/themes/modd/pages.c"
