// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Audio tap: the last AUDIO_TAP_N output samples (mono, the mean of L and R,
 * 16-bit, 44.1 kHz) as the DAC gets them, after the master volume. Written by
 * the audio ISR one block at a time; GET /api/audio/capture serves the ring in
 * time order and GET /api/audio its RMS and peak. Emulator builds only
 * (CONFIG_RPC_AUDIO_TAP): 16 KiB in the framebuffer's section. */

#ifdef CONFIG_RPC_AUDIO_TAP

#define AUDIO_TAP_N 8192u                       /* 186 ms at 44.1 kHz; a power of two */

static int16_t audio_tap_ring[AUDIO_TAP_N] __attribute__((section(".framebuffer")));
static volatile uint32_t audio_tap_pos;         /* samples written since boot (wraps) */
static volatile uint8_t audio_tap_frozen;       /* set while /api/audio/capture streams the ring */

/* one DMA half: interleaved 24-bit DAC words (Q15 << 7, see modd_audio.h) */
static void audio_tap_block(const int32_t *o, uint32_t frames)
{
    uint32_t i, p = audio_tap_pos;
    if (audio_tap_frozen)
        return;
    for (i = 0; i < frames; i++) {
        int32_t m = ((o[2u * i] >> 7) + (o[2u * i + 1u] >> 7)) / 2;
        audio_tap_ring[p & (AUDIO_TAP_N - 1u)] = (int16_t)(m > 32767 ? 32767 : m < -32767 ? -32767 : m);
        p++;
    }
    audio_tap_pos = p;
}

/* RMS (rounded) and peak of the ring as it stands */
static void audio_tap_level(uint32_t *rms, uint32_t *peak)
{
    uint64_t sum = 0;
    uint32_t i, pk = 0, r = 0, b;
    for (i = 0; i < AUDIO_TAP_N; i++) {
        int32_t v = audio_tap_ring[i];
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        sum += (uint64_t)(a * a);
        if (a > pk) pk = a;
    }
    sum /= AUDIO_TAP_N;
    for (b = 1u << 15; b; b >>= 1) {            /* integer square root, 16-bit result */
        uint32_t t = r | b;
        if ((uint64_t)t * t <= sum) r = t;
    }
    *rms = r;
    *peak = pk;
}

#endif /* CONFIG_RPC_AUDIO_TAP */
