// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd shell control map.
 *
 * modd-native:
 *   HOME tap    HOME                 HOME hold   Settings (MENU)
 *   FX hold     WX, again DEMO       SAVE hold   FILES, again DISK
 *   GLO tap     hints (shell.c)      GLO hold    LOG and back (shell.c)
 *
 * Under CONFIG_AUDIO_FELUCCA_STACK_INCLUDED the map matches upstream Felucca:
 *   GLO tap     SONG (VIEW_GLO)      HOME hold   Settings (≈ upstream MENU)
 *   FX hold     none (upstream: layer overlay)
 *   SAVE hold   none (upstream: undo)
 *   SCL/LFO/EDIT/SEQ/REC holds are layer overlays in upstream; none here.
 *
 * SELECT: upstream Felucca defaults to BPM; on a page that claims it, the page's action. */

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
static const modd_controls_t modd_controls = {
    .btn_tap = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_FX,
        [BTN_SCL]   = VIEW_SCL,
        [BTN_ENV]   = VIEW_ENV,
        [BTN_LFO]   = VIEW_LFO,
        [BTN_EDIT]  = VIEW_EDIT,
        [BTN_GLO]   = VIEW_GLO,
        [BTN_HOME]  = VIEW_HOME,
        [BTN_SAVE]  = VIEW_SAVE,
        [BTN_ARP]   = VIEW_ARP,
        [BTN_SEQ]   = VIEW_SEQ,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .btn_hold = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_NONE,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_NONE,
        [BTN_HOME]  = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_NONE,
        [BTN_ARP]   = VIEW_NONE,
        [BTN_SEQ]   = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_BPM,
};
#else
static const modd_controls_t modd_controls = {
    .btn_tap = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_FX,
        [BTN_SCL]   = VIEW_SCL,
        [BTN_ENV]   = VIEW_ENV,
        [BTN_LFO]   = VIEW_LFO,
        [BTN_EDIT]  = VIEW_EDIT,
        [BTN_GLO]   = VIEW_NONE,
        [BTN_HOME]  = VIEW_HOME,
        [BTN_SAVE]  = VIEW_SAVE,
        [BTN_ARP]   = VIEW_ARP,
        [BTN_SEQ]   = VIEW_SEQ,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .btn_hold = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_CONN,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_NONE,
        [BTN_HOME]  = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_DEMO,
        [BTN_ARP]   = VIEW_NONE,
        [BTN_SEQ]   = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_PAGE,
};
#endif
