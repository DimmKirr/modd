/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Data-driven knob page: each instance is backed by an upstream PAGES[] entry
 * through the modd_pages.h adapter. Replaces the hand-typed 4-card pages. */

/* --- direct-param helpers (kept for pages that bind P_* directly) --- */

typedef struct {
    uint16_t pid;
    int16_t  min, max;
    const char *label;
} lvt_simple_t;

static void lvt_simple_show(const lvt_simple_t *s, const char *const *labels)
{
    uint32_t k;
    char b[8];
    for (k = 0; k < 4u; k++) {
        int32_t v, span, ratio;
        const char *lab = (labels && labels[k]) ? labels[k] : s[k].label;
        if (!MODD_P_VALID(s[k].pid)) {
            lvt_card_set(k, "", "", "", lvt_pal.dim, -1);
            continue;
        }
        v = modd_param(modd_trk(modd_sel()), s[k].pid);
        span = s[k].max - s[k].min;
        ratio = span > 0 ? (v - s[k].min) * 1000 / span : 0;
        if (s[k].min < 0) lv_snprintf(b, sizeof b, "%+d", (int)v);
        else              lv_snprintf(b, sizeof b, "%d", (int)v);
        lvt_card_set(k, lab, b, "", lvt_pal.primary, ratio);
    }
}

/* --- helpers (moved from page_simple.c) --- */

static const engine_t *lvt_cur_engine(void)
{
    return modd_engine_of(modd_trk(modd_sel())) < NENGINES
        ? ENGINES[modd_engine_of(modd_trk(modd_sel()))] : 0;
}

static int lvt_engine_is(const char *name)
{
    const engine_t *e = lvt_cur_engine();
    const char *a, *b;
    if (!e || !e->name) return 0;
    for (a = e->name, b = name; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
    }
    return *a == *b;
}

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED

/* --- visibility helpers --- */
static int lvt_fm6_visible(void) { return lvt_engine_is("FM6"); }
static int lvt_filter_visible(void) { return lvt_engine_is("ANALOG"); }
static int lvt_lanes_visible(void) { return lvt_engine_is("DRUM"); }

/* --- common rendering --- */

static void pg_knob_show(uint32_t up_idx)
{
    modd_card_t cards[4];
    uint32_t n = modd_pg_read(up_idx, cards, 4), k;
    for (k = 0; k < 4u; k++) {
        if (k < n) {
            int32_t span = cards[k].max - cards[k].min;
            int32_t ratio = span > 0
                ? (cards[k].value - cards[k].min) * 1000 / span : 0;
            lvt_card_set(k, cards[k].label, cards[k].text, "",
                         lvt_pal.primary, ratio);
        } else {
            lvt_card_set(k, "", "", "", lvt_pal.dim, -1);
        }
    }
}

static void pg_knob_enter(uint32_t up_idx, const char *foot)
{
    lv_obj_t *body = lvt_page_begin();
    lvt_cards(body);
    lvt_footer("", foot, "");
    pg_knob_show(up_idx);
}

static int pg_knob_knob(uint32_t up_idx, uint32_t id, int32_t clicks)
{
    uint32_t slot;
    if      (id == ENC_K1) slot = 0;
    else if (id == ENC_K2) slot = 1;
    else if (id == ENC_K3) slot = 2;
    else if (id == ENC_K4) slot = 3;
    else return 0;
    if (modd_pg_turn(up_idx, slot, clicks) != 0)
        return 0;
    nav_dirty = 1;
    return 1;
}

/* --- page instances (macro generates trampolines) --- */

#define PG_KNOB(TAG, UP_IDX, NAME, TITLE, VIEW, FOOT, VIS)                \
    static void pg_##TAG##_enter(void)  { pg_knob_enter(UP_IDX, FOOT); }   \
    static void pg_##TAG##_show(void)   { pg_knob_show(UP_IDX); }          \
    static int  pg_##TAG##_knob(uint32_t id, int32_t c)                    \
        { return pg_knob_knob(UP_IDX, id, c); }                           \
    static const modd_page_t PAGE_##TAG = {                                \
        .name = NAME, .title = TITLE, .view = VIEW, .flags = 0,           \
        .visible = VIS,                                                    \
        .enter = pg_##TAG##_enter, .status = pg_##TAG##_show,              \
        .knob = pg_##TAG##_knob,                                           \
    }

/* Pure 4-card pages: upstream PAGES[] index → modd PAGE_* struct.
 * Pages with custom LVGL widgets (ENV curve, LFO wave, SCL wheel, ARP pattern)
 * keep their own files and read data through the adapter separately.           */
/*           TAG            UP  NAME          TITLE              VIEW         FOOT          VIS               */
PG_KNOB(ENV_DEST,       1,  "ENV DEST",   "Env Dest",        VIEW_ENV,    "ENV DEST",   0);
PG_KNOB(LFO2,           3,  "LFO 2",      "LFO 2",           VIEW_LFO,    "LFO 2",      0);
PG_KNOB(LFO_DEST,       4,  "LFO DEST",   "LFO Dest",        VIEW_LFO,    "LFO DEST",   0);
PG_KNOB(SLICER,         7,  "SLICER",     "Slicer",          VIEW_FX,     "SLICER",     0);
PG_KNOB(INSERT,         8,  "INSERT",     "Insert",          VIEW_FX,     "INSERT",     0);
PG_KNOB(INSERT2,        9,  "INSERT 2",   "Insert 2",        VIEW_FX,     "INSERT 2",   0);
PG_KNOB(DLY,           10,  "DLY",        "Delay",           VIEW_FX,     "DLY",        0);
PG_KNOB(REVERB,        11,  "REVERB",     "Reverb",          VIEW_FX,     "REVERB",     0);
PG_KNOB(CHORUS,        12,  "CHORUS",     "Chorus",          VIEW_FX,     "CHORUS",     0);
PG_KNOB(CHORD,         14,  "CHORD",      "Chord",           VIEW_SCL,    "CHORD",      0);
PG_KNOB(EDIT2,         16,  "EDIT 2",     "Edit 2",          VIEW_EDIT,   "EDIT 2",     0);
PG_KNOB(FILTER,        17,  "FILTER",     "Filter",          VIEW_EDIT,   "FILTER",     lvt_filter_visible);
PG_KNOB(LANES,         18,  "LANES",      "Lanes",           VIEW_EDIT,   "LANES",      lvt_lanes_visible);
PG_KNOB(LANES2,        19,  "LANES 2",    "Lanes 2",         VIEW_EDIT,   "LANES 2",    lvt_lanes_visible);
PG_KNOB(VOICE2,        30,  "VOICE 2",    "Voice 2",         VIEW_EDIT,   "VOICE 2",    0);
PG_KNOB(VOICE3,        31,  "VOICE 3",    "Voice 3",         VIEW_EDIT,   "VOICE 3",    0);
PG_KNOB(FM6_OP1_ENV,   24,  "OP1 ENV",    "Operator 1 Env",  VIEW_EDIT,   "OP1 ENV",    lvt_fm6_visible);
PG_KNOB(FM6_OP2_ENV,   25,  "OP2 ENV",    "Operator 2 Env",  VIEW_EDIT,   "OP2 ENV",    lvt_fm6_visible);
PG_KNOB(FM6_OP3_ENV,   26,  "OP3 ENV",    "Operator 3 Env",  VIEW_EDIT,   "OP3 ENV",    lvt_fm6_visible);
PG_KNOB(FM6_OP4_ENV,   27,  "OP4 ENV",    "Operator 4 Env",  VIEW_EDIT,   "OP4 ENV",    lvt_fm6_visible);
PG_KNOB(FM6_OP_LEVEL,  28,  "OP LEVEL",   "Operator Levels",  VIEW_EDIT,  "OP LEVEL",   lvt_fm6_visible);
PG_KNOB(ARP2,          39,  "ARP 2",      "Arp 2",           VIEW_ARP,    "ARP 2",      0);
PG_KNOB(CLOCK,         44,  "CLOCK",      "Clock",           VIEW_HOME,   "CLOCK",      0);

#endif /* CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */
