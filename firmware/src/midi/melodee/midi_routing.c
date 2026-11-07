/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/midi_control.c @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/midi_control.c */
/* Melodee MIDI routing stub: multi-channel routing, bend, CC, clock.
 * TODO: port from upstream melodee (midi_control.c, midi_clock.c). */

typedef struct {
    int16_t bend;
    uint8_t wheel, pedal, targets;
    uint8_t ready, semis, cents;
    uint8_t rpn_msb, rpn_lsb;
} midi_channel_t;

static midi_channel_t midi_ch[16];
static int32_t midi_bend_target[NTRK];

static midi_channel_t *midi_channel(uint32_t ch)
{
    midi_channel_t *c = &midi_ch[ch];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}

static track_t *midi_track(uint32_t ch)
{
    uint32_t i = ch % NTRK;
    return &trk[i];
}

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

static void midi_clock_init(void) {}
static void midi_clock_tick(void) {}

static void midi_routing_init(void) {}
