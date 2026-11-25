/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca core: the upstream audio stack + sequencer (CONFIG_CORE_FELUCCA). The ISR is in audio/core/audio.c.
 * Origin: hugelton/Felucca firmware/src/{params,mod,voice,slicer,fx,perform,click,song_chain,seq,chord,motion,
 *         midi_control,midi_clock}.c @ v1.8.2 (6162de86)
 * The upstream files are #included unchanged from firmware/upstream/felucca/firmware/src/; this file and
 * firmware/adapters/modd_audio.h hold the adapter glue. */

/* --- glue: what upstream's engines.c / eng_drum.c / eng_fm6.c give the stack, for the engines this
 * build does not have (ENG_DRUM and the FM6 patch list are absent engines in engines.c). Names and constants only. --- */
enum { DK_COUNT = 1 };
static const uint8_t DK_PLAYS[DK_COUNT] = {0};
static const char *const N_DRUM_KIT[DK_COUNT] = {"-"};
#ifndef CONFIG_ENGINE_FM6_FELUCCA
static const char *const N_FM6_PATCH[1] = {"-"};
#define FM6_OWN 0
static int fm6_slot_ok(int32_t s) { (void)s; return 1; }
#endif
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }
static void eng_mem_claim(const track_t *t) { (void)t; }
static voice_t *drum_reuse(track_t *t, uint32_t note) { (void)t; (void)note; return 0; }
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};
static uint32_t drum_lane(uint32_t note) { uint32_t i; for (i = 0; i < NLANE; i++) if (DRUM_LANE_NOTE[i] == note) return i; return 0; }
static uint32_t kb_out_tick;

/* Buffer sizes from Kconfig, through patches/felucca/0001 (#ifndef around upstream's three #defines; the
 * patched copies are first on the include path). Unsigned constant expressions, as upstream's 65536u. */
#define DLY_LEN (CONFIG_FELUCCA_DLY_LEN + 0u)
#define CHO_LEN (CONFIG_FELUCCA_CHO_LEN + 0u)
#define SL_LEN (CONFIG_FELUCCA_SL_LEN + 0u)
_Static_assert((DLY_LEN & (DLY_LEN - 1u)) == 0u, "CONFIG_FELUCCA_DLY_LEN must be a power of two");
_Static_assert((CHO_LEN & (CHO_LEN - 1u)) == 0u, "CONFIG_FELUCCA_CHO_LEN must be a power of two");
_Static_assert((SL_LEN & (SL_LEN - 1u)) == 0u, "CONFIG_FELUCCA_SL_LEN must be a power of two");

#include "upstream/felucca/firmware/src/params.c"
static int16_t modd_tp_fit(const track_t *t, uint32_t id, int32_t v)
{
    return (int16_t)param_fit(param_desc_of(eng_idx(t->eng_req), id), v);
}
#include "upstream/felucca/firmware/src/mod.c"
#include "upstream/felucca/firmware/src/voice.c"
static void mod_init(void) {}
#include "upstream/felucca/firmware/src/slicer.c"
#include "upstream/felucca/firmware/src/fx.c"
_Static_assert((PB_FRAMES & (PB_FRAMES - 1u)) == 0u, "PB_FRAMES (NTRK * SL_LEN / 2) must be a power of two");
_Static_assert(PB_FREC <= PB_FRAMES, "CONFIG_FELUCCA_SL_LEN is too small for FREEZE (PB_FREC frames)");
_Static_assert(HB_MASK < PB_FRAMES, "CONFIG_FELUCCA_SL_LEN is too small for the OCT UP / DN delay (HB_MASK)");

/* --- sequencer (upstream: song_chain → seq, with chord, motion, midi_control, midi_clock) --- */

/* glue: upstream midi_control.c needs midi_send / midi_uart.c's output; modd does not route MIDI OUT yet */
static void midi_send(uint8_t a, uint8_t b, uint8_t c) { (void)a; (void)b; (void)c; }
static void midi_send_rt(uint8_t msg) { (void)msg; }

#include "upstream/felucca/firmware/src/song_chain.c"
#include "upstream/felucca/firmware/src/seq.c"

static void seq_toggle(void) { if (song.playing) seq_stop(); else seq_start(); }
static void seq_init(void)
{
    uint32_t i;
    song.playing = 0;
    song.tick = 0;
    for (i = 0; i < NTRK; i++) {
        trk[i].seq_pos = 0;
        trk[i].seq_idx = 0;
    }
}
static void scales_init(void) {}
static void seq_pattern_clear(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        __builtin_memset(&t->step[i], 0, sizeof(step_t));
}

/* --- parameter locks (modd-native; shared with page_automation.c) --- */
#define AUTO_NPARAMS 8
static const uint16_t auto_params[AUTO_NPARAMS] = {
    P_FLT, P_RES, P_SHP, P_DIST, P_DLY, P_REV, P_CHOR, P_LEVEL,
};
static uint8_t auto_locks[NTRK][NSTEP][AUTO_NPARAMS];
static uint8_t auto_inited;
static void auto_ensure_init(void)
{
    if (auto_inited) return;
    memset(auto_locks, 0xFF, sizeof auto_locks);
    auto_inited = 1;
}

/* --- track / param defaults (the ISR is in audio/core/audio.c) --- */

static void felucca_tracks_init(void)
{
    uint32_t i, k;
    for (k = 0; k < NTRK; k++) {
        __builtin_memset(&trk[k], 0, sizeof(track_t));
        for (i = 0; i < P_COUNT; i++)
            trk[k].p[i] = TP[i].def;
    }
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
}

static void modd_song_init(void)
{
    static const uint8_t ENG_OF[NTRK] = {0, 12, 3, 2};
    uint32_t k, i;
    song.sel = 1;
    for (k = 0; k < NTRK; k++) {
        uint32_t e = eng_idx(ENG_OF[k]);
        trk[k].eng_req = trk[k].engine = (uint8_t)e;
        for (i = 0; i < 8u; i++)
            trk[k].p[P_E0 + i] = ENGINES[e]->edit[i].def;
    }
#ifdef CONFIG_ENGINE_FM6_FELUCCA
    fm6_init();
#endif
}
