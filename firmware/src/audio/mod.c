/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Origin: firmware/upstream/felucca/firmware/src/mod.c (adapted for modd) */
/* Modulation matrix (Felucca): per track four slots (plus AFTERTOUCH's), each SRC -> DST by AMT (-64..63),
 * run in the audio ISR (fx.c mix_part: mod_begin / mod_end; voice.c track_render: mod_voice; trk_note_on: mod_note).
 * See the upstream file for the full description of sources and destinations.
 * modd differences: one core (no per-part MOD()); no track_desc() / TP[] tables (per-block ranges come from the
 * engine's edit[] for E1..E8, 0..127 for the track parameters, -63..63 for PAN); the DIST/CHO/DLY/REV sends use
 * modd's P_DIST/P_CHOR/P_DLY/P_REV; TIME (a step played late) is kept for the sequencer to call. */
enum { MS_OFF, MS_LFO, MS_ENV, MS_VEL, MS_KEY, MS_RAND, MS_MODW, MS_AT, MS_EXPR, MS_SH, MS_SLEW, MS_N };
enum { MD_OFF, MD_PITCH, MD_CUT, MD_SHP, MD_AMP, MD_PAN, MD_DIST, MD_CHO, MD_DLY, MD_REV, MD_RATE, MD_VIB, MD_E1,
       MD_DEPTH = MD_E1 + 8, MD_VEL, MD_TIME, MD_N };   /* (append-only: the stored values keep their meaning) */
_Static_assert(P_M2SRC == P_M1SRC + 3 && P_M4AMT == P_M1SRC + 11, "matrix slots: 3 ids each");
#define NMSLOT 4u
#define NMSLOT_ALL (NMSLOT + 1u)  /* + 1.8's AFTERTOUCH (VOICE 3): a slot of its own, its source AT (P_ATDST, P_ATAMT) */
#define MS_VOICE(s) ((s) >= MS_ENV && (s) <= MS_RAND)             /* a value per voice */
#define MS_BIPOLAR(s) ((s) == MS_LFO || (s) == MS_KEY || (s) == MS_RAND || (s) >= MS_SH)
#define MD_ENGINE(d) ((d) >= MD_E1 && (d) < MD_E1 + 8)           /* E1..E8: P_E0..P_E7 */

/* slot k's source, destination, amount (k == NMSLOT: AFTERTOUCH's) */
static inline uint32_t ms_src(const int16_t *p, uint32_t k) { return k < NMSLOT ? (uint32_t)p[P_M1SRC + 3u * k] : MS_AT; }
static inline int32_t ms_dst(const int16_t *p, uint32_t k) { return k < NMSLOT ? p[P_M1DST + 3u * k] : p[P_ATDST]; }
static inline int32_t ms_amt(const int16_t *p, uint32_t k) { return k < NMSLOT ? p[P_M1AMT + 3u * k] : p[P_ATAMT]; }
/* the slots to look at: AFTERTOUCH's only while it routes (the matrix's own loops as before otherwise) */
static inline uint32_t ms_n(const int16_t *p) { return NMSLOT + (p[P_ATDST] && p[P_ATAMT]); }

static const char *const N_MSRC[MS_N] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "S&H", "SLEW"};
static const char *const N_MDST[MD_N] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "DIST", "CHO", "DLY", "REV",
                                         "RATE", "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "DEPTH",
                                         "VEL", "TIME"};

/* the track parameter of a per-block destination */
static uint32_t mod_param(uint32_t d)
{
    static const uint8_t ID[MD_E1] = {[MD_PAN] = P_PAN, [MD_DIST] = P_DIST, [MD_CHO] = P_CHOR, [MD_DLY] = P_DLY,
                                      [MD_REV] = P_REV, [MD_RATE] = P_LFRT, [MD_VIB] = P_LD_PIT};
    return MD_ENGINE(d) ? P_E0 + d - MD_E1 : ID[d];
}

/* the range of a per-block destination's parameter */
static void mod_range(const track_t *t, uint32_t id, int32_t *mn, int32_t *mx)
{
    if (id >= P_E0) {
        const engine_t *e = t->engine < NENGINES ? ENGINES[t->engine] : 0;
        *mn = e ? e->edit[id - P_E0].min : 0;
        *mx = e ? e->edit[id - P_E0].max : 127;
    } else if (id == P_PAN) {
        *mn = -63;
        *mx = 63;
    } else {
        *mn = 0;
        *mx = 127;
    }
}

typedef struct {                 /* a part's, while mix_part renders it (one per part: two cores render at once, fx.c mix_block) */
    uint8_t on;                  /* a slot is active: track_render calls mod_voice */
    uint8_t amp;                 /* an AMP slot: the voice gain applies */
    uint8_t nv, nk;              /* voice terms, per-block parameters held */
    int32_t pit, cut, shp, gain; /* per voice, from the per-track sources (gain Q15) */
    int32_t ldep;                /* DEPTH: the track LFO's gain (Q15; 32767 = none, track_render leaves it alone) */
    uint8_t vsrc[NMSLOT_ALL], vdst[NMSLOT_ALL];
    int8_t vamt[NMSLOT_ALL];
    uint8_t kid[NMSLOT_ALL];     /* per-block: the parameter, its stored value */
    int16_t keep[NMSLOT_ALL];
} modw_t;
static modw_t mod;                /* modd: one core */
#define MOD(t) mod

static uint32_t mod_seed = 0x2545F491u;
static int16_t mod_rand(void)    /* RAND of a note-on: xorshift32, bipolar */
{
    mod_seed ^= mod_seed << 13;
    mod_seed ^= mod_seed >> 17;
    mod_seed ^= mod_seed << 5;
    return (int16_t)(mod_seed >> 16);
}

static int32_t mod_key(uint32_t note) { return clamp(((int32_t)note - 60) * 512, -32767, 32767); }

/* S&H: the value of the LFO's cycle (voice.c lfo_rand; 0 before its first one ends), bipolar */
static int32_t mod_sh(const track_t *t) { return t->lfo_rnd ? (int32_t)(t->lfo_rnd >> 16) - 32768 : 0; }

/* source s of the track: the per-track ones, the per-voice ones of the latest note-on */
static int32_t mod_tsrc(const track_t *t, uint32_t s, int32_t lfo)
{
    switch (s) {
    case MS_SH:
        return mod_sh(t);
    case MS_SLEW:                                       /* the cycle before's value .. this one's, by the phase */
        return t->lfo_prev + (((mod_sh(t) - t->lfo_prev) * (int32_t)(t->lfo_ph >> 17)) >> 15);
    case MS_LFO:
        return lfo;
    case MS_ENV:
        return t->m_env;
    case MS_VEL:
        return t->m_vel * 258;
    case MS_KEY:
        return mod_key(t->m_key);
    case MS_RAND:
        return t->m_rnd;
    case MS_MODW:
        return t->mw * 258;
    case MS_AT:
        return t->at * 258;
    default:
        return (127 - t->ex_off) * 258;
    }
}

/* AMP: the gain (Q15, 0..32767) of source x (bipolar: as 0..1) at amount a */
static int32_t mod_gain(uint32_t s, int32_t x, int32_t a)
{
    int32_t u = MS_BIPOLAR(s) ? (x + 32768) >> 1 : x;
    return clamp(32767 - (a > 0 ? ((32767 - u) * a) >> 6 : (u * -a) >> 6), 0, 32767);
}

/* a per-voice destination d by source value x, amount a */
static void mod_voice_add(uint32_t s, uint32_t d, int32_t x, int32_t a, int32_t *pit, int32_t *cut, int32_t *shp,
                          int32_t *gain)
{
    if (d == MD_PITCH)
        *pit += (x * a * 3) >> 15;
    else if (d == MD_CUT)
        *cut += (x * a) >> 7;
    else if (d == MD_SHP)
        *shp += (x * a) >> 7;
    else
        *gain = mulq15(*gain, mod_gain(s, x, a));
}

/* DEPTH: the track LFO's gain (Q15) from the DEPTH slots, 32767 = none (lfo: the LFO unscaled; a per-voice source
 * takes the latest note's value, as on the other per-block destinations). Its own function: mod_begin's loop stays
 * what it was */
static __attribute__((noinline)) int32_t mod_depth(const track_t *t, int32_t lfo)
{
    const int16_t *p = t->p;
    int32_t g = 32767;
    uint32_t k;
    for (k = 0; k < NMSLOT; k++) {
        uint32_t s = (uint32_t)p[P_M1SRC + 3u * k];
        if (p[P_M1DST + 3u * k] == MD_DEPTH && p[P_M1AMT + 3u * k] && s && s < MS_N)
            g = mulq15(g, mod_gain(s == MS_LFO && p[P_LPOL] ? MS_ENV : s, mod_tsrc(t, s, lfo), p[P_M1AMT + 3u * k]));
    }
    if (p[P_ATDST] == MD_DEPTH && p[P_ATAMT])           /* 1.8: AFTERTOUCH's */
        g = mulq15(g, mod_gain(MS_AT, mod_tsrc(t, MS_AT, lfo), p[P_ATAMT]));
    return g;
}

/* mod_begin's slot (s, d, a): a per-voice term into mw, or a per-block offset into off (inlined: the matrix's loop
 * as it was, AFTERTOUCH's slot after it) */
static inline __attribute__((always_inline)) void mod_slot(track_t *t, modw_t *mw, int32_t *off, int32_t lfo,
                                                           uint32_t s, uint32_t d, int32_t a)
{
    const int16_t *p = t->p;
    uint32_t j;
    if (!s || !d || !a || s >= MS_N || d >= MD_DEPTH)
        return;
    if (d <= MD_AMP) {                                  /* per voice */
        mw->amp |= d == MD_AMP;
        if (MS_VOICE(s)) {
            mw->vsrc[mw->nv] = (uint8_t)s;
            mw->vdst[mw->nv] = (uint8_t)d;
            mw->vamt[mw->nv++] = (int8_t)a;
        } else {                                        /* (a UNI LFO, 0..1, takes AMP as ENV does) */
            mod_voice_add(s == MS_LFO && p[P_LPOL] ? MS_ENV : s, d, mod_tsrc(t, s, lfo), a, &mw->pit, &mw->cut,
                          &mw->shp, &mw->gain);
        }
        return;
    }
    {                                                   /* per block: AMT 64 = the whole range */
        uint32_t id = mod_param(d);
        int32_t pmin, pmax, x = mod_tsrc(t, s, lfo);
        mod_range(t, id, &pmin, &pmax);
        for (j = 0; j < mw->nk && mw->kid[j] != id; j++)
            ;
        if (j == mw->nk) {
            mw->kid[j] = (uint8_t)id;
            mw->keep[j] = p[id];
            off[j] = 0;
            mw->nk++;
        }
        off[j] += (((x * a) >> 6) * (pmax - pmin)) >> 15;
    }
}

/* before a part's block (fx.c mix_part): the per-track offsets, the per-block destinations into t->p.
 * 0 = no slot active (then nothing changed) */
static __attribute__((noinline)) int mod_begin(track_t *t)
{
    const int16_t *p = t->p;
    int32_t lfo, off[NMSLOT_ALL];
    uint32_t k, j, at = p[P_ATDST] && p[P_ATAMT] && p[P_ATDST] < MD_VEL;
    modw_t *mw = &MOD(t);
    mw->on = 0;
    for (k = 0; k < NMSLOT; k++)                        /* (VEL and TIME: at the notes, not here) */
        if (p[P_M1SRC + 3u * k] && p[P_M1DST + 3u * k] && p[P_M1AMT + 3u * k] && p[P_M1DST + 3u * k] < MD_VEL)
            break;
    if (k == NMSLOT && !at)
        return 0;
    lfo = mulq15(t->lfo_val, t->lfo_fade);              /* (the LFO of the block before: track_render ticks it) */
    mw->on = 1;
    mw->amp = 0;
    mw->nv = mw->nk = 0;
    mw->pit = mw->cut = mw->shp = 0;
    mw->gain = 32767;
    mw->ldep = mod_depth(t, lfo);                       /* DEPTH first: the other slots see the LFO it leaves */
    if (mw->ldep < 32767)
        lfo = mulq15(lfo, mw->ldep);
    for (; k < NMSLOT; k++)
        mod_slot(t, mw, off, lfo, (uint32_t)p[P_M1SRC + 3u * k], (uint32_t)p[P_M1DST + 3u * k], p[P_M1AMT + 3u * k]);
    if (at)                                             /* 1.8: AFTERTOUCH's */
        mod_slot(t, mw, off, lfo, MS_AT, (uint32_t)p[P_ATDST], p[P_ATAMT]);
    for (j = 0; j < mw->nk; j++) {
        uint32_t id = mw->kid[j];
        int32_t pmin, pmax;
        mod_range(t, id, &pmin, &pmax);
        t->p[id] = (int16_t)clamp(mw->keep[j] + off[j], pmin, pmax);
    }
    return 1;
}

/* after the part's block (when mw->on): the stored values back */
static __attribute__((noinline)) void mod_end(track_t *t)
{
    uint32_t j;
    modw_t *mw = &MOD(t);
    for (j = 0; j < mw->nk; j++)
        t->p[mw->kid[j]] = mw->keep[j];
    mw->on = 0;
}

/* a voice's modulation (voice.c track_render, when mw->on), onto its vmod_t m as track_render made it:
 * pitch (the increment again, with the fine factor fine as there), cutoff and shape (clamped to what the
 * fixed routings can reach), the amplitude (and v->env_out, the next block's start). env: its envelope */
static __attribute__((noinline)) void mod_voice(track_t *t, voice_t *v, vmod_t *m, int32_t fine)
{
    const modw_t *mw = &MOD(t);
    if (!mw->on)
        return;
    int32_t pit = mw->pit, cut = mw->cut, shp = mw->shp, gain = mw->gain, env = m->envq15;
    uint32_t k;
    if (v == &t->v[t->m_vi])
        t->m_env = env;                                 /* ENV of the latest note, for per-block destinations */
    for (k = 0; k < mw->nv; k++) {
        uint32_t s = mw->vsrc[k];
        int32_t x = s == MS_ENV ? env : s == MS_VEL ? v->mvel * 258 : s == MS_KEY ? mod_key(v->note) : v->mrnd;
        mod_voice_add(s, mw->vdst[k], x, mw->vamt[k], &pit, &cut, &shp, &gain);
    }
    if (pit) {
        m->pitch16 = clamp(m->pitch16 + pit, 0, 2047);
        m->inc = pitch_inc(m->pitch16);
        if (fine)
            m->inc += (uint32_t)((int32_t)(m->inc >> 12) * fine);
    }
    m->cutoff = clamp(m->cutoff + cut, -150 * 256, 150 * 256);
    m->shape = clamp(m->shape + shp, -62 * 256, 190 * 256);
    if (mw->amp) {
        m->amp1 = mulq15(m->amp1, gain);
        v->env_out = m->amp1;
    }
}

/* 1.5.6 (#213): a slot on destination d (VEL, TIME) is active (not inlined: its loop stays off the ISRs' own) */
static __attribute__((noinline)) int mod_has(const track_t *t, uint32_t d)
{
    const int16_t *p = t->p;
    uint32_t k;
    for (k = 0; k < ms_n(p); k++)
        if (ms_dst(p, k) == (int32_t)d && ms_amt(p, k) && ms_src(p, k) > 0 && ms_src(p, k) < MS_N)
            return 1;
    return 0;
}

/* 1.5.6 (#213): the slots on destination d (VEL, TIME) at a note of velocity vel, RAND rnd: VEL its offset (AMT 64 at
 * a full source = 127), TIME its share of the longest delay (Q15, 0..; bipolar sources as 0..1, AMT < 0 inverted, as
 * AMP). The per-track sources as now (the LFO with its FADE and DEPTH), ENV the latest note's. Not inlined: once a
 * note-on / step, and only with such a slot */
static __attribute__((noinline)) int32_t mod_at_note(const track_t *t, uint32_t d, uint32_t note, uint32_t vel,
                                                     int32_t rnd)
{
    const int16_t *p = t->p;
    int32_t sum = 0, lfo = mulq15(t->lfo_val, t->lfo_fade), g = mod_depth(t, lfo);
    uint32_t k;
    if (g < 32767)
        lfo = mulq15(lfo, g);
    for (k = 0; k < ms_n(p); k++) {
        uint32_t s = ms_src(p, k);
        int32_t a = ms_amt(p, k), x;
        if (ms_dst(p, k) != (int32_t)d || !a || !s || s >= MS_N)
            continue;
        x = s == MS_VEL ? (int32_t)vel * 258 : s == MS_KEY ? mod_key(note) : s == MS_RAND ? rnd : mod_tsrc(t, s, lfo);
        if (d == MD_VEL) {
            sum += (((x * a) >> 6) * 127) >> 15;
        } else {
            int32_t u = MS_BIPOLAR(s) && !(s == MS_LFO && p[P_LPOL]) ? (x + 32768) >> 1 : x;
            u = clamp(u, 0, 32767);
            sum += a > 0 ? (u * a) >> 6 : ((32767 - u) * -a) >> 6;
        }
    }
    return sum;
}

/* a note-on of track t (voice.c trk_note_on): the per-voice sources of the latest note; the velocity it plays at
 * (1.5.6: the matrix's VEL, when a slot has it) */
static uint32_t mod_note(track_t *t, uint32_t note, uint32_t vel)
{
    int16_t r = mod_rand();
    if (mod_has(t, MD_VEL))
        vel = (uint32_t)clamp((int32_t)vel + mod_at_note(t, MD_VEL, note, vel, r), 1, 127);
    t->m_vel = (uint8_t)vel;
    t->m_key = (uint8_t)note;
    t->m_rnd = r;
    return vel;
}

/* 1.5.6 (#213): TIME on a step of velocity vel, its first note: how late it plays, in samples (0: on time). At most
 * 30 ms, and 7/16 of the step (period: the step's samples), as the longest NUDGE */
#ifndef NUDGE_DIV
#define NUDGE_DIV 16u
#endif
#define MOD_TIME_MAX ((uint32_t)FS * 30u / 1000u)
static __attribute__((noinline)) uint32_t mod_time(track_t *t, uint32_t note, uint32_t vel, uint32_t period)
{
    uint32_t mx = period * 7u / NUDGE_DIV, d;
    int32_t q;
    if (!mod_has(t, MD_TIME))
        return 0;
    q = clamp(mod_at_note(t, MD_TIME, note, vel, mod_rand()), 0, 32767);
    d = ((uint32_t)q * MOD_TIME_MAX) >> 15;
    return d < mx ? d : mx;
}

/* a MIDI controller for track t (seq.c events_block): CC1 / CC11 / channel aftertouch; CC121 resets them */
static __attribute__((noinline)) void mod_midi(track_t *t, uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st == 0xD0u) {
        t->at = (uint8_t)d1;
    } else if (st == 0xB0u) {
        if (d1 == 1u)
            t->mw = (uint8_t)d2;
        else if (d1 == 11u)
            t->ex_off = (uint8_t)(127u - d2);
        else if (d1 == 121u)
            t->mw = t->at = t->ex_off = 0;
    }
}

static const char *mod_src_name(uint32_t src) { return src < MS_N ? N_MSRC[src] : "---"; }
static const char *mod_dst_name(uint32_t dst) { return dst < MD_N ? N_MDST[dst] : "---"; }
static const char *mod_src_icon(uint32_t src) { (void)src; return ""; }
static const char *mod_dst_icon(uint32_t dst) { (void)dst; return ""; }
static void mod_init(void) { memset(&mod, 0, sizeof mod); }
