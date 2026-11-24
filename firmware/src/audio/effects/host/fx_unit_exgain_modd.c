// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Wrapper: Optimist's EXGN insert (firmware/src/fx/insfx/exgain.c, a gain in dB) as an fx_unit_t.
 * The upstream file is included unchanged. This file supplies only what the file expects from its includer:
 * the build switch, the instance count, the RAM-code marker and the contract functions. glide_next (and GLIDE_K) come from
 * Optimist's dsp/dsp.c, INCLUDED unchanged by dsp_optimist_modd.c (CONFIG_DSP_OPTIMIST, selected by this unit).
 * LEVEL_Q12 (the gain curve) comes from the generated tables header, CTL_LOG2 from the DSP files. */
#define FELUCCA_EX_GAIN 1
#define IFX_NI 2u                         /* the instances the unit's state array holds: here the left and the right side */
#ifndef HOT2
#define HOT2                              /* no RAM-code section in modd */
#endif

#include "upstream/optimist/firmware/src/fx/insfx/exgain.c"

typedef struct { int32_t amount; } exgn_slot_t;   /* the amount the unit runs at; release sets it to 0 (OFF: glide back to 0 dB) */
_Static_assert(sizeof(exgn_slot_t) <= FX_STATE_BYTES, "exgain state fits the host's slot state");

static const char *exgn_type_name(uint32_t t) { return t == 0 ? "GAIN" : "?"; }
static uint32_t exgn_bytes(uint32_t t) { (void)t; return 0u; }

static void exgn_prepare(void *state, void *mem, uint32_t t, int32_t amount)
{
    (void)mem; (void)t;
    ifx_exgn[0].g = 0;                    /* at rest */
    ifx_exgn[1].g = 0;
    ((exgn_slot_t *)state)->amount = amount;
}

static void exgn_run(void *state, void *mem, int32_t *l, int32_t *r, uint32_t n)
{
    int32_t a = ((exgn_slot_t *)state)->amount;
    uint32_t i, m;
    (void)mem;
    for (i = 0; i < n; i += m) {          /* the unit's glide is a step a block of CTL samples */
        m = n - i < CTL ? n - i : CTL;
        ifx_exgn_run(&ifx_exgn[0], a, l + i, m);
        ifx_exgn_run(&ifx_exgn[1], a, r + i, m);
    }
}

static void exgn_release(void *state) { ((exgn_slot_t *)state)->amount = 0; }

static int exgn_quiet(void *state, void *mem)
{
    (void)state; (void)mem;
    return !ifx_exgn[0].g && !ifx_exgn[1].g;
}

static const fx_unit_t FX_UNIT_EXGAIN = {
    .name = "OPT_EXGAIN",
    .ntypes = 1,
    .type_name = exgn_type_name,
    .bytes = exgn_bytes,
    .prepare = exgn_prepare,
    .run = exgn_run,
    .release = exgn_release,
    .quiet = exgn_quiet,
};
