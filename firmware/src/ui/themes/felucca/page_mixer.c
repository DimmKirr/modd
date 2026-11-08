// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Felucca MIXER page: 4-track level/pan overview. */

static void fmixer_draw(void)
{
    uint32_t k;

    ui_header();

    for (k = 0; k < NTRK && k < 4; k++) {
        const track_t *t = &trk[k];
        char val[8];
        fui_fmt_val(val, t->p[P_LEVEL]);
        uint16_t vc = (song.sel == k) ? T_ACCENT : T_THEME;
        int32_t ratio = t->p[P_LEVEL] * 1000 / 127;
        ui_card(k, "LEVEL", val, "", vc, ratio, trk_icon(k, 1));
    }

    /* graph: vertical bars */
    cv_begin(240, Y_FOOT - Y_GRAPH, T_BG);
    {
        int32_t bw = 40, gap = 12;
        int32_t ox = (240 - (int32_t)NTRK * (bw + gap) + gap) / 2;
        int32_t max_h = Y_FOOT - Y_GRAPH - 20;

        for (k = 0; k < NTRK; k++) {
            int32_t x = ox + (int32_t)k * (bw + gap);
            int32_t h = trk[k].p[P_LEVEL] * max_h / 127;
            int32_t y = max_h - h + 4;
            uint16_t c = (song.sel == k) ? T_ACCENT : T_SEL;

            cv_rrect(x, y, bw, h, 3, c, T_BG);
            cv_trk(x + (bw - 16) / 2, max_h + 8, 16, k,
                   (song.sel == k) ? T_ACCENT : T_MID, T_BG);
        }
    }
    cv_blit(0, Y_GRAPH);

    {
        const track_t *t = &trk[song.sel];
        const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;
        ui_footer(eng ? eng->name : "---", "ACID", "MIXER");
    }
}

static const param_slot_t fmixer_slots[] = {
    {ENC_ALGO, P_LEVEL, 0, 127, 4, SLOT_TRACK, "T1 LVL"},
    {ENC_K1,   P_LEVEL, 0, 127, 4, SLOT_TRACK, "T2 LVL"},
    {ENC_K2,   P_LEVEL, 0, 127, 4, SLOT_TRACK, "T3 LVL"},
    {ENC_K3,   P_LEVEL, 0, 127, 4, SLOT_TRACK, "T4 LVL"},
};

static const modd_page_t FPAGE_MIXER = {
    .name = "MIXER", .title = "MIXER", .view = VIEW_GLO, .draw = fmixer_draw,
    .knob_map = fmixer_slots, .knob_count = 4,
};
