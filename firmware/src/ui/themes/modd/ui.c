// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd theme root, selected by CONFIG_UI_MODD.
 * The modd UI is built on Melodee's renderer: ui/themes/melodee/gfx.c (Kerem Kilic, GPL-3.0-only,
 * ported unchanged) draws anti-aliased Rubik type into a small canvas and blits it; its palette
 * tokens (T_BG, T_TEXT, T_THEME ...) colour everything here. The generated tables it needs come
 * from Melodee's own generators plus modd's palette list (tools/codegen/gen_modd_palettes.py),
 * run by tools/build/build.py gen_melodee_ui() when CONFIG_GEN_MELODEE_UI is set.
 * Included from modd.c after ui_nav.c and ui_elements.c. */

#include "ui/themes/melodee/gfx.c"
#include "ui_font_xs.h"
#include "ui/themes/modd/theme.c"
#include "ui/themes/modd/controls.c"
#include "ui/themes/modd/list.c"
#include "ui/themes/modd/leds.c"
#include "ui/themes/modd/shell.c"
#include "ui/themes/modd/pages.c"
