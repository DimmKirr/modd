/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: hugelton/Felucca firmware/src/core.h @ v1.6.1 (a53ecf42)
 *         https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/core.h */
/* Audio adapter: types and constants shared between the audio core,
 * engines, effects, sequencer, and UI. */
#ifndef MODD_AUDIO_H
#define MODD_AUDIO_H
#include <stdint.h>

/* --- core constants --- */

#ifndef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED   /* the included stack's core.h defines these */
#define NVOICE 8
#define NPART  4
#define NTRK   NPART
#define NSTEP  64
#endif
#define CTL    32          /* samples per control tick */
#ifdef CONFIG_DOCK
#define BLOCK  256         /* DMA half-buffer: fm1-dock's HALF_FRAMES (its modules render 256-frame blocks; FM-106 renders nothing else) */
#else
#define BLOCK  128         /* DMA half-buffer: 128 stereo samples @ 44.1kHz */
#endif

/* --- audio pipeline precision --- */

#ifdef CONFIG_AUDIO_24BIT
#define AUDIO_Q       23
#define AUDIO_AMP_ONE 8388607           /* Q23 full scale */
#define AUDIO_SAT_MAX 8388607
#define AUDIO_SAT_MIN (-8388607)
#ifndef CONFIG_AUDIO_24BIT_HEADROOM
#define CONFIG_AUDIO_24BIT_HEADROOM 1
#endif
#define AUDIO_OUT_SHIFT CONFIG_AUDIO_24BIT_HEADROOM
static inline int32_t audio_sat(int32_t x)
{
    return x > AUDIO_SAT_MAX ? AUDIO_SAT_MAX
         : x < AUDIO_SAT_MIN ? AUDIO_SAT_MIN : x;
}
#else
#define AUDIO_Q       15
#define AUDIO_AMP_ONE 32767             /* Q15 full scale */
#define AUDIO_OUT_SHIFT 7               /* Q15 << 7 = Q22 in 24-bit DAC word */
static inline int32_t audio_sat(int32_t x)
{
    return x > 32767 ? 32767 : x < -32767 ? -32767 : x;
}
#endif

/* engine params are always the last 8 in the P_* enum */
#include "modd_engine.h"

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
/* The included Felucca stack: upstream core.h is the one source of P_*, G_*, step_t, track_t, song_t, trk[]
 * and song. The engines it indexes are the ones the build has (engines.c keeps upstream's engine numbers). */
#ifdef CONFIG_ENGINE_SLICE
#define FELUCCA_SLICE 1
#else
#define FELUCCA_SLICE 0
#endif
#ifdef CONFIG_ENGINE_ADPCM
#define FELUCCA_ADPCM 1
#else
#define FELUCCA_ADPCM 0
#endif
#define FELUCCA_CPU2 0          /* no second core in this build */
#include "upstream/felucca/firmware/src/core.h"
#else /* !CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */

/* --- parameter IDs (superset of all upstreams) --- */

enum {
    P_LEVEL, P_PAN, P_TUNE, P_FINE,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_FLT, P_RES, P_FENV, P_SHP,
    P_LFO, P_LFRT, P_LFSH, P_LFDL,
    P_GLI, P_UNI, P_UDTN, P_USPRD,
    P_ARP, P_ARTM, P_ARNG, P_AGATE,
    P_OCT, P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_MONO, P_HOLD, P_VELO,
    /* envelope destination (Felucca) */
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,
    /* LFO destination (Felucca) */
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    /* LFO 2: sync, trigger, polarity (Felucca 1.2) */
    P_LSYNC, P_LTRIG, P_LPOL,
    /* arp 2 (Felucca 1.2) */
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    /* sequencer per-track (Felucca) */
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    /* voice (Felucca) */
    P_VOICE, P_GLIDE, P_GLMODE, P_PRIO,
    P_ALLOC, P_DETUNE, P_MUTE,
    /* slicer (Felucca) */
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,
    /* modulation matrix */
    P_M1SRC, P_M1DST, P_M1AMT,
    P_M2SRC, P_M2DST, P_M2AMT,
    P_M3SRC, P_M3DST, P_M3AMT,
    P_M4SRC, P_M4DST, P_M4AMT,
    /* FM6 operator envelopes + levels (Felucca) */
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    /* chord keys (Felucca chord.c) */
    P_CHRD, P_VOIC,
    /* drum lane levels (Felucca eng_drum.c) */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7,
    /* ENV sync (Felucca 1.5) */
    P_ESYNC,
    /* step quantise (Felucca 1.2) */
    P_SQNT,
    /* aftertouch (Felucca 1.8) */
    P_ATSNS, P_ATDST, P_ATAMT,
    /* FX sends (common across upstreams) */
    P_DIST, P_CHOR, P_DLY, P_REV,
    /* Felucca INSERT effect per-track params (1.5) */
    P_ITYPE, P_IMIX, P_IA, P_IB, P_IC,
    /* Felucca spread */
    P_SPRD,
    /* per-engine track params (Felucca 1.5: ANALOG filter type) */
    P_FTYPE,
    /* engine params (always last 8) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

/* --- global (song-level) effect parameters (Felucca) --- */

enum {
    G_BPM, G_SWING, G_CLOCK,
    G_RTYPE, G_RSIZE, G_RDAMP,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_CDEPTH, G_CRATE,
    /* project/tools (Felucca) */
    G_SLOT, G_LOAD, G_SAVE,
    G_CLRSEQ, G_INITSND,
    G_COUNT
};

/* --- step (sequencer pattern data) --- */

typedef struct {
    uint8_t note[4];
    uint8_t vel;
    uint8_t flags;
    uint8_t len;
    uint8_t hit;
} step_t;

/* --- track --- */

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    uint8_t eng_req;
    uint8_t user;
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;
    int32_t lfo_fade;
    uint32_t lfo_rnd;
    int16_t lfo_prev;
    /* modulation matrix state (Felucca mod.c) */
    int32_t m_env;
    int16_t m_rnd;
    uint8_t m_vel, m_key, m_vi, mw, at, ex_off;
    /* effects state (Felucca fx.c) */
    int32_t dist_hp, dist_lp1, dist_lp2;
    int32_t peak;
    uint8_t tail;
    uint8_t ins_run;
    /* keyboard input */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;
    uint8_t latched;
    /* sequencer state */
    step_t step[NSTEP];
    uint32_t seq_pos;
    uint16_t seq_idx;
} track_t;

/* --- song (transport) --- */

typedef struct {
    uint32_t bpm;
    uint32_t tick;
    uint32_t len;
    uint32_t master_q12;
    uint8_t playing;
    uint8_t recording;
    uint8_t rec;
    uint8_t sel;
    int16_t g[G_COUNT];
    int8_t octave;
} song_t;

/* --- globals (the audio core owns these) --- */

static track_t trk[NTRK];
static song_t song;

#endif /* CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */

/* --- engine registry --- */
/* When CONFIG_AUDIO is enabled, engines.c defines ENGINES[] and NENGINES.
 * When audio is off, provide a minimal stub so UI pages compile. */

#ifndef CONFIG_AUDIO

/* When real engines are compiled (CONFIG_ENGINES), engines.c provides
 * ENGINES[] and NENGINES. Otherwise, provide a mock set for the UI. */
#ifndef CONFIG_ENGINES
static const engine_t _eng_analog = {
    .name       = "ANALOG",
    .page_title = {"SYNTH 1", "SYNTH 2"},
    .edit       = {
        { "CUT",  F_CUTOFF, 20, 999, 355, 0, "Hz" },
        { "RES",  F_PCT,     0, 100,  79, 0, "%"  },
        { "ATK",  F_TIME,    0, 999,  10, 0, "ms" },
        { "REL",  F_TIME,    0, 999,  88, 0, "ms" },
        { "SHP",  F_PCT,     0, 100,  50, 0, "%"  },
        { "FLT",  F_PCT,     0, 100,  64, 0, "%"  },
        { "DRV",  F_PCT,     0, 100,   0, 0, "%"  },
        { "MIX",  F_PCT,     0, 100, 100, 0, "%"  },
    },
    .npresets = 12, .knob = {0, 1, 2, 3},
};
static const engine_t _eng_fm6 = {
    .name       = "FM6",
    .page_title = {"FM OP 1-3", "FM OP 4-6"},
    .edit       = {
        { "ALGO", F_INT,  0, 31,   7, 0, ""  },
        { "FB",   F_PCT,  0, 100, 40, 0, "%"  },
        { "ATK",  F_TIME, 0, 999, 15, 0, "ms" },
        { "REL",  F_TIME, 0, 999, 60, 0, "ms" },
        { "MOD",  F_PCT,  0, 100, 72, 0, "%"  },
        { "HARM", F_INT,  0,  15,  3, 0, ""   },
        { "DTUN", F_SEMI, -24, 24, 0, 0, "st" },
        { "MIX",  F_PCT,  0, 100, 90, 0, "%"  },
    },
    .npresets = 20, .knob = {0, 1, 4, 5},
};
static const engine_t _eng_noise = {
    .name       = "NOISE",
    .page_title = {"NOISE", ""},
    .edit       = {
        { "COL",  F_PCT,  0, 100, 50, 0, "%"  },
        { "HPF",  F_CUTOFF, 20, 999, 80, 0, "Hz" },
        { "ATK",  F_TIME, 0, 999,  5, 0, "ms" },
        { "REL",  F_TIME, 0, 999, 200, 0, "ms" },
        { "LVL",  F_PCT,  0, 100, 70, 0, "%"  },
        { "PAN",  F_BIPCT, -100, 100, 0, 0, "" },
        { "SND",  F_PCT,  0, 100, 30, 0, "%"  },
        { "MIX",  F_PCT,  0, 100, 100, 0, "%"  },
    },
    .npresets = 6, .knob = {0, 1, 2, 3},
};
static const engine_t _eng_lofi = {
    .name       = "LOFI",
    .page_title = {"LOFI", ""},
    .edit       = {
        { "BITS", F_INT,  1,  16,  8, 0, ""   },
        { "RATE", F_INT,  1, 100, 44, 0, "kHz"},
        { "DRFT", F_PCT,  0, 100, 20, 0, "%"  },
        { "FILT", F_CUTOFF, 20, 999, 500, 0, "Hz"},
        { "ATK",  F_TIME, 0, 999, 12, 0, "ms" },
        { "REL",  F_TIME, 0, 999, 150, 0, "ms"},
        { "WARM", F_PCT,  0, 100, 65, 0, "%"  },
        { "MIX",  F_PCT,  0, 100, 85, 0, "%"  },
    },
    .npresets = 8, .knob = {0, 1, 2, 6},
};
static const engine_t *const ENGINES[] = {
    &_eng_analog, &_eng_fm6, &_eng_noise, &_eng_lofi
};
#undef NENGINES
#define NENGINES 4
#endif /* !CONFIG_ENGINES */

static void modd_song_init(void);   /* defined below, shared with the real audio_init() */

static void modd_mock_init(void)
{
    uint32_t k;
    modd_song_init();
    song.playing = 1;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_LFO]   = 50;
        trk[k].p[P_LFRT]  = 30;
        trk[k].p[P_DIST]  = 20;
        trk[k].p[P_DLY]   = 35;
        trk[k].p[P_REV]   = 45;
    }
    /* track 0: ANALOG, preset 3, C minor, octave 0 */
    trk[0].preset      = 3;
    trk[0].p[P_ROOT]   = 0;    /* C */
    trk[0].p[P_SCALE]  = 2;    /* minor */
    trk[0].p[P_OCT]    = 0;
    trk[0].step[0].note[0] = 60;  trk[0].step[0].vel = 100;
    trk[0].step[2].note[0] = 64;  trk[0].step[2].vel = 80;
    trk[0].step[4].note[0] = 67;  trk[0].step[4].vel = 90;
    trk[0].step[6].note[0] = 72;  trk[0].step[6].vel = 110;

    /* track 1: FM6, preset 7, D major, octave +1 */
    trk[1].preset      = 7;
    trk[1].p[P_ROOT]   = 2;    /* D */
    trk[1].p[P_SCALE]  = 1;    /* major */
    trk[1].p[P_OCT]    = 1;
    trk[1].step[0].note[0] = 62;  trk[1].step[0].vel = 90;
    trk[1].step[1].note[0] = 66;  trk[1].step[1].vel = 85;

    /* track 2: NOISE, preset 1, E chromatic, octave -1 */
    trk[2].preset      = 1;
    trk[2].p[P_ROOT]   = 4;    /* E */
    trk[2].p[P_SCALE]  = 0;    /* chromatic */
    trk[2].p[P_OCT]    = -1;

    /* track 3: LOFI, preset 5, A pentatonic, octave 0 */
    trk[3].preset      = 5;
    trk[3].p[P_ROOT]   = 9;    /* A */
    trk[3].p[P_SCALE]  = 3;    /* pentatonic */
    trk[3].p[P_OCT]    = 0;
}

#else /* CONFIG_AUDIO */
#ifndef NENGINES
#define NENGINES 1
#endif
#endif /* CONFIG_AUDIO */

/* Song and track defaults: called by audio_init() (real) and modd_mock_init()
 * (UI-only). Sets engine assignments, BPM, selected track, per-track presets.
 * Runs after tracks are zeroed and params_init() has applied engine defaults. */
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
/* defined by audio/core/felucca/stack_modd.c, after the engine registry it reads */
static void modd_song_init(void);
#else
static void modd_song_init(void)
{
    uint32_t k;
#ifndef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
    song.bpm = 128;
#endif
    song.sel = 1;
    song.g[G_BPM]    = 128;
    song.g[G_RTYPE]  = 0;      /* ROOM */
    song.g[G_RSIZE]  = 70;
    song.g[G_RDAMP]  = 60;
    song.g[G_DTIME]  = 2;      /* 1/4 note */
    song.g[G_DFDBK]  = 50;
    song.g[G_DCOLOR] = 64;
    song.g[G_DMIX]   = 80;
    song.g[G_CDEPTH] = 40;
    song.g[G_CRATE]  = 30;
    for (k = 0; k < NTRK; k++) {
        trk[k].eng_req = (uint8_t)(k % NENGINES);
        trk[k].engine  = (uint8_t)(k % NENGINES);
        trk[k].p[P_ATK]   = 10;
        trk[k].p[P_DEC]   = 40;
        trk[k].p[P_SUS]   = 80;
        trk[k].p[P_REL]   = 88;
        trk[k].p[P_LEVEL] = 100;
    }
    /* track 1: FM6 (ENGINES[4]) when available */
    if (NENGINES > 4) {
        trk[1].eng_req = 4;
        trk[1].engine  = 4;
    }
}
#endif

/* --- accessors: the shell, the REST API and the pages read tracks and the song through these, never through the
 * fields that differ between the type sources (modd's own types, or upstream core.h with CONFIG_AUDIO_FELUCCA_STACK_INCLUDED) --- */

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
/* Parameter ids modd's pages use that core.h has no field for. They are stored, not heard ("stored but ignored"):
 * a side array owned by the adapter, ids past P_COUNT (P_COUNT itself stays the empty slot), reached only through
 * modd_tp() and modd_tp_set(). */
enum {
    MODD_XP_BASE = P_COUNT,
    P_TUNE, P_FINE, P_FLT, P_RES, P_FENV, P_SHP, P_LFO, P_LFRT, P_LFSH, P_LFDL,
    P_GLI, P_UNI, P_UDTN, P_USPRD, P_ARP, P_ARTM, P_ARNG, P_OCT, P_MONO, P_HOLD, P_VELO,
    MODD_XP_END
};
#define MODD_XP_N (MODD_XP_END - MODD_XP_BASE - 1)
static int16_t modd_xp[NTRK][MODD_XP_N];
#define MODD_P_VALID(id) ((id) < P_COUNT || ((id) > MODD_XP_BASE && (id) < MODD_XP_END))
static inline int16_t modd_tp(const track_t *t, uint32_t id)
{
    if (id < P_COUNT)
        return t->p[id];
    return id > MODD_XP_BASE && id < MODD_XP_END ? modd_xp[(uint32_t)(t - trk) % NTRK][id - MODD_XP_BASE - 1u] : 0;
}
static int16_t modd_tp_fit(const track_t *t, uint32_t id, int32_t v);   /* stack_modd.c: upstream's own range for the parameter */
static inline void modd_tp_set(track_t *t, uint32_t id, int16_t v)
{
    if (id < P_COUNT)
        t->p[id] = modd_tp_fit(t, id, v);       /* a page's own range is not upstream's (SLCR is 0..2, INSRT 0..8) */
    else if (id > MODD_XP_BASE && id < MODD_XP_END)
        modd_xp[(uint32_t)(t - trk) % NTRK][id - MODD_XP_BASE - 1u] = v;
}
static inline uint32_t modd_engine_of(const track_t *t) { return t->eng_req; }   /* what the panel asked for: engine follows at the next block */
static inline int32_t modd_bpm(void) { return song.g[G_BPM]; }
static inline uint32_t modd_step_len(const step_t *s) { return (uint32_t)(uint8_t)step_nudge(s); }
static inline int32_t modd_step_nudge(const step_t *s) { return step_nudge(s); }
#else
#define MODD_P_VALID(id) ((id) < P_COUNT)
static inline int16_t modd_tp(const track_t *t, uint32_t id) { return t->p[id]; }
static inline void modd_tp_set(track_t *t, uint32_t id, int16_t v) { t->p[id] = v; }
static inline uint32_t modd_engine_of(const track_t *t) { return t->engine; }
static inline int32_t modd_bpm(void) { return (int32_t)song.bpm; }
static inline uint32_t modd_step_len(const step_t *s) { return s->len; }
static inline int32_t modd_step_nudge(const step_t *s) { return (int8_t)s->len; }
#endif

#endif /* MODD_AUDIO_H */
