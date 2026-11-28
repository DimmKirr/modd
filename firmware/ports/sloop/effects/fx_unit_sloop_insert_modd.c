// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Wrapper: Melodee's SLOOP insert unit (firmware/src/fx_sloop_insert.h, SLOOP origin) as an fx_unit_t.
 * The upstream header is included unchanged. This file supplies only what the header expects from its
 * includer: the parameter record it reads, the type ids, the dry-copy arrays, one arithmetic helper,
 * and the five contract functions. */

/* The unit's parameter interface: the only fields of Melodee's fx_state_t that the header reads. */
typedef struct {
    uint8_t type, mono;
    int16_t rate, character;
    int32_t echo, mix;
} fx_state_t;

/* The type ids the unit indexes by (Melodee numbers them elsewhere; here they run 0..7 in the unit's order). */
enum { FX_SOFT, FX_HARD, FX_FOLD, FX_FUZZ, FX_CRUSH, FX_FLANGER, FX_PEDAL_PHASER, FX_PEDAL_CHORUS, FX_SLOOP_N };

static int32_t fx_dry_l[CTL], fx_dry_r[CTL];                 /* the unit mixes against a dry copy of the block */

/* a * k / 65536, rounded down, without overflowing 32 bits (a up to +-2^19, k up to 2^16) */
static inline int32_t fx_scale(int32_t a, uint32_t k) { return (int32_t)(((int64_t)a * (int32_t)k) >> 16); }

#include "upstream/melodee/firmware/src/fx_sloop_insert.h"

/* One slot's state: line-less types keep the unit's state here (bytes() = 0); line types keep it at the
 * start of the borrowed block, with the delay lines right behind it, which is the layout the unit assumes. */
typedef struct {
    fx_state_t f;
    int32_t target;
    fx_sloop_insert_t local;
} sloop_slot_t;
_Static_assert(sizeof(sloop_slot_t) <= FX_STATE_BYTES, "sloop insert state fits the host's slot state");

static const char *sloop_type_name(uint32_t t)
{
    static const char *const n[FX_SLOOP_N] = {"SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "FLANGER", "PHASER", "CHORUS"};
    return t < FX_SLOOP_N ? n[t] : "?";
}

static uint32_t sloop_bytes(uint32_t t)
{
    return (t == FX_FLANGER || t == FX_PEDAL_CHORUS) ? fx_sloop_insert_bytes(t) : 0u;
}

static void sloop_prepare(void *state, void *mem, uint32_t t, int32_t amount)
{
    sloop_slot_t *w = (sloop_slot_t *)state;
    (void)mem;
    memset(w, 0, sizeof *w);
    w->f.type = (uint8_t)(FX_SOFT + t);
    w->f.mono = 0;
    w->f.echo = 1;                 /* the input is always live: the unit never retires its state */
    w->f.rate = 64;
    w->f.character = 64;
    w->f.mix = 0;                  /* the unit ramps the wet level up to target */
    w->target = clamp(amount, 0, 127) * 258;   /* 127 -> 32766, full wet is 32768 */
}

static void sloop_run(void *state, void *mem, int32_t *l, int32_t *r, uint32_t n)
{
    sloop_slot_t *w = (sloop_slot_t *)state;
    fx_sloop_insert_t *s = mem ? (fx_sloop_insert_t *)mem : &w->local;
    uint32_t i, k, m;
    for (i = 0; i < n; i += m) {
        m = n - i < CTL ? n - i : CTL;
        for (k = 0; k < m; k++) { fx_dry_l[k] = l[i + k]; fx_dry_r[k] = r[i + k]; }
        fx_sloop_insert_run(&w->f, s, m, w->target, l + i, r + i, 32, 0);
    }
}

static void sloop_release(void *state) { ((sloop_slot_t *)state)->target = 0; }

static int sloop_quiet(void *state, void *mem)
{
    sloop_slot_t *w = (sloop_slot_t *)state;
    (void)mem;
    return w->target == 0 && w->f.mix == 0;
}

static const fx_unit_t FX_UNIT_SLOOP_INSERT = {
    .name = "SLOOP_INSERT",
    .ntypes = FX_SLOOP_N,
    .type_name = sloop_type_name,
    .bytes = sloop_bytes,
    .prepare = sloop_prepare,
    .run = sloop_run,
    .release = sloop_release,
    .quiet = sloop_quiet,
};
