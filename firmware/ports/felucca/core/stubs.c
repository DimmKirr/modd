/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca core: shared glue stubs for absent engines and upstream dependencies.
 * Thin adapter — the real implementation is in firmware/upstream/felucca/. */

enum { DK_COUNT = 1 };
static const uint8_t DK_PLAYS[DK_COUNT] = {0};
static const char *const N_DRUM_KIT[DK_COUNT] = {"-"};
#ifndef CONFIG_ENGINE_FM6_FELUCCA
static const char *const N_FM6_PATCH[1] = {"-"};
#define FM6_OWN 0
static int fm6_slot_ok(int32_t s) { (void)s; return 1; }
#endif
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }
static void eng_mem_claim(const track_t *t) { (void)t; }
static voice_t *drum_reuse(track_t *t, uint32_t note) { (void)t; (void)note; return 0; }
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};
static uint32_t drum_lane(uint32_t note) { uint32_t i; for (i = 0; i < NLANE; i++) if (DRUM_LANE_NOTE[i] == note) return i; return 0; }
static uint32_t kb_out_tick;
