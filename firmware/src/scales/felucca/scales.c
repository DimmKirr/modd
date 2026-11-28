// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Scales stub (Felucca).
 * TODO: extract SCALE_MASK[] from felucca seq.c into modd_scales.h,
 * import felucca (16) or melodee (70) scale sets via CONFIG_SCALES_*. */

static uint32_t scale_mask(uint32_t idx)
{
    (void)idx;
    return 0x0FFFu; /* chromatic: all 12 notes */
}

static void scales_init(void) {}
