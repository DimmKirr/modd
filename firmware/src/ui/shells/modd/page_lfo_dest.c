/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* LFO DEST: LFO amount to pitch, filter, shape, amp. */

LVT_SIMPLE_PAGE(LFO_DEST, "LFO DEST", "LFO Dest", VIEW_LFO, "LFO DEST", 0,
    (P_LD_PIT, -64, 63, "PIT"), (P_LD_FLT, -64, 63, "FLT"), (P_LD_SHP, -64, 63, "SHP"), (P_LD_AMP, 0, 127, "AMP"));
