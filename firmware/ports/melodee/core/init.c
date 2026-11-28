/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Melodee core: track init, song defaults, parameter locks.
 * NOT YET COMPILABLE — requires CONFIG_AUDIO_MELODEE_STACK_INCLUDED.
 * Target origin: keremimo/Melodee firmware/src/ @ v1.6.1 (bac9c05e)
 *
 * Will define: melodee_tracks_init(), modd_song_init().
 * Initializes 8 tracks (NTRK=8: 4 synth + 4 MIDI-only),
 * sets up song defaults, assigns default engines per track.
 *
 * External deps:
 *   - Melodee core.h types (track_t with NTRK=8, song_t, P_COUNT)
 *   - TP[], GP[] (from params.c)
 *   - ENGINES[], eng_idx() (engine registry)
 *   - fm6_init() (if CONFIG_ENGINE_FM6_MELODEE)
 *
 * Blockers:
 *   - CONFIG_AUDIO_MELODEE_STACK_INCLUDED not yet implemented
 *   - Default engine assignments need CONFIG_ENGINE_* awareness
 */
