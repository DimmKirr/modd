/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* CHORD: chord type and voicing. */

LVT_SIMPLE_PAGE(CHORD, "CHORD", "Chord", VIEW_SCL, "CHORD", 0,
    (P_CHRD, 0, 127, "CHRD"), (P_VOIC, 0, 127, "VOIC"), LVT_EMPTY, LVT_EMPTY);
