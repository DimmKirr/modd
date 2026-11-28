// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Memory arena contract: borrow a block by id, give it back. NULL means refused.
 * Smallest form for the effects host probe: one static block in .pool (not the free tail
 * _resource_start.._resource_end of POOL: nothing in modd allocates over that region yet, and a
 * static block shows up in the memory report and cannot collide with dock / wifi / net). The
 * real arena (demono-6) will sit behind the same three calls. Main loop only, never the audio
 * interrupt. */
#ifndef MODD_ARENA_H
#define MODD_ARENA_H
#include <stdint.h>

#ifdef CONFIG_ARENA_MELODEE
/* The one area: Melodee's resources.c, included unchanged by mem/arena_melodee_modd.c (after dsp.c, before the
 * effects host). These are declarations; the definitions live in that wrapper. The fx host borrows from it as
 * RES_FX0 + id. Two probe helpers show the two shapes upstream firmwares ask in, both served from that area:
 *   Melodee: resource_get(RES_ENGINE0 + part, bytes)          (engines.c cz_part, fm6_part, ...)
 *   SLOOP:   eng_arena_of(&trk[part], engine)                 (engines.c: a fixed per-part arena; its size is the
 *            sizeof of SLOOP's eng_arena_t union there, here the adapter's modd_eng_arena_bytes) */
static volatile uint8_t modd_arena_refuse;                                         /* test switch: refuse every fx get */
static uint32_t modd_arena_used(void);
static void *modd_arena_get(uint32_t id, uint32_t bytes);
static void modd_arena_put(uint32_t id);
static void *modd_arena_probe_melodee(uint32_t part, uint32_t bytes);
static void *modd_arena_probe_sloop(uint32_t part, uint32_t engine, uint32_t bytes);
static void modd_arena_probe_release(uint32_t part);
#else
#define MODD_ARENA_BYTES 4096u
#define MODD_ARENA_IDS   4u

static uint8_t modd_arena_mem[MODD_ARENA_BYTES] __attribute__((aligned(8), section(".pool")));
static uint32_t modd_arena_off[MODD_ARENA_IDS], modd_arena_len[MODD_ARENA_IDS];   /* len 0 = id free */
static volatile uint8_t modd_arena_refuse;                                         /* test switch: refuse every get */

static uint32_t modd_arena_used(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < MODD_ARENA_IDS; i++) n += modd_arena_len[i];
    return n;
}

/* First fit: try offset 0 and the end of every block lent out. One block per id. */
static void *modd_arena_get(uint32_t id, uint32_t bytes)
{
    uint32_t c, i, off, need = (bytes + 7u) & ~7u;
    if (id >= MODD_ARENA_IDS || modd_arena_len[id] || !need || modd_arena_refuse) return 0;
    for (c = 0; c <= MODD_ARENA_IDS; c++) {
        int ok = 1;
        off = c ? modd_arena_off[c - 1u] + modd_arena_len[c - 1u] : 0;
        if (c && !modd_arena_len[c - 1u]) continue;
        if (off + need > MODD_ARENA_BYTES) continue;
        for (i = 0; i < MODD_ARENA_IDS; i++)
            if (modd_arena_len[i] && off < modd_arena_off[i] + modd_arena_len[i] && modd_arena_off[i] < off + need) ok = 0;
        if (ok) {
            modd_arena_off[id] = off;
            modd_arena_len[id] = need;
            return &modd_arena_mem[off];
        }
    }
    return 0;
}

static void modd_arena_put(uint32_t id)
{
    if (id < MODD_ARENA_IDS) modd_arena_len[id] = 0;
}
#endif /* CONFIG_ARENA_MELODEE */
#endif /* MODD_ARENA_H */
