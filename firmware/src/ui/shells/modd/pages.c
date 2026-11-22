// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd shell: its pages, registered by view, each under its Kconfig gate. */

#ifndef CONFIG_HOME_PAGE_FILES
#include "ui/shells/modd/page_home.c"
#endif
#ifdef CONFIG_AUDIO
#include "ui/shells/modd/page_mixer.c"
#include "ui/shells/modd/page_env.c"
#include "ui/shells/modd/page_lfo.c"
#include "ui/shells/modd/page_fx.c"
#include "ui/shells/modd/page_scl.c"
#include "ui/shells/modd/page_edit.c"
#include "ui/shells/modd/page_voice.c"
#include "ui/shells/modd/page_arp.c"
#include "ui/shells/modd/page_seq.c"
#include "ui/shells/modd/page_simple.c"
#include "ui/shells/modd/page_env_dest.c"
#include "ui/shells/modd/page_user.c"
#include "ui/shells/modd/page_presets.c"
#include "ui/shells/modd/page_phrases.c"
#include "ui/shells/modd/page_project.c"
#include "ui/shells/modd/page_tools.c"
#include "ui/shells/modd/page_step.c"
#include "ui/shells/modd/page_detail.c"
#include "ui/shells/modd/page_automation.c"
#include "ui/shells/modd/page_lfo2.c"
#include "ui/shells/modd/page_lfo_dest.c"
#include "ui/shells/modd/page_mod.c"
#include "ui/shells/modd/page_chord.c"
#include "ui/shells/modd/page_arp2.c"
#include "ui/shells/modd/page_edit2.c"
#include "ui/shells/modd/page_filter.c"
#include "ui/shells/modd/page_voice2.c"
#include "ui/shells/modd/page_voice3.c"
#include "ui/shells/modd/page_lanes.c"
#include "ui/shells/modd/page_lanes2.c"
#include "ui/shells/modd/page_slicer.c"
#include "ui/shells/modd/page_insert.c"
#include "ui/shells/modd/page_insert2.c"
#include "ui/shells/modd/page_fm6_ops.c"
#include "ui/shells/modd/page_dly.c"
#include "ui/shells/modd/page_reverb.c"
#include "ui/shells/modd/page_chorus.c"
#include "ui/shells/modd/page_clock.c"
#include "ui/shells/modd/page_song.c"
#endif
#ifdef CONFIG_SYS_MENU
#include "ui/shells/modd/page_settings.c"
#include "ui/shells/modd/page_brightness.c"
#include "ui/shells/modd/page_palette.c"
#include "ui/shells/modd/page_select_mode.c"
#include "ui/shells/modd/page_midi_ch.c"
#include "ui/shells/modd/page_transpose.c"
#endif
#ifdef CONFIG_LWIP
#include "ui/shells/modd/page_weather.c"   /* any IP link: WiFi or USB */
#endif
#ifdef CONFIG_UDISK
#include "ui/shells/modd/page_files.c"
#include "ui/shells/modd/page_disk.c"
#endif
#ifdef CONFIG_PAGE_DEMO
#include "ui/shells/modd/page_demo.c"
#endif
#include "ui/shells/modd/page_about.c"
#ifdef CONFIG_DEBUG_LOG_BUF
#include "ui/shells/modd/page_debug.c"
#endif

static void shell_register_pages(void)
{
    /* VIEW_HOME */
#ifdef CONFIG_HOME_PAGE_FILES
    nav_register(&PAGE_FILES);
#else
    nav_register(&PAGE_HOME);
#endif
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_MIXER);
    nav_register(&PAGE_CLOCK);
    nav_register(&PAGE_SONG);
#endif

    /* VIEW_ENV */
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_ENV);
    nav_register(&PAGE_ENV_DEST);
#endif

    /* VIEW_LFO */
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_LFO);
    nav_register(&PAGE_LFO2);
    nav_register(&PAGE_LFO_DEST);
    nav_register(&PAGE_MOD);
#endif

    /* VIEW_FX */
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_FX);
    nav_register(&PAGE_SLICER);
    nav_register(&PAGE_INSERT);
    nav_register(&PAGE_INSERT2);
    nav_register(&PAGE_DLY);
    nav_register(&PAGE_REVERB);
    nav_register(&PAGE_CHORUS);
#endif

    /* VIEW_SCL */
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_SCL);
    nav_register(&PAGE_CHORD);
#endif

    /* VIEW_EDIT */
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_EDIT);
    nav_register(&PAGE_VOICE);
    nav_register(&PAGE_EDIT2);
    nav_register(&PAGE_FILTER);
    nav_register(&PAGE_VOICE2);
    nav_register(&PAGE_VOICE3);
    nav_register(&PAGE_LANES);
    nav_register(&PAGE_LANES2);
    nav_register(&PAGE_FM6_OP1_ENV);
    nav_register(&PAGE_FM6_OP2_ENV);
    nav_register(&PAGE_FM6_OP3_ENV);
    nav_register(&PAGE_FM6_OP4_ENV);
    nav_register(&PAGE_FM6_OP_LEVEL);
#endif

    /* VIEW_SETTINGS (HOME hold) */
#ifdef CONFIG_SYS_MENU
    nav_register(&PAGE_SETTINGS);
    nav_register(&PAGE_BRIGHTNESS);
    nav_register(&PAGE_PALETTE);
    nav_register(&PAGE_SELECT_MODE);
    nav_register(&PAGE_MIDI_CH);
    nav_register(&PAGE_TRANSPOSE);
#endif

    /* VIEW_ARP */
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_ARP);
    nav_register(&PAGE_ARP2);
#endif

    /* VIEW_SEQ */
#ifdef CONFIG_AUDIO
    nav_register(&PAGE_SEQ);
    nav_register(&PAGE_STEP);
    nav_register(&PAGE_DETAIL);
    nav_register(&PAGE_AUTOMATION);
    nav_register(&PAGE_USER);
    nav_register(&PAGE_PRESETS);
    nav_register(&PAGE_PHRASES);
    nav_register(&PAGE_PROJECT);
    nav_register(&PAGE_TOOLS);
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

    /* VIEW_GLO (GLO hold): about + log */
    nav_register(&PAGE_ABOUT);
#ifdef CONFIG_DEBUG_LOG_BUF
    lvt_dbg_idx = nav_npage;
    nav_register(&PAGE_DEBUG);
#endif
}
