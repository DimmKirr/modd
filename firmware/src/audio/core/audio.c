/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/audio.c @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/audio.c */
/* Minimal audio core: I2S DMA double-buffer, mix_block, master output.
 * Pattern from Melodee audio.c, stripped to the essentials. */

#define HALF_FRAMES BLOCK
#define HALF_WORDS  (HALF_FRAMES * 2u)

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(32)));
static int32_t mix_buf_l[BLOCK];
static int32_t mix_buf_r[BLOCK];

#ifdef CONFIG_EFFECTS_FELUCCA
static void mix_block(int32_t *out, uint32_t n);
#else
static void mix_block(void)
{
    uint32_t i;
#ifdef CONFIG_DOCK
    if (dock_active) {                          /* a dock module owns the block (dock.c) */
        dock_render(mix_buf_l, mix_buf_r, BLOCK);
        return;
    }
#endif
    for (i = 0; i < BLOCK; i++) {
        mix_buf_l[i] = 0;
        mix_buf_r[i] = 0;
    }
    for (i = 0; i < NTRK; i++)
        engine_block(&trk[i], mix_buf_l, mix_buf_r, BLOCK);
}
#endif /* !CONFIG_EFFECTS_FELUCCA */

void fm1_alnk0_irq(void)
{
    if (fm1_audio_irq_elapsed_ticks > AUDIO_DEADLINE_TICKS)
        audio_deadline_misses++;
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        int32_t *o = &abuf[half * HALF_WORDS];
#ifdef CONFIG_EFFECTS_FELUCCA
        {
            /* Felucca's mix, its buffers and its engines work one control tick (CTL frames) at a time */
            uint32_t b, j;
            _Static_assert(HALF_FRAMES % CTL == 0u, "the half-buffer is a whole number of control ticks");
            for (b = 0; b < HALF_FRAMES; b += CTL)
                mix_block(o + 2u * b, CTL);
            for (j = 0; j < 2u * HALF_FRAMES; j++)
                o[j] <<= AUDIO_OUT_SHIFT;
        }
#else
        {
            uint32_t i;
            int32_t mv;
            mix_block();
#ifdef CONFIG_FX_HOST
            fx_host_run(mix_buf_l, mix_buf_r, HALF_FRAMES);   /* the master slot: after the mix, before the master volume */
#endif
            mv = (int32_t)song.master_q12;
            for (i = 0; i < HALF_FRAMES; i++) {
#ifdef CONFIG_AUDIO_24BIT
                int32_t l = (int32_t)(((int64_t)mix_buf_l[i] * mv) >> 12);
                int32_t r = (int32_t)(((int64_t)mix_buf_r[i] * mv) >> 12);
                o[2u * i]     = audio_sat(l) >> AUDIO_OUT_SHIFT;
                o[2u * i + 1] = audio_sat(r) >> AUDIO_OUT_SHIFT;
#else
                o[2u * i]     = ((mix_buf_l[i] >> 2) * mv >> 10) << AUDIO_OUT_SHIFT;
                o[2u * i + 1] = ((mix_buf_r[i] >> 2) * mv >> 10) << AUDIO_OUT_SHIFT;
#endif
            }
        }
#endif /* CONFIG_EFFECTS_FELUCCA */
#ifdef CONFIG_RPC_AUDIO_TAP
        audio_tap_block(o, HALF_FRAMES);
#endif
        fm1_audio_ack_half();
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    for (i = 0; i < NTRK; i++) {
        __builtin_memset(&trk[i], 0, sizeof(track_t));
        trk[i].engine = 0;
        params_init(&trk[i]);
    }
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
