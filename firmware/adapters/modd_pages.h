/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Accessors for the pages, the shells and the REST API that need names from the audio stack (the modulation matrix).
 * The ones that only need the types are in modd_audio.h. Included after the audio stack. */
#ifndef MODD_PAGES_H
#define MODD_PAGES_H
#include <stdint.h>

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
/* switch a track's engine as the panel does: eng_req and the new engine's P_E0..P_E7 together (IRQ off), the
 * stack's engine_block (voice.c) fades the old engine out and switches */
static void modd_engine_select(track_t *t, uint32_t ei)
{
    uint32_t i;
    if (ei >= NENGINES)
        ei = 0;
    fm1_irq_off();
    t->eng_req = (uint8_t)ei;
    t->preset = 0;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = ENGINES[ei]->edit[i].def;
    fm1_irq_on();
}
static const char *modd_mod_src_name(uint32_t src) { return src < sizeof N_MSRC / sizeof N_MSRC[0] ? N_MSRC[src] : "---"; }
static const char *modd_mod_dst_name(const track_t *t, uint32_t dst) { return mod_dst_name(t, (int32_t)dst); }
#else
static const char *modd_mod_src_name(uint32_t src) { return mod_src_name(src); }
static const char *modd_mod_dst_name(const track_t *t, uint32_t dst) { (void)t; return mod_dst_name(dst); }
#endif

/* The effect page of this build as cards (modd_card_t: modd_fx.h, the host's cards need it earlier), one interface
 * over two audio paths. The text is the value as the page shows it. A build with no effect page reads 0 cards. */

#if defined(CONFIG_AUDIO_FELUCCA_STACK_INCLUDED)
/* the stack's INSERT page: its PAGES[] entry (the first with the INSERT graph), each card's descriptor from page_desc
 * (TP[], and the type's own labels), the text from param_format; a set stores the value as a knob does (a 16-bit store,
 * clamped by the descriptor's range) */
static const page_t *modd_fx_page(void)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_INS)
            return &PAGES[i];
    return 0;
}
static uint32_t modd_fx_page_read(modd_card_t *out, uint32_t max)
{
    const page_t *pg = modd_fx_page();
    uint32_t k, n = 0;
    for (k = 0; pg && k < 4u && n < max; k++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, k, &vp);
        char val[12];
        const char *unit, *u;
        uint32_t j = 0;
        if (!d || !vp)
            continue;
        param_format(d, *vp, val, &unit);
        out[n].label = d->label;
        out[n].value = *vp;
        out[n].min = d->min;
        out[n].max = d->max;
        for (u = val; *u && j < 11u; u++)
            out[n].text[j++] = *u;
        for (u = unit; *u && j < 11u; u++)
            out[n].text[j++] = *u;
        out[n].text[j] = 0;
        n++;
    }
    return n;
}
static int modd_fx_page_set(uint32_t slot, int32_t value)
{
    const page_t *pg = modd_fx_page();
    int16_t *vp;
    const param_desc_t *d;
    if (!pg || slot >= 4u)
        return 1;
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || value < d->min || value > d->max)
        return 1;
    *vp = (int16_t)param_fit(d, value);
    return 0;
}
#elif defined(CONFIG_FX_HOST)
static uint32_t modd_fx_page_read(modd_card_t *out, uint32_t max) { return fx_host_page_read(out, max); }
static int modd_fx_page_set(uint32_t slot, int32_t value) { return fx_host_page_set(slot, value); }
#else
static uint32_t modd_fx_page_read(modd_card_t *out, uint32_t max) { (void)out; (void)max; return 0; }
static int modd_fx_page_set(uint32_t slot, int32_t value) { (void)slot; (void)value; return 1; }
#endif

#endif /* MODD_PAGES_H */
