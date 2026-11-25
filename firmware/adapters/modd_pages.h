/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Accessors for the pages, the shells and the REST API that need names from the audio stack (the modulation matrix).
 * The ones that only need the types are in modd_audio.h. Included after the audio stack. */
#ifndef MODD_PAGES_H
#define MODD_PAGES_H
#include <stdint.h>

/* --- State accessors: UI and RPC read/write upstream data through these ---
 * The boundary test (tests/build/data_boundary_test.py) rejects direct use
 * of trk[], song.g[], song.sel, ->p[] and ->eng_req in firmware/src/ui/ and
 * firmware/src/rpc/. These accessors keep the struct access inside the
 * adapter. */
static inline uint32_t modd_sel(void) { return song.sel; }
static inline void modd_sel_set(uint32_t k) { song.sel = (uint8_t)k; }
static inline int16_t modd_gp(uint32_t id) { return song.g[id]; }
static inline void modd_gp_set(uint32_t id, int16_t v) { song.g[id] = v; }
static inline uint8_t modd_playing(void) { return song.playing; }
#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
static inline int16_t modd_tpi(uint32_t k, uint32_t id) { return modd_tp(&trk[k], id); }
static inline void modd_tpi_set(uint32_t k, uint32_t id, int16_t v) { modd_tp_set(&trk[k], id, v); }
static inline int16_t modd_param(const track_t *t, uint32_t id) { return modd_tp(t, id); }
static inline void modd_param_set(track_t *t, uint32_t id, int16_t v) { modd_tp_set(t, id, v); }
#else
static inline int16_t modd_tpi(uint32_t k, uint32_t id) { return trk[k].p[id]; }
static inline void modd_tpi_set(uint32_t k, uint32_t id, int16_t v) { trk[k].p[id] = v; }
static inline int16_t modd_param(const track_t *t, uint32_t id) { return t->p[id]; }
static inline void modd_param_set(track_t *t, uint32_t id, int16_t v) { t->p[id] = v; }
#endif
static inline uint32_t modd_eng(uint32_t k) { return trk[k].eng_req; }
static inline uint32_t modd_engine_cur(uint32_t k) { return trk[k].engine; }
static inline track_t *modd_trk(uint32_t k) { return &trk[k]; }
static inline uint32_t modd_eng_of(const track_t *t) { return t->eng_req; }

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
/* global encoder actions (upstream: ui_input.c EN_ALGO, EN_PRESET → track_select, preset_step) */
static inline void modd_track_step(int32_t dir)
{
    int32_t s = (int32_t)song.sel + (dir > 0 ? 1 : -1);
    if (s < 0) s = 0;
    if (s >= (int32_t)NTRK) s = (int32_t)NTRK - 1;
    song.sel = (uint8_t)s;
}
static inline void modd_preset_step(int32_t dir)
{
    track_t *t = &trk[song.sel];
    uint32_t ei = t->eng_req;
    const engine_t *e = ei < NENGINES ? ENGINES[ei] : 0;
    if (e && e->npresets > 0) {
        int32_t v = (int32_t)t->preset + (dir > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v >= (int32_t)e->npresets) v = (int32_t)e->npresets - 1;
        t->preset = (uint8_t)v;
    }
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

/* --- Generic page data accessors (the data layer) ---
 * The shell and the API read and drive parameters only through these.
 * Under CONFIG_AUDIO_FELUCCA_STACK_INCLUDED they delegate to upstream's
 * PAGES[], page_desc(), param_format(), param_turn(). */

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED

static uint32_t modd_pg_count(void) { return NPAGES; }
static const char *modd_pg_title(uint32_t idx) { return idx < NPAGES ? PAGES[idx].title : ""; }

static uint32_t modd_pg_read(uint32_t idx, modd_card_t *out, uint32_t max)
{
    uint32_t k, n = 0;
    if (idx >= NPAGES)
        return 0;
    for (k = 0; k < 4u && n < max; k++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(&PAGES[idx], k, &vp);
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

static int modd_pg_turn(uint32_t idx, uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const param_desc_t *d;
    if (idx >= NPAGES || slot >= 4u)
        return 1;
    d = page_desc(&PAGES[idx], slot, &vp);
    if (!d || !vp)
        return 1;
    *vp = (int16_t)param_turn(d, *vp, steps);
    return 0;
}

static int modd_pg_set(uint32_t idx, uint32_t slot, int32_t value)
{
    int16_t *vp;
    const param_desc_t *d;
    if (idx >= NPAGES || slot >= 4u)
        return 1;
    d = page_desc(&PAGES[idx], slot, &vp);
    if (!d || !vp || value < d->min || value > d->max)
        return 1;
    *vp = (int16_t)param_fit(d, value);
    return 0;
}

static int32_t modd_pg_slot_value(uint32_t idx, uint32_t slot)
{
    int16_t *vp;
    const param_desc_t *d;
    if (idx >= NPAGES || slot >= 4u)
        return 0;
    d = page_desc(&PAGES[idx], slot, &vp);
    return (d && vp) ? *vp : 0;
}

static uint32_t modd_pg_slot_pid(uint32_t idx, uint32_t slot)
{
    if (idx >= NPAGES || slot >= 4u)
        return 0xFF;
    return PAGES[idx].id[slot];
}

#else
static uint32_t modd_pg_count(void) { return 0; }
static const char *modd_pg_title(uint32_t idx) { (void)idx; return ""; }
static uint32_t modd_pg_read(uint32_t idx, modd_card_t *out, uint32_t max) { (void)idx; (void)out; (void)max; return 0; }
static int modd_pg_turn(uint32_t idx, uint32_t slot, int32_t steps) { (void)idx; (void)slot; (void)steps; return 1; }
static int modd_pg_set(uint32_t idx, uint32_t slot, int32_t value) { (void)idx; (void)slot; (void)value; return 1; }
static int32_t modd_pg_slot_value(uint32_t idx, uint32_t slot) { (void)idx; (void)slot; return 0; }
static uint32_t modd_pg_slot_pid(uint32_t idx, uint32_t slot) { (void)idx; (void)slot; return 0xFF; }
#endif

#endif /* MODD_PAGES_H */
