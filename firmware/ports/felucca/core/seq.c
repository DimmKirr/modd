/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca core: sequencer adapter (song_chain, seq, chord, motion, midi_control, midi_clock).
 * Thin wrapper — the real implementation is in firmware/upstream/felucca/firmware/src/seq.c.
 * Origin: hugelton/Felucca firmware/src/{song_chain,seq,chord,motion,midi_control,midi_clock}.c @ v1.8.2 (6162de86) */

static void midi_send(uint8_t a, uint8_t b, uint8_t c) { (void)a; (void)b; (void)c; }
static void midi_send_rt(uint8_t msg) { (void)msg; }

#include "upstream/felucca/firmware/src/song_chain.c"
#include "upstream/felucca/firmware/src/seq.c"

static void seq_toggle(void) { if (song.playing) seq_stop(); else seq_start(); }
static void seq_init(void)
{
    uint32_t i;
    song.playing = 0;
    song.tick = 0;
    for (i = 0; i < NTRK; i++) {
        trk[i].seq_pos = 0;
        trk[i].seq_idx = 0;
    }
}
static void scales_init(void) {}
static void seq_pattern_clear(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        __builtin_memset(&t->step[i], 0, sizeof(step_t));
}
