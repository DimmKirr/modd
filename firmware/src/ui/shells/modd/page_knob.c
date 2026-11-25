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

#define PG_KNOB_CTL(TAG, UP_IDX, NAME, TITLE, VIEW, FOOT, VIS, L1, L2, L3, L4) \
    static void pg_##TAG##_enter(void)  { pg_knob_enter(UP_IDX, FOOT); }        \
    static void pg_##TAG##_show(void)   { pg_knob_show(UP_IDX); }               \
    static int  pg_##TAG##_k1(int32_t c) { return pg_knob_knob(UP_IDX, ENC_K1, c); } \
    static int  pg_##TAG##_k2(int32_t c) { return pg_knob_knob(UP_IDX, ENC_K2, c); } \
    static int  pg_##TAG##_k3(int32_t c) { return pg_knob_knob(UP_IDX, ENC_K3, c); } \
    static int  pg_##TAG##_k4(int32_t c) { return pg_knob_knob(UP_IDX, ENC_K4, c); } \
    static const ctl_slot_t TAG##_CONTROLS[] = {                                \
        { ENC_K1, 0, 0, 0, 0, 0, L1, pg_##TAG##_k1 },                           \
        { ENC_K2, 0, 0, 0, 0, 0, L2, pg_##TAG##_k2 },                           \
        { ENC_K3, 0, 0, 0, 0, 0, L3, pg_##TAG##_k3 },                           \
        { ENC_K4, 0, 0, 0, 0, 0, L4, pg_##TAG##_k4 },                           \
    };                                                                          \
    static const modd_page_t PAGE_##TAG = {                                     \
        .name = NAME, .title = TITLE, .view = VIEW, .flags = 0,                 \
        .visible = VIS,                                                         \
        .enter = pg_##TAG##_enter, .status = pg_##TAG##_show,                   \
        .controls = TAG##_CONTROLS, .ncontrols = 4,                             \
    }

/* Pure 4-card pages: upstream PAGES[] index → modd PAGE_* struct.
 * Under CONFIG_AUDIO_FELUCCA_STACK_INCLUDED the LFO, VOICE, SCL and ARP pages
 * are also data-driven (1:1 upstream mapping); their custom-widget versions in
 * page_lfo.c / page_voice.c / page_scl.c / page_arp.c compile only when the
 * Felucca stack is not active.                                                 */
/*           TAG            UP  NAME          TITLE              VIEW         FOOT          VIS               */
PG_KNOB_CTL(LFO,            2,  "LFO",        "LFO",             VIEW_LFO,    "LFO",        0, "RATE", "WAVE", "PHS", "FADE");
PG_KNOB_CTL(VOICE,         29,  "VOICE",      "Voice",           VIEW_EDIT,   "VOICE",      0, "VCE", "GLD", "GLMOD", "PRIO");
PG_KNOB_CTL(SCL,           13,  "SCL",        "Scale",           VIEW_SCL,    "SCL",        0, "ROOT", "SCALE", "QNT", "TRANS");
PG_KNOB_CTL(ARP,           38,  "ARP",        "Arpeggiator",     VIEW_ARP,    "ARP",        0, "MODE", "RATE", "OCT", "GATE");
PG_KNOB_CTL(ENV_DEST,       1,  "ENV DEST",   "Env Dest",        VIEW_ENV,    "ENV DEST",   0, "ED_FLT", "ED_PIT", "ED_SHP", "ESYNC");
PG_KNOB_CTL(LFO2,           3,  "LFO 2",      "LFO 2",           VIEW_LFO,    "LFO 2",      0, "LSYNC", "LTRIG", "LPOL", "");
PG_KNOB_CTL(LFO_DEST,       4,  "LFO DEST",   "LFO Dest",        VIEW_LFO,    "LFO DEST",   0, "LD_PIT", "LD_FLT", "LD_SHP", "LD_AMP");
PG_KNOB_CTL(SLICER,         7,  "SLICER",     "Slicer",          VIEW_FX,     "SLICER",     0, "SLCR", "SLPAT", "SLRATE", "SLDEPTH");
PG_KNOB_CTL(INSERT,         8,  "INSERT",     "Insert",          VIEW_FX,     "INSERT",     0, "ITYPE", "IA", "IB", "IC");
PG_KNOB_CTL(INSERT2,        9,  "INSERT 2",   "Insert 2",        VIEW_FX,     "INSERT 2",   0, "IMIX", "", "", "");
PG_KNOB_CTL(DLY,           10,  "DLY",        "Delay",           VIEW_FX,     "DLY",        0, "DTIME", "DFDBK", "DCOLOR", "DMIX");
PG_KNOB_CTL(REVERB,        11,  "REVERB",     "Reverb",          VIEW_FX,     "REVERB",     0, "RTYPE", "RSIZE", "RDAMP", "");
PG_KNOB_CTL(CHORUS,        12,  "CHORUS",     "Chorus",          VIEW_FX,     "CHORUS",     0, "CRATE", "CDEPTH", "", "");
PG_KNOB_CTL(CHORD,         14,  "CHORD",      "Chord",           VIEW_SCL,    "CHORD",      0, "CHRD", "VOIC", "", "");
PG_KNOB_CTL(EDIT2,         16,  "EDIT 2",     "Edit 2",          VIEW_EDIT,   "EDIT 2",     0, "E4", "E5", "E6", "E7");
PG_KNOB_CTL(FILTER,        17,  "FILTER",     "Filter",          VIEW_EDIT,   "FILTER",     lvt_filter_visible, "FTYPE", "E4", "E5", "E7");
PG_KNOB_CTL(LANES,         18,  "LANES",      "Lanes",           VIEW_EDIT,   "LANES",      lvt_lanes_visible, "LN0", "LN1", "LN2", "LN3");
PG_KNOB_CTL(LANES2,        19,  "LANES 2",    "Lanes 2",         VIEW_EDIT,   "LANES 2",    lvt_lanes_visible, "LN4", "LN5", "LN6", "LN7");
PG_KNOB_CTL(VOICE2,        30,  "VOICE 2",    "Voice 2",         VIEW_EDIT,   "VOICE 2",    0, "ALLOC", "DETUNE", "PAN", "MUTE");
PG_KNOB_CTL(VOICE3,        31,  "VOICE 3",    "Voice 3",         VIEW_EDIT,   "VOICE 3",    0, "SPRD", "ATSNS", "ATDST", "ATAMT");
PG_KNOB_CTL(FM6_OP1_ENV,   24,  "OP1 ENV",    "Operator 1 Env",  VIEW_EDIT,   "OP1 ENV",    lvt_fm6_visible, "FM1_ATK", "FM1_DEC", "FM1_SUS", "FM1_REL");
PG_KNOB_CTL(FM6_OP2_ENV,   25,  "OP2 ENV",    "Operator 2 Env",  VIEW_EDIT,   "OP2 ENV",    lvt_fm6_visible, "FM2_ATK", "FM2_DEC", "FM2_SUS", "FM2_REL");
PG_KNOB_CTL(FM6_OP3_ENV,   26,  "OP3 ENV",    "Operator 3 Env",  VIEW_EDIT,   "OP3 ENV",    lvt_fm6_visible, "FM3_ATK", "FM3_DEC", "FM3_SUS", "FM3_REL");
PG_KNOB_CTL(FM6_OP4_ENV,   27,  "OP4 ENV",    "Operator 4 Env",  VIEW_EDIT,   "OP4 ENV",    lvt_fm6_visible, "FM4_ATK", "FM4_DEC", "FM4_SUS", "FM4_REL");
PG_KNOB_CTL(FM6_OP_LEVEL,  28,  "OP LEVEL",   "Operator Levels",  VIEW_EDIT,  "OP LEVEL",   lvt_fm6_visible, "FM1_LVL", "FM2_LVL", "FM3_LVL", "FM4_LVL");
PG_KNOB_CTL(ARP2,          39,  "ARP 2",      "Arp 2",           VIEW_ARP,    "ARP 2",      0, "ASWING", "APROB", "AHOLD", "AORDER");
PG_KNOB_CTL(CLOCK,         44,  "CLOCK",      "Clock",           VIEW_HOME,   "CLOCK",      0, "BPM", "SWING", "CLOCK", "");

#endif /* CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */
