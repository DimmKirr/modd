/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* VOICE 3: spread and aftertouch. */

LVT_SIMPLE_PAGE(VOICE3, "VOICE 3", "Voice 3", VIEW_EDIT, "VOICE 3", 0,
    (P_SPRD, 0, 127, "SPRD"), (P_ATSNS, 0, 127, "AT SNS"), (P_ATDST, 0, 127, "AT DST"), (P_ATAMT, 0, 127, "AT AMT"));
