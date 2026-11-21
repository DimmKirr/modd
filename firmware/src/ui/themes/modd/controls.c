// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd theme control map.
 *
 *   HOME tap    HOME                 HOME hold   Settings (MENU)
 *   FX hold     WX, again DEMO       SAVE hold   FILES, again DISK
 *   GLO tap     hints (shell.c)      GLO hold    LOG and back (shell.c)
 *   SELECT      on a page that does not take it: the next page of the view (CTL_SELECT_PAGE)
 *
 * The other taps name views no page of either theme is in yet (FX, SCL, ENV, ...): they do
 * nothing until one is. Every entry is written out: 0 is VIEW_HOME, not "none". */

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
