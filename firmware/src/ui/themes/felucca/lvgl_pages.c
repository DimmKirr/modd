// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Felucca LVGL theme: page registrations.
 *
 * The same shared page code as the modd theme (page_*.c in themes/modd/),
 * rendered with the Felucca palettes. Pages that upstream Felucca has but
 * modd does not (SONG, STEP, PRESETS, INSERT, etc.) are added here as they
 * are ported. */

#ifndef CONFIG_HOME_PAGE_FILES
#include "ui/themes/modd/page_home.c"
#endif
#ifdef CONFIG_AUDIO
#include "ui/themes/modd/page_mixer.c"
#include "ui/themes/modd/page_env.c"
#include "ui/themes/modd/page_lfo.c"
#include "ui/themes/modd/page_fx.c"
#include "ui/themes/modd/page_scl.c"
#include "ui/themes/modd/page_edit.c"
#include "ui/themes/modd/page_voice.c"
#include "ui/themes/modd/page_arp.c"
#include "ui/themes/modd/page_seq.c"
#endif
#ifdef CONFIG_SYS_MENU
#include "ui/themes/modd/page_settings.c"
#endif
#ifdef CONFIG_LWIP
#include "ui/themes/modd/page_weather.c"
#endif
#ifdef CONFIG_UDISK
#include "ui/themes/modd/page_files.c"
#include "ui/themes/modd/page_disk.c"
#endif
#ifdef CONFIG_PAGE_DEMO
#include "ui/themes/modd/page_demo.c"
#endif
#include "ui/themes/modd/page_about.c"
#ifdef CONFIG_DEBUG_LOG_BUF
#include "ui/themes/modd/page_debug.c"
#endif

static void shell_register_pages(void)
{
#ifdef CONFIG_HOME_PAGE_FILES
    nav_register(&PAGE_FILES);
#else
    nav_register(&PAGE_HOME);
#endif
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_MIXER);
    nav_register(&PAGE_ENV);
    nav_register(&PAGE_LFO);
    nav_register(&PAGE_FX);
    nav_register(&PAGE_SCL);
    nav_register(&PAGE_EDIT);
    nav_register(&PAGE_VOICE);
#endif
#ifdef CONFIG_SYS_MENU
    nav_register(&PAGE_SETTINGS);
#endif
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_ARP);
    nav_register(&PAGE_SEQ);
#endif
#ifdef CONFIG_LWIP
    nav_register(&PAGE_WEATHER);
#endif
#ifdef CONFIG_PAGE_DEMO
    nav_register(&PAGE_DEMO);
#endif
#ifdef CONFIG_UDISK
#ifndef CONFIG_HOME_PAGE_FILES
    nav_register(&PAGE_FILES);
#endif
    nav_register(&PAGE_DISK);
#endif
    nav_register(&PAGE_ABOUT);
#ifdef CONFIG_DEBUG_LOG_BUF
    lvt_dbg_idx = nav_npage;
    nav_register(&PAGE_DEBUG);
#endif
}
