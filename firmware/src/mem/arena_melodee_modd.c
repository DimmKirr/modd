// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Wrapper: Melodee's resources.c (bounded working-memory arena, linker-derived banks) as the one memory area behind
 * modd_arena.h. The upstream file is included unchanged. This file supplies what resources.c expects from its
 * includer (NPART comes from modd_audio.h): the enum constants it sizes its table with, the switches it tests, and
 * the linker symbols it reads (firmware/app*.ld). Then the modd_arena.h calls and the probe shapes on top of it.
 * Mutations run in the main loop only; the audio interrupt never reads the table (see the API probe in api_ctl.c). */
#define FX_STATE_COUNT 2          /* resource ids RES_FX0 .. +1: the effects host takes RES_FX0 + 0 */
#define MELODEE_USB_AUDIO 0
#define MELODEE_CACHE_RAM 0       /* the cache bank is skipped (RESOURCE_CACHE_AVAILABLE 0); its linker symbols are still read */
#ifndef FM1_IRQ_TARGET
#define FM1_IRQ_TARGET 1          /* selects the linker-symbol banks instead of the host arrays */
#endif

#include "upstream/melodee/firmware/src/resources.c"

static uint32_t modd_arena_used(void) { return resource_used(); }

static void *modd_arena_get(uint32_t id, uint32_t bytes)
{
    if (id >= FX_STATE_COUNT || !bytes || modd_arena_refuse || resource[RES_FX0 + id].size) return 0;
    return resource_get(RES_FX0 + id, bytes);
}

static void modd_arena_put(uint32_t id)
{
    if (id < FX_STATE_COUNT) resource_release(RES_FX0 + id);
}

/* Melodee's call, as its engines.c part helpers make it. */
static void *modd_arena_probe_melodee(uint32_t part, uint32_t bytes)
{
    return resource_get(RES_ENGINE0 + part % NPART, bytes);
}

/* SLOOP's call: eng_arena_of(track, engine) names no size (SLOOP sizes the arena as the union of its engines'
 * states); the adapter's size is modd_eng_arena_bytes. A part whose engine changed gets a fresh zeroed block. */
static uint32_t modd_eng_arena_bytes;
static uint8_t modd_eng_arena_own[NPART];            /* engine + 1 whose state is in the part's block, 0 = none */
static void *eng_arena_of(const track_t *t, uint32_t eng)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p >= NPART) return 0;
    if (modd_eng_arena_own[p] != eng + 1u) {
        resource_release(RES_ENGINE0 + p);
        modd_eng_arena_own[p] = 0;
    }
    void *m = resource_get(RES_ENGINE0 + p, modd_eng_arena_bytes);
    if (m) modd_eng_arena_own[p] = (uint8_t)(eng + 1u);
    return m;
}

static void *modd_arena_probe_sloop(uint32_t part, uint32_t engine, uint32_t bytes)
{
    modd_eng_arena_bytes = bytes;
    return eng_arena_of(&trk[part % NPART], engine);
}

static void modd_arena_probe_release(uint32_t part)
{
    resource_release(RES_ENGINE0 + part % NPART);    /* Melodee's eng_state_clear */
    modd_eng_arena_own[part % NPART] = 0;
}
