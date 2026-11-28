/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Adapted from MIDI control contribution by ChanceTheMaker (2026).
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/{midi_control,midi_clock}.c @ v1.4.5 (398ada1e)
 * Class: HOSTED — upstream files #include'd unchanged; stubs below provide
 *        the seq.c functions they call back into. The stubs become real when
 *        the Melodee core (seq.c, chord.c, motion.c) is ported.
 * Stubs: trk_index, drum_track, trk_n, voice_kill, input_on, input_off,
 *        key_off, midi_map, chord_build, chord_strum_begin, midi_chord_tone,
 *        mchord_of, mchord_held, mchord_forget, midi_note_track,
 *        midi_track_only, midi_track_accept, midi_track_controller,
 *        midi_knob_receive, mlearn_receive, mod_midi, motion_capture,
 *        motion_guard, motion_unguard, motion_native_tag, motion_native_peek,
 *        motion_native_edited, motion_begin, step_midi_edge, p5_patch_of,
 *        p5_edit_value, fm6_patch_of, fm6_put_patch, cz_patch_of, cz_ed_put,
 *        midi_sync_emit, midi_sync_send, beat_samples, chain_div_samples */

/* ---- constants the upstream files expect ---- */

#define MELODEE_BLE 0
#ifndef FS
#define FS 44100u
#endif
#define MIDI_BEAT_UNITS 24576u
#ifndef MIDI_TICKS_PER_MS
#define MIDI_TICKS_PER_MS 1u
#endif
#define KB_SILENT 255u
#define MIDI_TRACK_FOLLOW 0x80u
#define CHORD_MAX 4u
#define MCHORD_N 24u
#define MO_NATIVE_N 144u
#define ENGI_FM6 12u
#define ENGI_CZ 15u
#define ENGI_PROPHET 19u
#define FP_SIZE 155
#define FP_OP 21
#define CZ_BYTES 144u
#define P5_BEND 86
#define Q_MPC 4u

/* ---- types the upstream files expect from seq.c / chord.c ---- */

typedef struct { uint32_t pos, beat; uint8_t total, rate, vel; } chord_strum_t;
typedef struct { uint8_t id, ch, src, n, note[CHORD_MAX]; chord_strum_t strum; } mchord_t;

typedef struct { uint8_t raw[CZ_BYTES]; } midi_cz_patch_stub_t;
typedef struct { uint8_t raw[FP_SIZE + 1u]; } midi_p5_patch_stub_t;

static const uint8_t CZ_ENV_END[2][3] = {{54,37,20},{111,94,77}};
#define LCZ_GLOBALS 0u
#define LCZ_EBASE(l,e) (LCZ_GLOBALS+16u+((l)*3u+(e))*18u)

/* ---- globals the upstream files read/write ---- */

static mchord_t mchord[MCHORD_N];
static uint32_t midi_strum_pending;
static uint8_t kb_chn[27], kb_trk[27];
static uint8_t kb_chord[27][CHORD_MAX];
static uint32_t live_held[NTRK][4];
static uint32_t midi_event_source;
static uint8_t midi_hint;
static int32_t midi_bend_q8[NTRK];
static int32_t midi_bend_target[NTRK];
static uint32_t midi_beat_samples;
static uint8_t midi_sync_active;
static uint32_t seq_started;
static uint64_t seq_seek_units;
static uint32_t cin_left;
static uint32_t beat_pos, beat_n;

static struct {
    uint32_t running;
    uint64_t start_units;
} chain;

static const param_desc_t TP[P_COUNT];

/* ---- HOST stubs: seq.c / chord.c / motion.c functions ---- */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static uint32_t trk_n(void) { return NTRK; }
static track_t *midi_track(uint32_t ch) { return &trk[ch % NTRK]; }
static int drum_track(const track_t *t) { (void)t; return 0; }
static void voice_kill(voice_t *v) { v->active = 0; }
static void input_on(track_t *t, uint32_t note, uint32_t vel) { (void)t; (void)note; (void)vel; }
static void input_off(track_t *t, uint32_t note) { (void)t; (void)note; }
static void key_off(uint32_t k, track_t *t) { (void)k; (void)t; }
static uint32_t midi_map(track_t *t, uint32_t note) { (void)t; return note; }
static uint32_t chord_build(track_t *t, uint32_t note, uint8_t *nn) { (void)t; nn[0] = (uint8_t)note; return 1; }
static uint32_t chord_strum_begin(track_t *t, uint8_t *notes, uint32_t n, chord_strum_t *s, uint32_t vel)
{ (void)t; (void)notes; (void)s; (void)vel; return n; }
static void midi_chord_tone(mchord_t *m) { (void)m; }
static mchord_t *mchord_of(uint32_t ch, uint32_t note, uint32_t id) { (void)ch; (void)note; (void)id; return 0; }
static int mchord_held(uint32_t id, uint32_t note) { (void)id; (void)note; return 0; }
static void mchord_forget(uint32_t track) { (void)track; }

static track_t *midi_note_track(uint32_t ch, uint32_t note) { (void)note; return midi_track(ch); }
static int midi_track_only(const track_t *t) { (void)t; return 0; }
static int midi_track_accept(const track_t *t) { (void)t; return 1; }
static void midi_track_controller(track_t *t, uint32_t st, uint32_t d1, uint32_t d2)
{ (void)t; (void)st; (void)d1; (void)d2; }
static int mlearn_receive(uint32_t cc, uint32_t val) { (void)cc; (void)val; return 0; }
static int midi_knob_receive(uint32_t ch, uint32_t cc, uint32_t val) { (void)ch; (void)cc; (void)val; return 0; }
static void mod_midi(track_t *t, uint32_t st, uint32_t d1, uint32_t d2)
{ (void)t; (void)st; (void)d1; (void)d2; }
static void step_midi_edge(uint32_t track, uint32_t note, uint32_t on, uint32_t ch, uint32_t src)
{ (void)track; (void)note; (void)on; (void)ch; (void)src; }

/* ---- HOST stubs: motion ---- */

static void motion_capture(track_t *t, uint32_t id, int32_t v) { (void)t; (void)id; (void)v; }
static uint32_t motion_guard(void) { return 0; }
static void motion_unguard(uint32_t f) { (void)f; }
static int motion_native_tag(const track_t *t) { (void)t; return 0; }
static void motion_native_peek(const track_t *t, uint8_t *old) { (void)t; (void)old; }
static void motion_native_edited(const track_t *t, const uint8_t *old) { (void)t; (void)old; }
static void motion_begin(void) {}

/* ---- HOST stubs: engine-specific patch editing (dead code — motion_native_tag returns 0) ---- */

static midi_p5_patch_stub_t midi_p5_stub;
static midi_p5_patch_stub_t *p5_patch_of(const track_t *t) { (void)t; return &midi_p5_stub; }
static void p5_edit_value(track_t *t, uint32_t param, uint32_t value) { (void)t; (void)param; (void)value; }
#define P5_RELEASE_AMP 0u
#define P5_ATTACK_AMP 0u
#define P5_DECAY_AMP 0u

static uint8_t midi_fm6_stub[FP_SIZE + 1u];
static uint8_t *fm6_patch_of(uint32_t k) { (void)k; return midi_fm6_stub; }
static void fm6_put_patch(uint32_t k, const uint8_t *raw, uint32_t flag) { (void)k; (void)raw; (void)flag; }

static midi_cz_patch_stub_t midi_cz_stub;
static midi_cz_patch_stub_t *cz_patch_of(uint32_t k) { (void)k; return &midi_cz_stub; }
static int cz_ed_put(uint32_t tr, uint32_t param, uint32_t value, uint8_t *raw)
{ (void)tr; (void)param; (void)value; (void)raw; return 0; }

/* ---- HOST stubs: clock output / transport ---- */

static uint32_t beat_samples(void)
{
    return midi_sync_active && midi_beat_samples ? midi_beat_samples : FS * 60u / (uint32_t)song.g[G_BPM];
}
/* midi_sync_emit / midi_sync_send: provided by the USB layer (usb.c) */
static uint32_t chain_div_samples(uint64_t units, uint32_t div, uint32_t *rem)
{ (void)units; (void)div; if (rem) *rem = 0; return 0; }

/* ---- upstream MIDI control (INCLUDED, unchanged) ---- */

#pragma push_macro("NENGINES")
#undef NENGINES
#define NENGINES 20u
#include "upstream/melodee/firmware/src/midi_control.c"
#pragma pop_macro("NENGINES")

/* ---- upstream MIDI clock (INCLUDED, unchanged) ---- */

#define seq_start midi_clk_seq_start
#define seq_stop  midi_clk_seq_stop
static void midi_clk_seq_start(void) { song.playing = 1; song.tick = 0; }
static void midi_clk_seq_stop(void)  { song.playing = 0; song.tick = 0; }
#include "upstream/melodee/firmware/src/midi_clock.c"
#undef seq_start
#undef seq_stop

/* ---- modd API hooks (not in upstream) ---- */

static int midi_follow(void) { return 0; }
static uint32_t midi_leds(void) { return 0; }
static int midi_lit(uint32_t ch) { (void)ch; return 0; }
static int midi_note_held_impl(void) { return 0; }

#ifndef MODD_USB_MIDI
static void midi_out_event(uint32_t msg) { (void)msg; }
#endif

static void midi_pc_load(track_t *t, uint32_t pc) { (void)t; (void)pc; }
static int midi_pc_poll(uint32_t *out) { (void)out; return 0; }
static void midi_take(uint32_t ch) { (void)ch; }

static void midi_routing_init(void)
{
    __builtin_memset(midi_ch, 0, sizeof midi_ch);
    __builtin_memset(midi_sel_on, 0, sizeof midi_sel_on);
    __builtin_memset(midi_owners, 0, sizeof midi_owners);
    __builtin_memset(midi_owner_trs, 0, sizeof midi_owner_trs);
    __builtin_memset(mchord, 0, sizeof mchord);
    __builtin_memset(midi_bend_target, 0, sizeof midi_bend_target);
    __builtin_memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    midi_strum_pending = 0;
    midi_event_source = 0;
    midi_hint = 0;
}

static void midi_clock_init(void)
{
    __builtin_memset(&midi_clock, 0, sizeof midi_clock);
    __builtin_memset(&lfo_ext, 0, sizeof lfo_ext);
    __builtin_memset(&midi_sync_out, 0, sizeof midi_sync_out);
    midi_beat_samples = 0;
    midi_sync_active = 0;
    seq_started = 0;
    seq_seek_units = 0;
    beat_pos = beat_n = 0;
}

static void midi_clock_tick(void) {}
