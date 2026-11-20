// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* LVGL theme: its pages, registered by view: the modd theme's pages (ui/themes/modd/pages.c),
 * same names, same views, same order, same Kconfig gates. */

#ifndef CONFIG_HOME_PAGE_FILES
#include "ui/themes/lvgl/page_home.c"
#endif
#ifdef CONFIG_SYS_MENU
#include "ui/themes/lvgl/page_settings.c"
#endif
#ifdef CONFIG_LWIP
#include "ui/themes/lvgl/page_weather.c"   /* any IP link: WiFi or USB */
#endif
#ifdef CONFIG_UDISK
#include "ui/themes/lvgl/page_files.c"
#include "ui/themes/lvgl/page_disk.c"
#endif
#ifdef CONFIG_PAGE_DEMO
#include "ui/themes/lvgl/page_demo.c"
#endif
#ifdef CONFIG_DEBUG_LOG_BUF
#include "ui/themes/lvgl/page_debug.c"
#endif

static void shell_register_pages(void)
{
    /* VIEW_HOME */
#ifdef CONFIG_HOME_PAGE_FILES
    nav_register(&PAGE_FILES);
#else
    nav_register(&PAGE_HOME);
#endif

    /* VIEW_SETTINGS (HOME hold) */
#ifdef CONFIG_SYS_MENU
    nav_register(&PAGE_SETTINGS);
#endif

    /* VIEW_CONN (FX hold): weather + demo */
#ifdef CONFIG_LWIP
    nav_register(&PAGE_WEATHER);
#endif
#ifdef CONFIG_PAGE_DEMO
    nav_register(&PAGE_DEMO);
#endif

    /* VIEW_DEMO (SAVE hold): USB disk file browser + disk info */
#ifdef CONFIG_UDISK
#ifndef CONFIG_HOME_PAGE_FILES
    nav_register(&PAGE_FILES);
#endif
    nav_register(&PAGE_DISK);
#endif

    /* VIEW_GLO (GLO hold): the log */
#ifdef CONFIG_DEBUG_LOG_BUF
    lvt_dbg_idx = nav_npage;
    nav_register(&PAGE_DEBUG);
#endif
}
