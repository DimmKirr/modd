/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: layout constants
 * and drawing sequence follow Felucca's ui.c
 * Origin: hugelton/Felucca firmware/src/ui.c @ v1.6.1 (a53ecf42)
 *         https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/ui.c */
/* Felucca-styled UI shell: uses upstream gfx.c renderer (AA fonts, SURF
 * cards, palettes) with mock/adapter data through modd_page_t pages.
 * Visual style matches Felucca; content comes from adapters. */

/* icon rendering: extracted from upstream icons.c (cv_icon_*, cv_trk only;
 * the label-lookup tables are excluded: they depend on full modules) */
#include "ui/shells/felucca/fui_icons.c"

/* Felucca layout constants */
#define H_HEAD   24
#define Y_CARDS  28
#define CARD_W   57
#define CARD_H   44
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define Y_GRAPH  76
#define H_FOOT   38
#define Y_FOOT   (240 - H_FOOT)

#include "ui/shells/felucca/draw.c"
#include "ui/shells/felucca/theme.c"
#include "ui/shells/felucca/shell.c"
#include "ui/shells/felucca/page_home.c"
#include "ui/shells/felucca/page_params.c"
#include "ui/shells/felucca/page_seq.c"
#include "ui/shells/felucca/page_mixer.c"
#include "ui/shells/felucca/page_about.c"
#ifdef CONFIG_UDISK
#include "ui/shells/felucca/page_files.c"
#endif
#ifdef CONFIG_BLE
#include "ui/shells/felucca/page_ble.c"
#endif
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
#include "ui/shells/felucca/page_wifi.c"
#endif
#ifdef CONFIG_WIFI
#include "ui/shells/felucca/page_weather.c"
#endif
#ifdef CONFIG_DEBUG_LOG_BUF
#include "ui/shells/felucca/page_debug.c"
#endif
#ifdef CONFIG_SYS_MENU
#include "ui/shells/felucca/page_sys.c"
#endif
#include "ui/shells/felucca/dlg_confirm.c"
#include "ui/shells/felucca/kbd_piano.c"
#include "ui/shells/felucca/page_demo.c"
#include "ui/shells/felucca/pages.c"
