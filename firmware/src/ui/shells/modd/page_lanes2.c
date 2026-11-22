/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* LANES 2: DRUM engine lane levels 5-8. */

LVT_SIMPLE_PAGE(LANES2, "LANES 2", "Lanes 2", VIEW_EDIT, "LANES 2", lvt_lanes_visible,
    (P_LN4, 0, 127, "LN5"), (P_LN5, 0, 127, "LN6"), (P_LN6, 0, 127, "LN7"), (P_LN7, 0, 127, "LN8"));
