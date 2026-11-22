/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* SLICER: depth, pattern and rate. */

LVT_SIMPLE_PAGE(SLICER, "SLICER", "Slicer", VIEW_FX, "SLICER", 0,
    (P_SLCR, 0, 127, "SLCR"), (P_SLPAT, 1, 16, "PAT"), (P_SLRATE, 0, 127, "RATE"), (P_SLDEPTH, 0, 127, "DEPTH"));
