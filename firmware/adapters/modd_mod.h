// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Modulation adapter: source/destination names and icons for UI.
 * Stubs are active only when no modulation upstream is configured. */
#ifndef MODD_MOD_H
#define MODD_MOD_H
#include <stdint.h>

#ifndef CONFIG_MODULATION
static const char *mod_dst_name(uint32_t dst) { (void)dst; return "---"; }
static const char *mod_dst_icon(uint32_t dst) { (void)dst; return ""; }
static const char *mod_src_icon(uint32_t src) { (void)src; return ""; }
static const char *mod_src_name(uint32_t src) { (void)src; return "---"; }
static int mod_begin(track_t *t) { (void)t; return 0; }
static void mod_end(track_t *t) { (void)t; }
static void mod_voice(track_t *t, voice_t *v, vmod_t *m, int32_t fine) { (void)t; (void)v; (void)m; (void)fine; }
static uint32_t mod_note(track_t *t, uint32_t note, uint32_t vel) { (void)t; (void)note; return vel; }
static void mod_init(void) {}
#endif

#endif /* MODD_MOD_H */
