// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Control map adapter: maps physical buttons and encoders to UI views.
 * Each firmware upstream provides its own table; the shell dispatches
 * through it instead of hardcoding button-to-view assignments. */
#ifndef MODD_CONTROLS_H
#define MODD_CONTROLS_H
#include <stdint.h>

/* button IDs (matches hardware matrix after calibration) */
#define BTN_OCTDN   0
#define BTN_OCTUP   1
#define BTN_FX      2
#define BTN_SCL     3
#define BTN_ENV     4
#define BTN_LFO     5
#define BTN_EDIT    6
#define BTN_GLO     7
#define BTN_HOME    8
#define BTN_SAVE    9
#define BTN_ARP    10
#define BTN_SEQ    11
#define BTN_PLAY   12
#define BTN_REC    13
#define BTN_COUNT  14

/* encoder IDs */
#define ENC_SELECT   0
#define ENC_ALGO     1
#define ENC_K1       2
#define ENC_K2       3
#define ENC_K3       4
#define ENC_K4       5
#define ENC_PRESET   6
#define ENC_NONE     0xFF  /* no encoder assigned to this role */

/* what SELECT encoder does when no page claims it */
#define CTL_SELECT_PAGE   0   /* cycle pages within current view */
#define CTL_SELECT_BPM    1   /* adjust tempo */

/* --- Declarative knob-to-parameter slot --- */

#define SLOT_WRAP    (1u << 0)  /* wrap around at boundaries */
#define SLOT_TRACK   (1u << 1)  /* targets trk[slot_index] not trk[song.sel] */

typedef struct {
    uint8_t     enc_id;     /* ENC_K1..ENC_K4 (or ENC_ALGO, etc.) */
    uint8_t     pid;        /* P_* parameter id, P_COUNT = none */
    int16_t     min, max;   /* value range */
    int8_t      step;       /* clicks multiplier (1 or 4 typically) */
    uint8_t     flags;      /* SLOT_* flags */
    const char *label;      /* short label for codegen/MIDI learn */
} param_slot_t;

/* --- Theme encoder roles --- */

typedef struct {
    uint8_t page_cycle;     /* which encoder cycles pages within a view */
    uint8_t list_scroll;    /* which encoder scrolls lists */
    uint8_t value_edit;     /* which encoder edits focused values */
    uint8_t preset_browse;  /* which encoder browses presets */
} theme_enc_roles_t;

/* button action: VIEW_* id on tap/hold, VIEW_NONE for no nav action */
typedef struct modd_controls {
    uint8_t     btn_tap[BTN_COUNT];     /* VIEW_* on tap */
    uint8_t     btn_hold[BTN_COUNT];    /* VIEW_* on hold */
    uint32_t    hold_us;                /* hold threshold in microseconds */
    uint8_t     select_default;         /* CTL_SELECT_PAGE or CTL_SELECT_BPM */
} modd_controls_t;

/* The modd theme ships its own map (ui/themes/modd/controls.c); the tables below serve the
 * ported themes, chosen by the Control model Kconfig choice. */
#ifndef CONFIG_UI_MODD

/* --- Felucca control map --- */

#if defined(CONFIG_CONTROLS_FELUCCA) || \
    (!defined(CONFIG_CONTROLS_MELODEE) && !defined(CONFIG_CONTROLS_SLOOP))

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
        [BTN_FX]    = VIEW_DEMO,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_CONN,
        [BTN_HOME]  = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_NONE,
        [BTN_ARP]   = VIEW_NONE,
        [BTN_SEQ]   = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_PAGE,
};

/* --- Melodee control map --- */

#elif defined(CONFIG_CONTROLS_MELODEE)

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
        [BTN_FX]    = VIEW_DEMO,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_CONN,
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

/* --- SLOOP control map --- */

#elif defined(CONFIG_CONTROLS_SLOOP)

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
        [BTN_FX]    = VIEW_DEMO,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_CONN,
        [BTN_HOME]  = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_NONE,
        [BTN_ARP]   = VIEW_NONE,
        [BTN_SEQ]   = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 700000u,
    .select_default = CTL_SELECT_BPM,
};

#endif

/* --- Encoder roles per upstream --- */

#if defined(CONFIG_CONTROLS_FMLAB)

static const theme_enc_roles_t modd_enc_roles = {
    .page_cycle    = ENC_NONE,
    .list_scroll   = ENC_SELECT,
    .value_edit    = ENC_SELECT,
    .preset_browse = ENC_PRESET,
};

#else

static const theme_enc_roles_t modd_enc_roles = {
    .page_cycle    = ENC_SELECT,
    .list_scroll   = ENC_SELECT,
    .value_edit    = ENC_SELECT,
    .preset_browse = ENC_PRESET,
};

#endif

#endif /* !CONFIG_UI_MODD */

#endif /* MODD_CONTROLS_H */
