// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd theme: page registration by view. */

#include "ui/themes/modd/page_home.c"
#include "ui/themes/modd/page_settings.c"
#include "ui/themes/modd/page_weather.c"
#ifdef CONFIG_UDISK
#include "ui/themes/modd/page_files.c"
#include "ui/themes/modd/page_disk.c"
#endif
#include "ui/themes/modd/page_demo.c"
#ifdef CONFIG_DEBUG_LOG
#include "ui/themes/modd/page_debug.c"
#endif

static void shell_register_pages(void)
{
    /* VIEW_HOME */
    nav_register(&PAGE_HOME);

    /* VIEW_SETTINGS (HOME hold) */
    nav_register(&PAGE_SETTINGS);

    /* VIEW_CONN (GLO hold): weather + demo */
    nav_register(&PAGE_WEATHER);
    nav_register(&PAGE_DEMO);

    /* VIEW_DEMO (SAVE hold): USB disk file browser + disk info */
#ifdef CONFIG_UDISK
    nav_register(&PAGE_FILES);
    nav_register(&PAGE_DISK);
#endif

    /* VIEW_GLO: debug log (GLO tap toggle) */
#ifdef CONFIG_DEBUG_LOG
    nav_dbg_idx = nav_npage;
    nav_register(&PAGE_DEBUG);
#endif
}
