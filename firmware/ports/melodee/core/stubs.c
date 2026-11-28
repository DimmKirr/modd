/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Melodee core: shared glue stubs for absent engines and upstream dependencies.
 * NOT YET COMPILABLE — requires CONFIG_AUDIO_MELODEE_STACK_INCLUDED.
 * Target origin: keremimo/Melodee firmware/src/ @ v1.6.1 (bac9c05e)
 *
 * Target stubs (symbols Melodee's core files expect from outside):
 *
 * Engine identity:
 *   - eng_idx(preset) — map a preset number to an ENGINES[] index
 *   - ENG_CZ, ENG_SID, ENG_DRUM, ENG_P5_TEST — engine ID constants
 *
 * Drum grid:
 *   - drum_reuse(trk, voice) — check if a drum voice can be reused
 *   - drum_lane(note) — map a note to a drum lane
 *   - NLANE, DRUM_LANE_NOTE[] — drum lane count and note table
 *
 * MIDI output:
 *   - midi_send(status, data1, data2) — send a MIDI message
 *   - midi_send_rt(msg) — send a MIDI real-time message
 *
 * Memory arena (Melodee resource system):
 *   - resource_get(id) — allocate a resource slot
 *   - resource_release(id) — free a resource slot
 *   - resource[] — resource state array
 *   - RES_* constants
 *
 * Engine-specific callbacks (voice.c hard-codes these):
 *   - fm6_join(voice) — FM6 voice join
 *   - sid_chord(trk, note) — SID chord helper
 *   - sid_render_into(buf, n) — SID block render
 *   - p5_block(trk) — Prophet-5 block processing
 *   - p5_env_source(trk) — Prophet-5 envelope source
 *   - p5_patch_of(trk) — Prophet-5 patch accessor
 *   - phase_env_source(trk) — Phase engine envelope source
 *   - micro_active(trk) — microtonal scale active check
 *   - micro_pitch(note) — microtonal pitch lookup
 *
 * Blockers:
 *   - CONFIG_AUDIO_MELODEE_STACK_INCLUDED not yet implemented
 *   - Melodee core.h type system (track_t, P_*, NTRK=8) differs from modd's
 */
