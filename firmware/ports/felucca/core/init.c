/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca core: track init, song defaults, parameter locks.
 * Thin wrapper — depends on upstream params and engine tables. */

#define AUTO_NPARAMS 8
static const uint16_t auto_params[AUTO_NPARAMS] = {
    P_FLT, P_RES, P_SHP, P_DIST, P_DLY, P_REV, P_CHOR, P_LEVEL,
};
static uint8_t auto_locks[NTRK][NSTEP][AUTO_NPARAMS];
static uint8_t auto_inited;
static void auto_ensure_init(void)
{
    if (auto_inited) return;
    memset(auto_locks, 0xFF, sizeof auto_locks);
    auto_inited = 1;
}

static void felucca_tracks_init(void)
{
    uint32_t i, k;
    for (k = 0; k < NTRK; k++) {
        __builtin_memset(&trk[k], 0, sizeof(track_t));
        for (i = 0; i < P_COUNT; i++)
            trk[k].p[i] = TP[i].def;
    }
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
}

static void modd_song_init(void)
{
    static const uint8_t ENG_OF[NTRK] = {0, 12, 3, 2};
    uint32_t k, i;
    song.sel = 1;
    for (k = 0; k < NTRK; k++) {
        uint32_t e = eng_idx(ENG_OF[k]);
        trk[k].eng_req = trk[k].engine = (uint8_t)e;
        for (i = 0; i < 8u; i++)
            trk[k].p[P_E0 + i] = ENGINES[e]->edit[i].def;
    }
#ifdef CONFIG_ENGINE_FM6_FELUCCA
    fm6_init();
#endif
}
