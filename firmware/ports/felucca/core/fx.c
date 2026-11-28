/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca core: slicer + effects + perform adapter.
 * Thin wrapper — the real implementation is in firmware/upstream/felucca/firmware/src/{slicer,fx,perform,click}.c.
 * Origin: hugelton/Felucca firmware/src/{slicer,fx,perform,click}.c @ v1.8.2 (6162de86) */

#include "upstream/felucca/firmware/src/slicer.c"
#include "upstream/felucca/firmware/src/fx.c"
_Static_assert((PB_FRAMES & (PB_FRAMES - 1u)) == 0u, "PB_FRAMES (NTRK * SL_LEN / 2) must be a power of two");
_Static_assert(PB_FREC <= PB_FRAMES, "CONFIG_FELUCCA_SL_LEN is too small for FREEZE (PB_FREC frames)");
_Static_assert(HB_MASK < PB_FRAMES, "CONFIG_FELUCCA_SL_LEN is too small for the OCT UP / DN delay (HB_MASK)");