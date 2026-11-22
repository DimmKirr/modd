/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* ENV DEST: envelope amount to filter, pitch, shape; ENV sync. */

LVT_SIMPLE_PAGE(ENV_DEST, "ENV DEST", "Envelope Dest", VIEW_ENV, "ENV DEST", 0,
    (P_ED_FLT, -64, 63, "FLT"), (P_ED_PIT, -64, 63, "PIT"), (P_ED_SHP, -64, 63, "SHP"), (P_ESYNC, 0, 127, "SYNC"));
