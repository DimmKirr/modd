// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test of the Felucca LED animations in modd: the wrapper (upstream's fm1_led_anim.h pictures plus the
 * frame driver written for Melodee's LED arrays) and led_idle.c (when the idle animation runs). The input HAL
 * is a set of stubs: the same names and shapes as firmware/hal/fm1_input.h. */
#include <stdint.h>
#include <stdio.h>

#define CONFIG_LED_ANIM_FELUCCA 1
#define CONFIG_LED_ANIM_BOOT 1
#define CONFIG_LED_ANIM_IDLE 1
#define CONFIG_LED_ANIM_IDLE_MS 1000

#define FM1_NCOL 11u
#define FM1_NKEY 41u
#define FM1_NENC 7u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as in firmware/hal/fm1_input.h */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static volatile struct { uint32_t notes, buttons, frames; } fm1_in;
static volatile struct { uint32_t enc_moves[FM1_NENC]; } fm1_in_stat;
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[3][FM1_NCOL];
static uint8_t fm1_led_dim_mask[3];

#include "felucca/leds/led_anim_modd.c"
#include "ui/led_idle.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void frame(void) { fm1_in.frames++; led_anim_tick(); }
static void frames(uint32_t n) { while (n--) frame(); }
static int leds_zero(void)
{
    uint32_t c;
    for (c = 0; c < FM1_NCOL; c++)
        if (fm1_led[c]) return 0;
    return 1;
}
static int lit(uint32_t id, const uint8_t *arr)
{
    uint32_t r, c;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id) return (arr[c] >> r) & 1;
    return -1;
}

int main(void)
{
    uint32_t i, f, id;
    CHECK(!fm1_led_anim_on());
    led_anim_tick();                                   /* nothing runs: nothing happens */
    CHECK(leds_zero());

    /* (a) the sweep owns the LEDs, then releases them with the last picture */
    fm1_led_anim_start(1);
    CHECK(fm1_led_anim_on() && !fm1_led_idle_on());
    frames(100);
    CHECK(fm1_led_anim_on());
    frames(FM1_ANIM_FRAMES + 1u - 100u);
    CHECK(!fm1_led_anim_on());
    CHECK(leds_zero());
    CHECK(fm1_led_dim_mask[0] == 31);
    for (id = 0; id < FM1_NKEY; id++)
        CHECK(lit(id, fm1_led_dim[0]) == 1);           /* with glow: every LED in dim plane 0 */
    for (i = 0; i < FM1_NCOL; i++)
        CHECK(fm1_led_dim[1][i] == 0 && fm1_led_dim[2][i] == 0);
    frames(5);
    CHECK(!fm1_led_anim_on());                         /* the last picture stays, nothing runs */

    fm1_led_anim_start(0);                             /* without glow: all dark at the end */
    frames(FM1_ANIM_FRAMES + 1u);
    CHECK(!fm1_led_anim_on());
    CHECK(leds_zero());
    for (id = 0; id < FM1_NKEY; id++)
        CHECK(lit(id, fm1_led_dim[0]) == 0);

    /* (b) sigma-delta: lit frames = sum of levels / 64, within 1 */
    {
        static const uint32_t ids[] = {0, 5, 13, 14, 20, 27, 33, 40};
        uint32_t k;
        for (k = 0; k < sizeof ids / sizeof ids[0]; k++) {
            uint32_t sum = 0, count = 0;
            id = ids[k];
            fm1_led_anim_start(1);
            for (f = 0; f <= FM1_ANIM_FRAMES; f++) {
                sum += fm1_anim_level(f, id, 1) & 0x7Fu;
                frame();
                count += (uint32_t)lit(id, fm1_led);
            }
            CHECK(count + 1u >= sum / 64u && count <= sum / 64u + 1u);
            CHECK(sum > 0);
        }
    }

    /* (c) a key down during the sweep ends it within one frame */
    fm1_led_anim_start(1);
    frames(10);
    CHECK(fm1_led_anim_on());
    fm1_in.notes = 1;
    frame();
    CHECK(!fm1_led_anim_on() && leds_zero());
    fm1_in.notes = 0;
    fm1_led_anim_start(1);
    frames(10);
    fm1_in.buttons = 4;
    frame();
    CHECK(!fm1_led_anim_on());
    fm1_in.buttons = 0;

    /* (d) a key down ends the idle animation */
    fm1_led_idle_start(1);
    CHECK(fm1_led_anim_on() && fm1_led_idle_on());
    frames(5);
    CHECK(fm1_led_idle_on());
    fm1_in.buttons = 1;
    frame();
    CHECK(!fm1_led_idle_on() && !fm1_led_anim_on());
    fm1_in.buttons = 0;

    /* (e) the idle frame wraps and the animation goes on */
    fm1_led_idle_start(1);
    frames(FM1_IDLE_FRAMES + 10u);
    CHECK(fm1_led_idle_on());
    frames(FM1_IDLE_FRAMES);
    CHECK(fm1_led_idle_on());
    fm1_led_anim_stop();
    CHECK(!fm1_led_anim_on());

    /* (f) led_idle_poll */
    CHECK(led_idle_poll(500, 0, 1) == 0 && !fm1_led_idle_on());     /* before the idle time */
    CHECK(led_idle_poll(999, 0, 1) == 0);
    CHECK(led_idle_poll(1000, 0, 1) == 1 && fm1_led_idle_on());     /* after it */
    CHECK(led_idle_poll(1100, 0, 1) == 1);
    CHECK(led_idle_poll(1500, 1, 1) == 0 && !fm1_led_idle_on());    /* busy stops it */
    CHECK(led_idle_poll(1600, 0, 1) == 1);
    fm1_in_stat.enc_moves[2]++;                                     /* a knob turn stops it */
    CHECK(led_idle_poll(1700, 0, 1) == 0 && !fm1_led_idle_on());
    CHECK(led_idle_poll(2600, 0, 1) == 0);
    CHECK(led_idle_poll(2700, 0, 1) == 1 && fm1_led_idle_on());
    fm1_in.notes = 2;                                               /* a key held counts as input */
    CHECK(led_idle_poll(5000, 0, 1) == 0 && !fm1_led_idle_on());
    fm1_in.notes = 0;
    CHECK(led_idle_poll(5500, 0, 1) == 0);
    fm1_led_anim_start(1);                                          /* the boot sweep owns the LEDs */
    CHECK(led_idle_poll(1000000, 0, 1) == 1 && fm1_led_anim_on() && !fm1_led_idle_on());
    CHECK(led_idle_poll(1000001, 1, 1) == 1 && fm1_led_anim_on());  /* busy does not stop the sweep */
    fm1_led_anim_stop();

    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
