/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/seq.c @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/seq.c */
/* Melodee sequencer stub: 64-step, 8 banks, 1024 timed notes,
 * recording, undo, motion, song chain.
 * TODO: port from upstream melodee (seq.c, seq_edit.c, seq_notes.c,
 * seq_erase.c, seq_undo.c, song_chain.c, pattern.c, pattern_store.c,
 * recording.c, recording_undo.c, motion.c, click.c, chord.c, tracks.c). */

#define KB_SILENT 255u
static volatile uint8_t kb_asleep;

static void seq_init(void) {}

static void seq_tick(void)
{
    if (song.playing) {
        song.tick++;
        if (song.tick >= song.len * CTL)
            song.tick = 0;
    }
}

static void seq_start(void)
{
    song.playing = 1;
    song.tick = 0;
}

static void seq_stop(void)
{
    song.playing = 0;
    song.tick = 0;
}

static void seq_toggle(void)
{
    if (song.playing)
        seq_stop();
    else
        seq_start();
}

static uint32_t seq_step_pos(void)
{
    return song.tick / CTL;
}

static int seq_armed(void) { return song.rec; }

static void seq_arm(int on) { song.rec = (uint8_t)on; }

static void seq_events_block(void) {}

static void seq_pattern_clear(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        __builtin_memset(&t->step[i], 0, sizeof(step_t));
}
