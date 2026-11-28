// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Effects adapter: FX chain state for UI display and control.
 * Stubs are active only when no effects upstream is configured. */
#ifndef MODD_FX_H
#define MODD_FX_H
#include <stdint.h>

#if !defined(CONFIG_EFFECTS_FELUCCA) && !defined(CONFIG_EFFECTS_MELODEE) && !defined(CONFIG_EFFECTS_SLOOP)
static void fx_assign(uint32_t slot, uint32_t type) { (void)slot; (void)type; }
static void fx_default(track_t *t) { (void)t; }
static int fx_key_held(void) { return 0; }
static void fx_map_sync(void) { }
static const char *fx_say(uint32_t slot) { (void)slot; return "---"; }
#endif

/* Effect unit contract: one effect type family that the modd effects host (audio/effects/host/fx_host.c)
 * runs in a slot. The host owns the slot, the memory (modd_arena.h), the gate, the tail and the refusal;
 * a wrapper per unit adapts an effect from any firmware to this shape. No upstream terms here. */
typedef struct fx_unit {
    const char *name;
    uint8_t ntypes;
    const char *(*type_name)(uint32_t type);
    uint32_t (*bytes)(uint32_t type);                                         /* block to borrow for this type; 0 = none */
    void (*prepare)(void *state, void *mem, uint32_t type, int32_t amount);   /* main loop, gate closed: init state, set params; mem is zeroed by the host (NULL when bytes == 0) */
    void (*run)(void *state, void *mem, int32_t *l, int32_t *r, uint32_t n);  /* audio interrupt, in place; no allocation */
    void (*release)(void *state);                                             /* optional, main loop: start the tail (fade to dry) */
    int (*quiet)(void *state, void *mem);                                     /* optional: 1 = the tail is over */
} fx_unit_t;
#define FX_STATE_BYTES 256u   /* the host's fixed per-slot state area, handed to every call */

/* One card of the effect page (modd_pages.h modd_fx_page_read): label, value with its range, and the value as text. */
typedef struct { const char *label; int32_t value, min, max; char text[12]; } modd_card_t;

#endif /* MODD_FX_H */
