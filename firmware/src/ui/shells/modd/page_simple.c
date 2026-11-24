/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Shared helpers for the plain 4-card parameter pages (ENV DEST, LFO 2, FILTER, ...). */

typedef struct {
    uint16_t pid;       /* P_COUNT = empty slot */
    int16_t  min, max;
    const char *label;
} lvt_simple_t;

static const engine_t *lvt_cur_engine(void)
{
    const track_t *t = &trk[song.sel];
    return (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;
}

/* 1 when the selected track's engine is called `name` (case-insensitive on the ASCII letters) */
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

/* labels: NULL, or 4 strings that replace the table's (engine param names) */
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
        v = modd_tp(&trk[song.sel], s[k].pid);
        span = s[k].max - s[k].min;
        ratio = span > 0 ? (v - s[k].min) * 1000 / span : 0;
        if (s[k].min < 0) lv_snprintf(b, sizeof b, "%+d", (int)v);
        else              lv_snprintf(b, sizeof b, "%d", (int)v);
        lvt_card_set(k, lab, b, "", lvt_pal.primary, ratio);
    }
}

/* A slot is (PID, MIN, MAX, LABEL); (P_COUNT, 0, 0, "") is an empty one. */
#define LVT_SL(PID, MIN, MAX, LAB) { PID, MIN, MAX, LAB }
#define LVT_KM(K, PID, MIN, MAX, LAB) { K, PID, MIN, MAX, 1, 0, LAB }
#define LVT_UNPACK_SL(T) LVT_SL T
#define LVT_EXP(M, ...) M(__VA_ARGS__)
#define LVT_UNPACK_KM(K, T) LVT_EXP(LVT_KM, K, LVT_UNPACK_ARGS T)
#define LVT_UNPACK_ARGS(PID, MIN, MAX, LAB) PID, MIN, MAX, LAB
#define LVT_EMPTY (P_COUNT, 0, 0, "")

/* the show function, the knob map; the caller writes enter and the page struct */
#define LVT_SIMPLE_DATA(ID, S0, S1, S2, S3)                                           \
    static const lvt_simple_t ID##_SLOTS[4] = {                                       \
        LVT_UNPACK_SL(S0), LVT_UNPACK_SL(S1), LVT_UNPACK_SL(S2), LVT_UNPACK_SL(S3) }; \
    static void __attribute__((unused)) lvt_##ID##_show(void) { lvt_simple_show(ID##_SLOTS, 0); }             \
    static const param_slot_t ID##_KNOB_MAP[4] = {                                    \
        LVT_UNPACK_KM(ENC_K1, S0), LVT_UNPACK_KM(ENC_K2, S1),                         \
        LVT_UNPACK_KM(ENC_K3, S2), LVT_UNPACK_KM(ENC_K4, S3) }

#define LVT_SIMPLE_ENTER(ID, FOOT)                                                    \
    static void lvt_##ID##_enter(void)                                                \
    {                                                                                 \
        lv_obj_t *body = lvt_page_begin();                                            \
        lvt_cards(body);                                                              \
        lvt_footer("", FOOT, "");                                                     \
        lvt_##ID##_show();                                                            \
    }

#define LVT_SIMPLE_PAGE(ID, NAME, TITLE, VIEWID, FOOT, VIS, S0, S1, S2, S3)           \
    LVT_SIMPLE_DATA(ID, S0, S1, S2, S3);                                              \
    LVT_SIMPLE_ENTER(ID, FOOT)                                                        \
    static const modd_page_t PAGE_##ID = {                                            \
        .name = NAME, .title = TITLE, .view = VIEWID, .flags = 0, .visible = VIS,     \
        .enter = lvt_##ID##_enter, .status = lvt_##ID##_show,                         \
        .knob_map = ID##_KNOB_MAP, .knob_count = 4,                                   \
    }
