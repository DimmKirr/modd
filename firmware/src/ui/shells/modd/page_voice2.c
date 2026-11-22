/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* VOICE 2: allocation, detune, pan, mute. */

LVT_SIMPLE_PAGE(VOICE2, "VOICE 2", "Voice 2", VIEW_EDIT, "VOICE 2", 0,
    (P_ALLOC, 0, 127, "ALLOC"), (P_DETUNE, 0, 127, "DTUN"), (P_PAN, -64, 63, "PAN"), (P_MUTE, 0, 1, "MUTE"));
