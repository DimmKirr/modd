/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca core: params adapter.
 * Thin wrapper — the real implementation is in firmware/upstream/felucca/firmware/src/params.c.
 * Origin: hugelton/Felucca firmware/src/params.c @ v1.8.2 (6162de86) */

#define DLY_LEN (CONFIG_FELUCCA_DLY_LEN + 0u)
#define CHO_LEN (CONFIG_FELUCCA_CHO_LEN + 0u)
#define SL_LEN (CONFIG_FELUCCA_SL_LEN + 0u)
_Static_assert((DLY_LEN & (DLY_LEN - 1u)) == 0u, "CONFIG_FELUCCA_DLY_LEN must be a power of two");
_Static_assert((CHO_LEN & (CHO_LEN - 1u)) == 0u, "CONFIG_FELUCCA_CHO_LEN must be a power of two");
_Static_assert((SL_LEN & (SL_LEN - 1u)) == 0u, "CONFIG_FELUCCA_SL_LEN must be a power of two");

#include "upstream/felucca/firmware/src/params.c"

static int16_t modd_tp_fit(const track_t *t, uint32_t id, int32_t v)
{
    return (int16_t)param_fit(param_desc_of(eng_idx(t->eng_req), id), v);
}
