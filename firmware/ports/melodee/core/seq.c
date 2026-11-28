/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Melodee core: sequencer adapter (song_chain, seq, chord, motion, click).
 * NOT YET COMPILABLE — requires CONFIG_AUDIO_MELODEE_STACK_INCLUDED.
 * Target: #include "upstream/melodee/firmware/src/song_chain.c"
 *         #include "upstream/melodee/firmware/src/seq.c"
 * Target origin: keremimo/Melodee firmware/src/{song_chain,seq,chord*,motion,click,
 *         seq_erase,seq_edit,seq_notes,seq_undo,midi_control,midi_clock,
 *         recording,note_history,recording_undo,midi_knobs}.c @ v1.6.1 (bac9c05e)
 *
 * seq.c internally includes: midi_knobs.c, chord.c (which includes
 *   chord_auto.c, chord_melody.c), motion.c, recording.c, note_history.c,
 *   recording_undo.c, click.c, seq_erase.c, midi_control.c, midi_clock.c.
 *
 * Defines: seq_start(), seq_stop(), seq_tick(), seq_toggle(),
 *          chord_*, motion_*, click_render(), scale_count(),
 *          seq_pattern_clear(), seq_init().
 *
 * External deps:
 *   - Melodee core.h types (NTRK=8, NSTEP=64, NPAT=16, NPART)
 *   - ENGINES[], eng_idx(), ENG_DRUM (engine identity)
 *   - NLANE, DRUM_LANE_NOTE[], drum_lane() (drum grid)
 *   - midi_send(), midi_send_rt() (MIDI output)
 *   - trk_note_on/off(), trk_vmode() (voice dispatch)
 *   - micro_scale(), scale_mask() (scale system)
 *   - clamp(), beat_samples(), seq_div_samples(), step_samples() (timing)
 *   - fm1_in (button state for seq_erase)
 *   - settings_click_level (click volume from settings)
 *   - resource_get/release (seq_undo arena)
 *
 * Blockers:
 *   - CONFIG_AUDIO_MELODEE_STACK_INCLUDED not yet implemented
 *   - Melodee's core.h type system (NTRK=8 vs modd's 4)
 *   - scale system integration (micro_scale, scale_mask)
 */
