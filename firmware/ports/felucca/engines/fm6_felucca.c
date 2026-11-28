/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca FM6 engine: the upstream eng_fm6.c (+ fm6_core.c) included unchanged.
 * Origin: hugelton/Felucca firmware/src/eng_fm6.c @ v1.8.2 (d0260074) */

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

#include "upstream/felucca/firmware/src/eng_fm6.c"
