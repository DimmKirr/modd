/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* LFO 2: sync, trigger, polarity. */

LVT_SIMPLE_PAGE(LFO2, "LFO 2", "LFO 2", VIEW_LFO, "LFO 2", 0,
    (P_LSYNC, 0, 10, "SYNC"), (P_LTRIG, 0, 1, "TRIG"), (P_LPOL, 0, 1, "POL"), LVT_EMPTY);
