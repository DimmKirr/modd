// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Wrapper: Optimist's DSP file (firmware/src/dsp/dsp.c, with dsp_common.h) INCLUDED unchanged, for the Optimist
 * units of the effects host (glide_next of exgain.c, today). modd's engines include Felucca-Engines' dsp.c, which
 * defines 17 of the same names with other bodies; Optimist's file cannot go in beside it as it is. Names only:
 * every name both files define is renamed opt_<name> by a macro for the span of the include, and the macros are
 * removed after it, so the rest of modd (engines, units) still sees the Felucca-Engines versions. The Optimist-only
 * names (glide_next, GLIDE_K, ...) keep their names for the units that call them (exgain.c).
 * Supplied for the file: AINL (Optimist core.h), the SIMD switches off (hal/fm1_simd.h then defines only the switches).
 * Needs from modd: CTL 32, FS, the tables (SINE, TANH_Q15, SVF_G, PITCH_OCT, pitch_inc) and vmod_t, which
 * CONFIG_ENGINES has included before this file. Unused AINL functions and the unreferenced tables are dropped
 * by the compiler and the linker. VOICE_FS (24000) and CTL_LOG2 (5) are defined by both files with the same
 * tokens, which C allows, so they need no rename. */
#define mulq15 opt_mulq15
#define mulq16 opt_mulq16
#define clamp opt_clamp
#define softclip opt_softclip
#define soft_knee opt_soft_knee
#define blep opt_blep
#define osc_sine opt_osc_sine
#define osc_saw opt_osc_saw
#define osc_pulse opt_osc_pulse
#define osc_tri opt_osc_tri
#define sine_i opt_sine_i
#define noise32 opt_noise32
#define tsvf_t opt_tsvf_t
#define tsvf_coef opt_tsvf_coef
#define tsvf_coef_k opt_tsvf_coef_k
#define tsvf_lp opt_tsvf_lp
#define amp_at opt_amp_at
#define AINL static inline __attribute__((always_inline))   /* Optimist core.h:293 */
#include "upstream/optimist/firmware/src/dsp/dsp.c"
#undef AINL
#undef mulq15
#undef mulq16
#undef clamp
#undef softclip
#undef soft_knee
#undef blep
#undef osc_sine
#undef osc_saw
#undef osc_pulse
#undef osc_tri
#undef sine_i
#undef noise32
#undef tsvf_t
#undef tsvf_coef
#undef tsvf_coef_k
#undef tsvf_lp
#undef amp_at
