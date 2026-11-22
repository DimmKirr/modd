/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* FILTER: ANALOG engine filter type and E4, E5, E7. */

static int lvt_filter_visible(void) { return lvt_engine_is("ANALOG"); }

LVT_SIMPLE_PAGE(FILTER, "FILTER", "Filter", VIEW_EDIT, "FILTER", lvt_filter_visible,
    (P_FTYPE, 0, 2, "TYPE"), (P_E4, 0, 127, "E4"), (P_E5, 0, 127, "E5"), (P_E7, 0, 127, "E7"));
