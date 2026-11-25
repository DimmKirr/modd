/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Wrapper for the upstream Felucca audio stack (CONFIG_AUDIO_FELUCCA_STACK_INCLUDED).
 * Origin: hugelton/Felucca firmware/src/{params,mod,voice,slicer,fx,perform,click}.c @ v1.8.2 (6162de86)
 * The upstream files are #included unchanged from firmware/upstream/felucca/firmware/src/; this file and
 * firmware/adapters/modd_audio.h hold the adapter glue. The audio ISR is in audio/core/audio.c (the
 * universal host); this file provides mix_block and felucca_tracks_init for it. */

/* --- glue: what upstream's engines.c / eng_drum.c / eng_fm6.c / seq.c give the stack, for the engines this
 * build does not have (ENG_DRUM and the FM6 patch list are absent engines in engines.c). Names and constants only. --- */
enum { DK_COUNT = 1 };
static const uint8_t DK_PLAYS[DK_COUNT] = {0};
static const char *const N_DRUM_KIT[DK_COUNT] = {"-"};
static const char *const N_FM6_PATCH[1] = {"-"};
#define FM6_OWN 0
static int fm6_slot_ok(int32_t s) { (void)s; return 1; }
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }   /* engines.c: a bad engine number is 0 */
static void eng_mem_claim(const track_t *t) { (void)t; }   /* engines.c: zeroing the part's engine memory on a switch; modd's registry has never done it */
static voice_t *drum_reuse(track_t *t, uint32_t note) { (void)t; (void)note; return 0; }   /* eng_drum.c: no DRUM engine */
static uint32_t kb_out_tick;                   /* seq.c: when the block now rendering plays (audio.c writes it) */

/* Buffer sizes from Kconfig, through patches/felucca/0001 (#ifndef around upstream's three #defines; the
 * patched copies are first on the include path). Unsigned constant expressions, as upstream's 65536u. */
#define DLY_LEN (CONFIG_FELUCCA_DLY_LEN + 0u)
#define CHO_LEN (CONFIG_FELUCCA_CHO_LEN + 0u)
#define SL_LEN (CONFIG_FELUCCA_SL_LEN + 0u)
/* upstream masks every index with LEN - 1u */
_Static_assert((DLY_LEN & (DLY_LEN - 1u)) == 0u, "CONFIG_FELUCCA_DLY_LEN must be a power of two");
_Static_assert((CHO_LEN & (CHO_LEN - 1u)) == 0u, "CONFIG_FELUCCA_CHO_LEN must be a power of two");
_Static_assert((SL_LEN & (SL_LEN - 1u)) == 0u, "CONFIG_FELUCCA_SL_LEN must be a power of two");

#include "upstream/felucca/firmware/src/params.c"
/* modd_tp_set (modd_audio.h): every value a page or the API writes goes through upstream's param_fit, with the
 * descriptor upstream uses for this track's engine (the P_E0..P_E7 ranges are the engine's). */
static int16_t modd_tp_fit(const track_t *t, uint32_t id, int32_t v)
{
    return (int16_t)param_fit(param_desc_of(eng_idx(t->eng_req), id), v);
}
#include "upstream/felucca/firmware/src/mod.c"
#include "upstream/felucca/firmware/src/voice.c"
static void mod_init(void) {}                  /* modd.c boot: upstream's matrix state is zero-initialised */
#include "upstream/felucca/firmware/src/slicer.c"
#include "upstream/felucca/firmware/src/fx.c"
/* perform.c (included by fx.c) derives PB_FRAMES = NTRK * SL_LEN / 2 and masks with PB_FRAMES - 1u; its OCT UP / DN
 * delay masks with HB_MASK (4095) inside the same buffer */
_Static_assert((PB_FRAMES & (PB_FRAMES - 1u)) == 0u, "PB_FRAMES (NTRK * SL_LEN / 2) must be a power of two");
_Static_assert(PB_FREC <= PB_FRAMES, "CONFIG_FELUCCA_SL_LEN is too small for FREEZE (PB_FREC frames)");
_Static_assert(HB_MASK < PB_FRAMES, "CONFIG_FELUCCA_SL_LEN is too small for the OCT UP / DN delay (HB_MASK)");

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

/* power-on sounds, as upstream's TRK_DEF (engines.c): ANALOG, FM6, LOFI and a fourth part (DRUM is absent here: PHASE) */
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
}
