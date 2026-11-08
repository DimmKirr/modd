// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd theme: page registration by view. Real content: HOME. The rest are named stubs. */

#include "ui/themes/modd/page_home.c"
#include "ui/themes/modd/page_wifi.c"
#include "ui/themes/modd/page_colors.c"
#include "ui/themes/modd/page_weather.c"

static void shell_register_pages(void)
{
    /* VIEW_HOME */
    nav_register(&PAGE_HOME);

    /* VIEW_SETTINGS (HOME hold) */
    nav_register(&PAGE_WIFI);
    nav_register(&PAGE_COLORS);

    /* VIEW_CONN (GLO hold) */
    nav_register(&PAGE_WEATHER);
}
