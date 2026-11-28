/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca core: mod + voice adapter.
 * Thin wrapper — the real implementation is in firmware/upstream/felucca/firmware/src/{mod,voice}.c.
 * Origin: hugelton/Felucca firmware/src/{mod,voice}.c @ v1.8.2 (6162de86) */

#include "upstream/felucca/firmware/src/mod.c"
#include "upstream/felucca/firmware/src/voice.c"
static void mod_init(void) {}
