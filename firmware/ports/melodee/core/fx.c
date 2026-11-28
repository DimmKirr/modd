/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Melodee core: effects + mixer adapter.
 * NOT YET COMPILABLE — requires CONFIG_AUDIO_MELODEE_STACK_INCLUDED.
 * Target: #include "upstream/melodee/firmware/src/mix.c"
 *         (mix.c internally includes fx.c)
 * Target origin: keremimo/Melodee firmware/src/{mix,fx}.c @ v1.6.1 (bac9c05e)
 *
 * mix.c defines: mix_block() — the stereo mixer called by audio.c ISR.
 *   It #includes fx.c at line 7, so fx.c must NOT be included separately.
 *
 * fx.c defines: fx_init(), fx_process(), EQ, reverb, delay, chorus,
 *   distortion — Melodee's core-integrated effects chain.
 *   Uses performance_options.h and fm1_eq.h (hardware EQ).
 *
 * External deps:
 *   - Melodee core.h types (FX_SLOTS, FX_STATE_COUNT, G_MFXTONE)
 *   - resource_get(), resource_release(), resource[] (Melodee arena)
 *   - performance_options.h
 *   - fm1_eq_run() (HAL: hardware EQ DSP)
 *   - fm1_ticks() (HAL: timer)
 *
 * Blockers:
 *   - CONFIG_AUDIO_MELODEE_STACK_INCLUDED not yet implemented
 *   - Melodee resource/arena system not yet ported
 *   - mix.c transitively includes fx.c — single include point needed
 */
