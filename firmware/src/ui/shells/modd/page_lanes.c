/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* LANES: DRUM engine lane levels 1-4. */

static int lvt_lanes_visible(void) { return lvt_engine_is("DRUM"); }

LVT_SIMPLE_PAGE(LANES, "LANES", "Lanes", VIEW_EDIT, "LANES", lvt_lanes_visible,
    (P_LN0, 0, 127, "LN1"), (P_LN1, 0, 127, "LN2"), (P_LN2, 0, 127, "LN3"), (P_LN3, 0, 127, "LN4"));
