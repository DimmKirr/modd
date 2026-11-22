// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Felucca LVGL theme root, selected by CONFIG_UI_THEME_FELUCCA.
 *
 * The same LVGL widgets as the modd theme (chrome.c, shell.c, controls.c,
 * shared page files) with the upstream Felucca palettes (GREY default).
 * Included from modd.c after ui_nav.c and ui_elements.c. */

#include <lvgl/lvgl.h>

#include "ui/themes/modd/rt64.c"
#include "ui/themes/modd/port.c"
#include "ui/themes/felucca/lvgl_palette.c"
#include "ui/themes/modd/chrome.c"
#include "ui/themes/modd/controls.c"
#include "ui/themes/modd/list.c"
#include "ui/themes/modd/leds.c"
#include "ui/themes/modd/shell.c"
#include "ui/themes/felucca/lvgl_pages.c"
