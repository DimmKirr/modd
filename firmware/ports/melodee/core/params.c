/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Melodee core: parameter tables and descriptors adapter.
 * NOT YET COMPILABLE — requires CONFIG_AUDIO_MELODEE_STACK_INCLUDED.
 * Target: #include "upstream/melodee/firmware/src/params.c"
 * Target origin: keremimo/Melodee firmware/src/params.c @ v1.6.1 (bac9c05e)
 *
 * Defines: TP[P_COUNT], GP[G_COUNT], PAGES[], param_format(),
 *          track_desc(), param_desc_of(), enum_step(), fx_step/rank/family(),
 *          fm6_page_desc(), fx_rack_desc(), N_* name lists.
 *
 * External deps (must be available before this file):
 *   - Melodee core.h: P_*, G_*, FP_*, FN_*, FM6_* enums, track_t, song_t,
 *     param_desc_t, NTRK, NSTEP, NVOICE, NELEM
 *   - dsp.c: clamp()
 *   - ENGINES[], eng_idx() (from stubs.c or engine registry)
 *   - cz_edit.h, prophet_edit.h, recording_preferences.h, midi_knobs.h
 *   - scale_count() (forward-declared from seq.c)
 *   - fmt_int(), fmt_fix(), str_cpy(), str_len() (string helpers)
 *
 * Blockers:
 *   - CONFIG_AUDIO_MELODEE_STACK_INCLUDED not yet implemented
 *   - Melodee's P_* enum differs from modd's superset enum
 *   - Melodee's param_desc_t may differ from modd's
 */
