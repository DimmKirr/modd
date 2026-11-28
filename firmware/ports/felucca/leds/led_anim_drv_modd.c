/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: hugelton/Felucca firmware/bsp/boards/fm1/fm1_input.h @ v1.8.2 (6162de86):
 *   fm1__anim_frame, fm1__anim_begin, fm1_led_anim_start, fm1_led_anim_on, fm1_led_idle_start, fm1_led_idle_on,
 *   fm1_led_anim_stop (REWRITTEN for Melodee's LED arrays).
 * Approved by Dmitry Kireev, 2026-11-27: the driver lives inside Felucca's input scan, and modd uses Melodee's
 * input HAL (firmware/hal/fm1_input.h), so it cannot be included.
 *
 * The frame driver for the animations in fm1_led_anim.h: the same names and semantics as Felucca's. While an
 * animation runs it owns fm1_led and fm1_led_dim (the UI leaves them alone while fm1_led_anim_on() is true).
 * Each LED is drawn by a first-order sigma-delta over 64 frames: level q lights it in q of 64 frames. The glow
 * goes to dim plane 0 at duty 1/32 (fm1_led_dim_mask[0] = 31). One frame = one scan frame (fm1_in.frames),
 * 11 ticks of the 10 kHz TIMER5 interrupt. Uses the unity build's input HAL; includes no header itself. */

#define LED_ANIM_GLOW_MASK 31u

static volatile uint16_t fm1__an_f;                    /* 0: off, else the frame being drawn + 1 */
static uint8_t fm1__an_glow, fm1__an_idle, fm1__an_cb[FM1_NKEY], fm1__an_acc[FM1_NKEY];   /* LED: column << 3 | row, its sigma-delta */
static uint32_t fm1__an_seen;                          /* fm1_in.frames at the last drawn frame */

static void fm1__anim_frame(void)
{
    uint32_t f = (uint32_t)fm1__an_f - 1u, i, idle = fm1__an_idle;
    uint8_t l[FM1_NCOL] = {0}, d[FM1_NCOL] = {0};
    if (fm1_in.notes | fm1_in.buttons) {               /* a key or a button down: done, the UI's at once */
        if (idle) {
            fm1__an_f = 0;
            return;
        }
        f = FM1_ANIM_FRAMES;
    }
    for (i = 0; i < FM1_NKEY; i++) {
        uint32_t q = idle ? fm1_idle_level(f, i, fm1__an_glow) : fm1_anim_level(f, i, fm1__an_glow);
        uint32_t cb = fm1__an_cb[i], a = fm1__an_acc[i] + (q & 0x7Fu);
        uint32_t m = 1u << (cb & 7u);
        if (a >= FM1_ANIM_FULL) {                      /* lit frames: q of 64 */
            a -= FM1_ANIM_FULL;
            l[cb >> 3] |= (uint8_t)m;
        }
        if (q & FM1_ANIM_GLOW)
            d[cb >> 3] |= (uint8_t)m;
        fm1__an_acc[i] = (uint8_t)a;
    }
    if (idle)                                          /* endless: the frame wraps */
        f = (f + 1u) & (FM1_IDLE_FRAMES - 1u);
    fm1_led_dim_mask[0] = (uint8_t)LED_ANIM_GLOW_MASK;
    for (i = 0; i < FM1_NCOL; i++) {
        fm1_led_dim[0][i] = d[i];
        fm1_led_dim[1][i] = fm1_led_dim[2][i] = 0;
        fm1_led[i] = !idle && f >= FM1_ANIM_FRAMES ? 0u : l[i];
    }
    fm1__an_f = (uint16_t)(idle ? f + 1u : f >= FM1_ANIM_FRAMES ? 0u : f + 2u);   /* the sweep's last picture stays */
}

static void fm1__anim_begin(uint32_t glow, uint32_t idle)
{
    uint32_t i, r, c;
    fm1__an_f = 0;
    for (i = 0; i < FM1_NKEY; i++)
        for (r = 1; r < 5u; r++)
            for (c = 0; c < FM1_NCOL; c++)
                if (FM1_KEYMAP[r][c] == (int8_t)i)
                    fm1__an_cb[i] = (uint8_t)(c << 3 | r);
    for (i = 0; i < FM1_NKEY; i++)
        fm1__an_acc[i] = FM1_ANIM_FULL / 2u;
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = fm1_led_dim[0][c] = fm1_led_dim[1][c] = fm1_led_dim[2][c] = 0;
    fm1__an_glow = glow != 0u;
    fm1__an_idle = idle != 0u;
    fm1__an_seen = fm1_in.frames;
    fm1__an_f = 1;
}

static void fm1_led_anim_start(uint32_t glow) { fm1__anim_begin(glow, 0); }
static int fm1_led_anim_on(void) { return fm1__an_f != 0u; }
static void fm1_led_idle_start(uint32_t glow) { fm1__anim_begin(glow, 1); }
static int fm1_led_idle_on(void) { return fm1__an_f != 0u && fm1__an_idle; }
static void fm1_led_anim_stop(void) { fm1__an_f = 0; } /* the LEDs as the last frame left them: the UI writes them */

/* from the TIMER5 interrupt, every tick: one animation frame per scan frame */
static void led_anim_tick(void)
{
    if (fm1__an_f == 0u)
        return;
    if (fm1_in.frames == fm1__an_seen)
        return;
    fm1__an_seen = fm1_in.frames;
    fm1__anim_frame();
}
