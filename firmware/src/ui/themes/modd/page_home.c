// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd HOME: 4-track strip (cards) + transport/detail panel. */

static const char *const HOME_NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const char *const HOME_SCALE_NAMES[] = {
    "Chrom", "Major", "Minor", "Penta", "Blues", "Dorian",
    "Mixol", "Lydian", "Phryg", "Locr", "Harm", "Melod"
};

/* home_data_t, home_read(): ui/page_data.c (shared with the REST API) */

static void home_draw(void)
{
    home_data_t d;
    uint32_t k;
    home_read(&d);

    /* cards: one per track */
    for (k = 0; k < NTRK; k++) {
        char lbl[4], pstr[4];
        lbl[0] = 'T'; lbl[1] = (char)('1' + k); lbl[2] = 0;
        pstr[0] = 'P';
        if (d.preset[k] >= 10)
            pstr[1] = (char)('0' + d.preset[k] / 10);
        else
            pstr[1] = '0';
        pstr[2] = (char)('0' + d.preset[k] % 10);
        pstr[3] = 0;

        ui_card(k, lbl, d.engine_name[k], pstr,
                (k == d.selected) ? T_TRK(k) : T_MID,
                (k == d.selected) ? 1000 : -1, 0);
    }

    mt_panel_begin();

    /* transport: play/stop/rec glyph + BPM */
    {
        int32_t tx = 8, ty = 6;
        uint16_t tc = d.playing ? T_THEME : T_DIM;

        if (d.rec) {
            cv_circle(tx + 4, ty + 5, 4, T_REC, T_BG);
        } else if (d.playing) {
            int32_t i;
            for (i = 0; i < 8; i++)
                cv_rect(tx + i, ty + 1 + i / 2, 1, 8 - i, tc);
        } else {
            cv_rect(tx, ty, 8, 8, tc);
        }

        {
            char bpm[12];
            uint32_t b = d.bpm, bi = 0;
            if (b >= 100) bpm[bi++] = (char)('0' + b / 100);
            if (b >= 10)  bpm[bi++] = (char)('0' + (b / 10) % 10);
            bpm[bi++] = (char)('0' + b % 10);
            bpm[bi] = 0;
            cv_text_fit(tx + 14, ty_y(ty + 4, TY_TX), TY_TX, bpm, tc, T_BG, 50);
            cv_text_fit(tx + 60, ty_y(ty + 4, TY_LB), TY_LB, "BPM", T_DIM, T_BG, 40);
        }
    }

    /* separator */
    cv_rect(8, 22, UI_W - 16, 1, T_LINE);

    /* selected track detail */
    {
        int32_t dy = 28;

        /* track label + engine name */
        {
            char hdr[16];
            hdr[0] = 'T'; hdr[1] = (char)('1' + d.selected); hdr[2] = ' ';
            {
                const char *en = d.engine_name[d.selected];
                uint32_t i = 0, j = 3;
                while (en[i] && j < 14) hdr[j++] = en[i++];
                hdr[j] = 0;
            }
            cv_text_fit(8, ty_y(dy + 6, TY_TX), TY_TX, hdr, T_TRK(d.selected), T_BG, UI_W - 16);
        }

        /* detail: P03  C Minor  O+1 */
        {
            char detail[32];
            uint32_t di = 0;
            const char *sname;
            const char *rname;

            detail[di++] = 'P';
            if (d.preset[d.selected] >= 10)
                detail[di++] = (char)('0' + d.preset[d.selected] / 10);
            else
                detail[di++] = '0';
            detail[di++] = (char)('0' + d.preset[d.selected] % 10);
            detail[di++] = ' '; detail[di++] = ' ';

            rname = HOME_NOTE_NAMES[d.root];
            while (*rname) detail[di++] = *rname++;
            detail[di++] = ' ';

            if (d.scale < 12)
                sname = HOME_SCALE_NAMES[d.scale];
            else
                sname = "?";
            while (*sname) detail[di++] = *sname++;
            detail[di++] = ' '; detail[di++] = ' ';

            detail[di++] = 'O';
            if (d.oct < 0) {
                detail[di++] = '-';
                detail[di++] = (char)('0' + (-d.oct) % 10);
            } else if (d.oct > 0) {
                detail[di++] = '+';
                detail[di++] = (char)('0' + d.oct % 10);
            } else {
                detail[di++] = '0';
            }
            detail[di] = 0;

            cv_text_fit(8, ty_y(dy + 22, TY_LB), TY_LB, detail, T_MID, T_BG, UI_W - 16);
        }

        /* level bar */
        {
            int32_t bw = UI_W - 16;
            int32_t lvl = d.level;
            if (lvl > 100) lvl = 100;
            cv_rect(8, dy + 36, bw, 3, T_LINE);
            if (lvl > 0)
                cv_rect(8, dy + 36, bw * lvl / 100, 3, T_TRK(d.selected));
        }

        /* ADSR mini display */
        {
            int32_t bx = 8, by = dy + 46, bh = 24, bw = UI_W - 16;
            int32_t aw = d.atk * bw / 400;
            int32_t dw = d.dec * bw / 400;
            int32_t sw = bw / 4;
            int32_t sh = bh * d.sus / 100;
            int32_t rw = d.rel * bw / 400;
            int32_t x0, x1, x2, x3;
            if (aw < 4) aw = 4;
            if (dw < 4) dw = 4;
            if (rw < 4) rw = 4;
            if (aw + dw + sw + rw > bw) { aw = bw / 4; dw = bw / 4; sw = bw / 4; rw = bw / 4; }
            x0 = bx;
            x1 = bx + aw;
            x2 = x1 + dw;
            x3 = x2 + sw;
            cv_line(x0, by + bh, x1, by, T_DIM);
            cv_line(x1, by, x2, by + bh - sh, T_DIM);
            cv_line(x2, by + bh - sh, x3, by + bh - sh, T_DIM);
            cv_line(x3, by + bh - sh, x3 + rw, by + bh, T_DIM);
        }
    }

    /* version at the very bottom */
    cv_text_fit(8, ty_y(UI_PANEL_H - 8, TY_LB), TY_LB, CONFIG_FIRMWARE_VERSION, T_DIM, T_BG, UI_W - 16);

    mt_panel_end();
    ui_footer("", "modd", "");
}

static int home_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        int32_t s = (int32_t)song.sel + clicks;
        if (s < 0) s = 0;
        if (s >= (int32_t)NTRK) s = (int32_t)NTRK - 1;
        song.sel = (uint8_t)s;
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

#ifdef CONFIG_AUDIO
static int home_key(uint32_t note, uint32_t vel)
{
    track_t *t = &trk[song.sel];
    if (vel)
        trk_note_on(t, note, vel);
    else
        trk_note_off(t, note);
    return 1;
}
#endif

static const modd_page_t PAGE_HOME = {
    .name  = "HOME",
    .title = "modd",
    .view  = VIEW_HOME,
    .flags = PAGEF_HOME,
    .draw  = home_draw,
    .knob  = home_knob,
#ifdef CONFIG_AUDIO
    .key   = home_key,
#endif
    .help_text =
        "SELECT  Track 1-4\n"
        "HOME hold  Settings\n"
        "GLO hold   Weather",
};
