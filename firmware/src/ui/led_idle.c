// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* When the Felucca LED animations run (CONFIG_LED_ANIM_FELUCCA): the boot sweep owns the LEDs while it plays;
 * after CONFIG_LED_ANIM_IDLE_MS without panel input (a key or button down, a knob turned) and with nothing busy,
 * the idle animation starts; any input or a busy transport stops it. The shell calls led_idle_poll() from
 * its LED writer and leaves the LEDs alone while it returns non-zero. */

static int led_idle_poll(uint32_t now_ms, uint32_t busy, uint32_t glow)
{
#ifdef CONFIG_LED_ANIM_IDLE
    static uint32_t last_in, enc_sum;
    uint32_t i, s = 0;
    for (i = 0; i < FM1_NENC; i++)
        s += fm1_in_stat.enc_moves[i];
    if ((fm1_in.notes | fm1_in.buttons) || s != enc_sum)
        last_in = now_ms;
    enc_sum = s;
    if (busy || now_ms - last_in < CONFIG_LED_ANIM_IDLE_MS) {
        if (fm1_led_idle_on())
            fm1_led_anim_stop();
        return fm1_led_anim_on();
    }
    if (!fm1_led_anim_on())                            /* not over the boot sweep */
        fm1_led_idle_start(glow);
    return fm1_led_anim_on();
#else
    (void)now_ms; (void)busy; (void)glow;
    return fm1_led_anim_on();
#endif
}
