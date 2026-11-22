/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* FM6 operator pages: envelope per operator and the four levels (FM6 engine only). */

static int lvt_fm6_visible(void) { return lvt_engine_is("FM6"); }

LVT_SIMPLE_PAGE(FM6_OP1_ENV, "OP1 ENV", "Operator 1 Env", VIEW_EDIT, "OP1 ENV", lvt_fm6_visible,
    (P_FM1_ATK, 0, 127, "ATK"), (P_FM1_DEC, 0, 127, "DEC"), (P_FM1_SUS, 0, 127, "SUS"), (P_FM1_REL, 0, 127, "REL"));

LVT_SIMPLE_PAGE(FM6_OP2_ENV, "OP2 ENV", "Operator 2 Env", VIEW_EDIT, "OP2 ENV", lvt_fm6_visible,
    (P_FM2_ATK, 0, 127, "ATK"), (P_FM2_DEC, 0, 127, "DEC"), (P_FM2_SUS, 0, 127, "SUS"), (P_FM2_REL, 0, 127, "REL"));

LVT_SIMPLE_PAGE(FM6_OP3_ENV, "OP3 ENV", "Operator 3 Env", VIEW_EDIT, "OP3 ENV", lvt_fm6_visible,
    (P_FM3_ATK, 0, 127, "ATK"), (P_FM3_DEC, 0, 127, "DEC"), (P_FM3_SUS, 0, 127, "SUS"), (P_FM3_REL, 0, 127, "REL"));

LVT_SIMPLE_PAGE(FM6_OP4_ENV, "OP4 ENV", "Operator 4 Env", VIEW_EDIT, "OP4 ENV", lvt_fm6_visible,
    (P_FM4_ATK, 0, 127, "ATK"), (P_FM4_DEC, 0, 127, "DEC"), (P_FM4_SUS, 0, 127, "SUS"), (P_FM4_REL, 0, 127, "REL"));

LVT_SIMPLE_PAGE(FM6_OP_LEVEL, "OP LEVEL", "Operator Levels", VIEW_EDIT, "OP LEVEL", lvt_fm6_visible,
    (P_FM1_LEVEL, 0, 127, "OP1"), (P_FM2_LEVEL, 0, 127, "OP2"), (P_FM3_LEVEL, 0, 127, "OP3"), (P_FM4_LEVEL, 0, 127, "OP4"));
