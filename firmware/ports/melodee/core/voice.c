/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Melodee core: modulation matrix + voice allocator adapter.
 * NOT YET COMPILABLE — requires CONFIG_AUDIO_MELODEE_STACK_INCLUDED.
 * Target: #include "upstream/melodee/firmware/src/mod.c"
 *         #include "upstream/melodee/firmware/src/voice.c"
 * Target origin: keremimo/Melodee firmware/src/{mod,voice}.c @ v1.6.1 (bac9c05e)
 *
 * mod.c defines: mod_begin(), mod_end(), mod_voice(), mod_note(), mod_midi(),
 *                mod_dst_name().
 *
 * voice.c defines: voice_alloc(), voice_start(), trk_note_on/off(),
 *                  track_render(), track_render_audio(), env_tick(),
 *                  engine_block(), mono_play(), trk_all_off(),
 *                  voice_budget(), voices_busy(), spread_*.
 *
 * External deps:
 *   - Melodee core.h types
 *   - ENGINES[], eng_idx(), eng_state_prepare(), eng_state_clear()
 *   - Engine-specific: ENG_CZ, ENG_SID, ENG_DRUM, ENG_P5_TEST,
 *     fm6_join(), sid_chord(), sid_render_into(), p5_block(),
 *     p5_env_source(), p5_patch_of(), phase_env_source()
 *   - performance_options.h, midi_track.c, midi_insert.c
 *   - audio_batch_*, audio_worker_online (dual-core state)
 *   - rng() (random number generator)
 *
 * Blockers:
 *   - CONFIG_AUDIO_MELODEE_STACK_INCLUDED not yet implemented
 *   - 14 DSP name collisions with Felucca-engines dsp.c (mulq15, mulq16,
 *     clamp, osc_sine, osc_tri, blep, softclip, noise32, tsvf_t,
 *     tsvf_coef_k, tsvf_lp, amp_at, voice_amp, soft_knee)
 *   - voice.c hard-codes engine-specific refs (ENG_CZ, sid_chord, etc.)
 *     that need conditional stubs per CONFIG_ENGINE_*
 */
