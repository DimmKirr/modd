// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Felucca LVGL skin root, selected by CONFIG_UI_SKIN_FELUCCA.
 *
 * The same LVGL widgets as the modd shell (chrome.c, shell.c, controls.c,
 * shared page files) with the upstream Felucca palettes (GREY default).
 * Included from modd.c after ui_nav.c and ui_elements.c. */

#include <lvgl/lvgl.h>

#include "ui/shells/modd/rt64.c"
#include "ui/shells/modd/port.c"
#include "felucca/shell/lvgl_palette.c"
#include "ui/shells/modd/chrome.c"
#include "felucca/shell/lvgl_controls.c"
#include "ui/shells/modd/list.c"
#include "ui/shells/modd/leds.c"
#include "ui/shells/modd/shell.c"
#include "felucca/shell/lvgl_pages.c"
