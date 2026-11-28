/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: hugelton/Felucca firmware/src/seq.c @ v1.6.1 (a53ecf42)
 *         https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/seq.c */
/* Felucca sequencer, reduced to modd's track_t / step_t.
 * Kept from upstream: one transport, per-track polymeter (LEN / DIV / SWING / GATE), step playback through
 * trk_note_on / trk_note_off, live step recording (keys quantised onto swung steps, overdub into a chord of 4),
 * pattern clear. Left out (modd's track_t has no fields for them): count-in, metronome, song chain, motion
 * automation, step conditions, NUDGE/TIME timing, ARP, chords, external MIDI clock, MIDI IN routing.
 * events_block is called from the effects chain every audio block (fx.c). */

#define SEQ_GATE_DEFAULT 64u               /* P_SGATE 0 (unset): half a step */
#define SEQ_FIRST 0x7FFFFFFFu              /* seq_pos: step 0 fires on the first block */
#ifndef SF_ACCENT
#define SF_ACCENT 1u                       /* step_t.flags bit 0: accent (velocity 127) */
#endif

/* --- parameter locks (shared with page_automation.c which is included later) --- */
#define AUTO_NPARAMS 8
static const uint16_t auto_params[AUTO_NPARAMS] = {
    P_FLT, P_RES, P_SHP, P_DIST, P_DLY, P_REV, P_CHOR, P_LEVEL,
};
static uint8_t auto_locks[NTRK][NSTEP][AUTO_NPARAMS];
static uint8_t auto_inited;
static void auto_ensure_init(void)
{
    if (auto_inited) return;
    memset(auto_locks, 0xFF, sizeof auto_locks);
    auto_inited = 1;
}
static int16_t auto_saved[NTRK][AUTO_NPARAMS];
static uint8_t auto_active[NTRK];

/* per-step extras, packed into step_t's spare fields (no UI edits them yet; zero = off, so old patterns play as before):
 *   step_t.hit    CHANCE, 1..100 percent (0 = always)
 *   step_t.flags  bits 1..2: RATCH, extra hits (0..3, so 1..4 hits per step)
 *   step_t.len    NUDGE, signed, 1/128 of a step late (negative is played on the grid: no look-ahead)
 *   step_t.vel    VEL, 1..127 (0 = 96) */
#define SF_RATCH_SHIFT 1u
#define SF_RATCH_MASK  3u

/* what sounds per track (the notes a step started) and when its gate ends; kept out of track_t */
static uint8_t seq_notes[NTRK][4], seq_nn[NTRK];
static uint32_t seq_off[NTRK];
static uint32_t seq_rnd = 0x2545F491u;
/* a step waiting for its nudge, or ratcheting: its notes, velocity, the delay and the gap between hits (samples) */
static step_t seq_pstep[NTRK];
static uint8_t seq_pvel[NTRK], seq_rleft[NTRK];
static uint32_t seq_dly[NTRK], seq_rgap[NTRK], seq_rnext[NTRK], seq_pgate[NTRK];
static volatile uint8_t transport_req;     /* 1 start, 2 stop, 3 restart (from the UI) */

static uint32_t seq_trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static void auto_lock_apply(track_t *t, uint32_t step_idx)
{
    uint32_t i = seq_trk_index(t), k;
    auto_ensure_init();
    if (step_idx >= NSTEP) return;
    for (k = 0; k < AUTO_NPARAMS; k++) {
        auto_saved[i][k] = modd_tp(t, auto_params[k]);
        if (auto_locks[i][step_idx][k] != 0xFF)
            modd_tp_set(t, auto_params[k], (int16_t)auto_locks[i][step_idx][k]);
    }
    auto_active[i] = 1;
}

static void auto_lock_restore(track_t *t)
{
    uint32_t i = seq_trk_index(t), k;
    if (!auto_active[i]) return;
    for (k = 0; k < AUTO_NPARAMS; k++)
        modd_tp_set(t, auto_params[k], auto_saved[i][k]);
    auto_active[i] = 0;
}

static uint32_t seq_len(const track_t *t)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 16u;
    return len > NSTEP ? NSTEP : len;
}

/* the length of step idx in samples: SWING (the track's + the global, at most 100) makes the even steps
 * longer and the odd ones shorter, so every odd step starts late */
static uint32_t seq_step_len(const track_t *t, uint32_t period, uint32_t idx)
{
    int32_t sw = (int32_t)t->p[P_SSWING] + (int32_t)song.g[G_SWING];
    int32_t d;
    if (sw > 100) sw = 100;
    if (sw < 0) sw = 0;
    d = sw * (int32_t)period / 200;
    return (uint32_t)((int32_t)period + ((idx & 1u) ? -d : d));
}

static void seq_release(track_t *t)
{
    uint32_t i = seq_trk_index(t), k;
    for (k = 0; k < seq_nn[i]; k++)
        trk_note_off(t, seq_notes[i][k]);
    seq_nn[i] = 0;
    auto_lock_restore(t);
}

static uint32_t seq_random(void)
{
    seq_rnd ^= seq_rnd << 13;
    seq_rnd ^= seq_rnd >> 17;
    seq_rnd ^= seq_rnd << 5;
    return seq_rnd;
}

/* sound one hit of the pending step: (re)trigger its notes at the velocity, gate = share of the hit's length */
static void seq_hit(track_t *t)
{
    uint32_t i = seq_trk_index(t), k;
    const step_t *s = &seq_pstep[i];
    seq_release(t);
    for (k = 0; k < 4u && s->note[k]; k++) {
        trk_note_on(t, s->note[k], seq_pvel[i]);
        seq_notes[i][seq_nn[i]++] = s->note[k];
    }
    seq_off[i] = seq_pgate[i];
}

/* play one step: a NOTE step (re)triggers its notes at its velocity (accent 127); an empty step releases.
 * CHANCE may skip it, NUDGE delays it, RATCH repeats it */
static void seq_step(track_t *t, const step_t *s, uint32_t period)
{
    uint32_t i = seq_trk_index(t);
    uint32_t gate_p = t->p[P_SGATE] > 0 ? (uint32_t)t->p[P_SGATE] : SEQ_GATE_DEFAULT;
    uint32_t ratch = 1u + ((s->flags >> SF_RATCH_SHIFT) & SF_RATCH_MASK);
    int32_t nudge = modd_step_nudge(s);
    seq_dly[i] = 0;
    seq_rleft[i] = 0;
    seq_release(t);
    if (!s->note[0])
        return;
    if (s->hit && s->hit < 100u && seq_random() % 100u >= s->hit)
        return;                                    /* the chance failed: the step stays silent */
    auto_lock_apply(t, t->seq_idx);
    seq_pstep[i] = *s;
    seq_pvel[i] = (uint8_t)((s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u));
    seq_rgap[i] = period / ratch;
    seq_pgate[i] = seq_rgap[i] * gate_p / 128u;
    seq_rleft[i] = (uint8_t)(ratch - 1u);
    seq_rnext[i] = seq_rgap[i];
    if (nudge > 0) {
        seq_dly[i] = (uint32_t)nudge * period / 128u;
        if (!seq_dly[i])
            seq_hit(t);
        return;
    }
    seq_hit(t);
}

/* the delayed first hit and the ratchet's later hits, counted in samples */
static void seq_timers(track_t *t, uint32_t n)
{
    uint32_t i = seq_trk_index(t);
    if (seq_dly[i]) {
        if (seq_dly[i] > n) {
            seq_dly[i] -= n;
            return;
        }
        seq_dly[i] = 0;
        seq_hit(t);
        return;
    }
    if (seq_rleft[i]) {
        if (seq_rnext[i] > n) {
            seq_rnext[i] -= n;
            return;
        }
        seq_rnext[i] = seq_rgap[i];
        seq_rleft[i]--;
        seq_hit(t);
    }
}

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t i = seq_trk_index(t), period, len;
    seq_timers(t, n);
    if (seq_nn[i]) {                               /* the gate counts down in real time, stopped too */
        if (seq_off[i] <= n)
            seq_release(t);
        else
            seq_off[i] -= n;
    }
    if (!song.playing)
        return;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    len = seq_len(t);
    t->seq_pos += n;
    for (;;) {
        uint32_t cur = seq_step_len(t, period, t->seq_idx);
        if (t->seq_pos < cur && t->seq_pos != SEQ_FIRST + n)
            break;
        t->seq_pos = t->seq_pos >= SEQ_FIRST ? 0u : t->seq_pos - cur;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % len);
        seq_step(t, &t->step[t->seq_idx], period);
    }
}

static void seq_start(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(seq_len(t) - 1u);
        t->seq_pos = SEQ_FIRST;
        seq_nn[i] = 0;
        seq_dly[i] = 0;
        seq_rleft[i] = 0;
    }
    song.tick = 0;
    song.playing = 1;
}

static void seq_stop(void)
{
    uint32_t i;
    song.playing = 0;
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        auto_lock_restore(&trk[i]);
    }
}

static void seq_toggle(void)
{
    if (song.playing) seq_stop(); else seq_start();
}

static void seq_pattern_clear(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        __builtin_memset(&t->step[i], 0, sizeof(step_t));
}

/* live recording: the note goes into the nearest step, as swung (the one playing, or the next one when
 * the sequencer is past the middle of the playing one). Overdub: a step that holds notes gets this one
 * added (a chord of up to 4; when full, the last note is replaced). Only while the transport runs and the
 * track is armed (song.rec bit) */
static void seq_rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len, idx, period, cur, k;
    step_t *s;
    if (!((song.rec >> seq_trk_index(t)) & 1u) || !song.playing)
        return;
    len = seq_len(t);
    idx = t->seq_idx % len;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    cur = seq_step_len(t, period, t->seq_idx);
    if (t->seq_pos != SEQ_FIRST && t->seq_pos > cur / 2u)
        idx = (idx + 1u) % len;
    s = &t->step[idx];
    for (k = 0; k < 4u && s->note[k] && s->note[k] != note; k++)
        ;
    if (k == 4u)
        k = 3u;
    s->note[k] = (uint8_t)note;
    if (vel > 110u)
        s->flags |= SF_ACCENT;
    if (vel > 127u)
        vel = 127u;
    if (vel > s->vel || !s->note[1])               /* a first note sets the velocity, a chord keeps the loudest */
        s->vel = (uint8_t)vel;
}

/* pattern storage: a track's steps to / from a caller's buffer (the storage layer owns where it lives).
 * Both return the bytes used, or 0 when the buffer is too small (load leaves the track untouched) */
static uint32_t seq_pattern_save(const track_t *t, uint8_t *buf, uint32_t cap)
{
    if (cap < sizeof t->step)
        return 0;
    __builtin_memcpy(buf, t->step, sizeof t->step);
    return (uint32_t)sizeof t->step;
}

static uint32_t seq_pattern_load(track_t *t, const uint8_t *buf, uint32_t len)
{
    if (len < sizeof t->step)
        return 0;
    __builtin_memcpy(t->step, buf, sizeof t->step);
    return (uint32_t)sizeof t->step;
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i;
    if (transport_req == 1u)
        seq_start();
    else if (transport_req == 2u)
        seq_stop();
    else if (transport_req == 3u) {
        seq_stop();
        seq_start();
    }
    transport_req = 0;
    for (i = 0; i < NTRK; i++) {
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
        engine_block(&trk[i]);      /* the stack's engine switch (voice.c), called here as upstream's events_block does */
#endif
        seq_tick(&trk[i], n);
    }
    if (song.playing)
        song.tick++;
}

static void seq_init(void)
{
    uint32_t i;
    song.playing = 0;
    song.tick = 0;
    transport_req = 0;
    for (i = 0; i < NTRK; i++) {
        seq_nn[i] = 0;
        seq_off[i] = 0;
        seq_dly[i] = 0;
        seq_rleft[i] = 0;
        trk[i].seq_pos = 0;
        trk[i].seq_idx = 0;
    }
}
