/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* ARP 2: swing, probability, hold, order. */

LVT_SIMPLE_PAGE(ARP2, "ARP 2", "Arp 2", VIEW_ARP, "ARP 2", 0,
    (P_ASWING, 0, 100, "SWNG"), (P_APROB, 0, 127, "PROB"), (P_AHOLD, 0, 1, "HOLD"), (P_AORDER, 0, 127, "ORDR"));
