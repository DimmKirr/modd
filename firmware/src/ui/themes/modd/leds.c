// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd theme LED indicators.
 * Felucca's addressing scheme (led_put) over Melodee's 3-plane HAL
 * (fm1_led_dim[3], fm1_led_dim_mask[3], fm1_led_pwm.h). Each theme owns its
 * own ui_leds(); a Felucca or Melodee theme would have a different one. */

/* --- LED addressing (inline scan, no lookup table: saves 41 bytes of RAM) --- */

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint32_t p, r;
    if (!on) return;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                nl[p] |= (uint8_t)(1u << r);
}

/* PLAY's second LED (green): column 8, row bit 1 (not in the key matrix) */
#define LED_PLAY_GREEN ((8u << 3) | 1u)

/* --- Brightness levels --- */

enum { LIGHTS_OFF, LIGHTS_LOW, LIGHTS_MID, LIGHTS_HIGH, LIGHTS_FULL, LIGHTS_N };
#define BTN_DIM_MASK 3u

static uint8_t lights_dim_mask(uint32_t l)
{
    return l == LIGHTS_LOW ? 7u : l == LIGHTS_MID ? 3u : l == LIGHTS_HIGH ? 1u : 0u;
}

static uint8_t breath_val(void)
{
    uint32_t i = (fm1_ms / 125u) & 7u;
    if (i > 4u) i = 8u - i;
    return i == 4u ? 0xFFu : (uint8_t)((1u << i) - 1u);
}

static uint32_t breath_on(void) { return breath_val() != 0xFFu; }

/* --- Per-frame LED update --- */

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0};
    uint8_t nd[FM1_NCOL] = {0};
    uint8_t nb[FM1_NCOL] = {0};
    uint8_t nw[FM1_NCOL] = {0};
    uint32_t k, c, lights;

    lights = modd_lights % LIGHTS_N;

    /* active page button: bright (reverse-scan the tap map) */
    for (k = 0; k < BTN_COUNT; k++)
        if (modd_controls.btn_tap[k] == nav_view) { led_put(nl, k, 1); break; }

#ifdef CONFIG_AUDIO
    /* transport: PLAY steady while playing, breathing while counting in */
    led_put(nl, BTN_PLAY, song.playing && !seq_counting());
    led_put(nw, BTN_PLAY, seq_counting());

    /* REC: bright while recording, breathing while armed but not yet recording */
    {
        uint32_t armed = song.rec != 0u;
        uint32_t recording = armed && song.playing && !seq_counting();
        led_put(nl, BTN_REC, recording);
        led_put(nw, BTN_REC, armed && !recording);
    }

    /* PLAY green LED: lit while playing or on count-in beats */
    if (song.playing || seq_counting())
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
#endif

    /* note keys: held keys bright, scale dim */
    c = fm1_in.notes;
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((c >> k) & 1u));
#ifdef CONFIG_AUDIO
        if (!((c >> k) & 1u) && lights != LIGHTS_OFF) {
            uint32_t note = 53u + k;
            uint32_t pc = ((note + 120u - (uint32_t)trk[song.sel].p[P_ROOT]) % 12u);
            if ((scale_mask(trk[song.sel].p[P_SCALE]) >> pc) & 1u)
                led_put(nd, 14u + k, 1);
        }
#endif
    }

    /* idle buttons glow (dim plane 1) unless breathing */
    if (lights != LIGHTS_OFF) {
        for (k = 0; k < BTN_COUNT; k++)
            led_put(nb, k, 1);
    }

    /* commit to HAL arrays */
    fm1_led_dim_mask[0] = lights_dim_mask(lights);
    fm1_led_dim_mask[1] = BTN_DIM_MASK;
    fm1_led_dim_mask[2] = breath_val();
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[0][c] = (uint8_t)(nd[c] | nl[c]);
        fm1_led_dim[1][c] = nb[c];
        fm1_led_dim[2][c] = breath_on() ? nw[c] : 0u;
        fm1_led[c] = nl[c];
    }
}
