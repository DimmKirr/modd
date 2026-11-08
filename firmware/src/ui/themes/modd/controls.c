// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd theme's own control map: the theme owns its keymap, so the Control model choice does not
 * apply to it (kconf/ui.kconfig). Two buttons do navigation; everything else is left to pages.
 *   HOME tap   -> HOME view (Hello)
 *   HOME hold  -> SETTINGS view (WiFi, Colors)
 *   GLO  hold  -> CONN view (Weather) */

static const modd_controls_t modd_controls = {
    .btn_tap = {
        [BTN_OCTDN] = VIEW_NONE, [BTN_OCTUP] = VIEW_NONE, [BTN_FX]   = VIEW_NONE,
        [BTN_SCL]   = VIEW_NONE, [BTN_ENV]   = VIEW_NONE, [BTN_LFO]  = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE, [BTN_GLO]   = VIEW_NONE, [BTN_HOME] = VIEW_HOME,
        [BTN_SAVE]  = VIEW_NONE, [BTN_ARP]   = VIEW_NONE, [BTN_SEQ]  = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE, [BTN_REC]   = VIEW_NONE,
    },
    .btn_hold = {
        [BTN_OCTDN] = VIEW_NONE, [BTN_OCTUP] = VIEW_NONE, [BTN_FX]   = VIEW_NONE,
        [BTN_SCL]   = VIEW_NONE, [BTN_ENV]   = VIEW_NONE, [BTN_LFO]  = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE, [BTN_GLO]   = VIEW_CONN, [BTN_HOME] = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_NONE, [BTN_ARP]   = VIEW_NONE, [BTN_SEQ]  = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE, [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_PAGE,
};
