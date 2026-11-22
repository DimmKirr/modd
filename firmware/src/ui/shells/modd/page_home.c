// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* HOME: the four tracks and the selected one, as LVGL widgets from home_read() (ui/page_data.c).
 *
 *   cards     T1..T4: engine and preset; the selected track's in its colour, gauge full
 *   transport play / stop / record and the tempo
 *   track     "T2 ANALOG" in the track's colour; preset, key, scale and octave under it
 *   level     a bar in the track's colour
 *   envelope  attack, decay, sustain, release as a line
 *   version   the firmware's
 *
 * SELECT picks the track (stops at T1 and T4); the note keys play it (with audio). */

static const char *const HOME_NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const char *const HOME_SCALE_NAMES[] = {
    "Chrom", "Major", "Minor", "Penta", "Blues", "Dorian",
    "Mixol", "Lydian", "Phryg", "Locr", "Harm", "Melod"
};

#define HOME_ENV_W 224
#define HOME_ENV_H 24

static struct {
    lv_obj_t *play, *rec, *bpm, *track, *detail, *level, *env;
    lv_point_precise_t pts[5];
    uint32_t env_sig;
} lvt_home;

static void lvt_home_show(void)
{
    home_data_t d;
    char b[40];
    uint32_t k, s;
    home_read(&d);
    s = d.selected < NTRK ? d.selected : 0;

    for (k = 0; k < NTRK; k++) {
        char lbl[4], pstr[4];
        lv_snprintf(lbl, sizeof lbl, "T%u", (unsigned)(k + 1u));
        lv_snprintf(pstr, sizeof pstr, "P%02u", (unsigned)(d.preset[k] % 100u));
        lvt_card_set(k, lbl, d.engine_name[k], pstr,
                     k == s ? lvt_pal.trk_c[k] : lvt_pal.mid, k == s ? 1000 : -1);
    }

    /* transport: a dot while recording, else play / stop; THEME while playing */
    lv_obj_set_hidden(lvt_home.rec, !d.rec);
    lv_obj_set_hidden(lvt_home.play, d.rec);
    lvt_text(lvt_home.play, d.playing ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP);
    lvt_text_color(lvt_home.play, d.playing ? lvt_pal.primary : lvt_pal.dim);
    lvt_bg_color(lvt_home.rec, lvt_pal.rec, 0);
    lv_snprintf(b, sizeof b, "%u", (unsigned)d.bpm);
    lvt_text(lvt_home.bpm, b);
    lvt_text_color(lvt_home.bpm, d.playing ? lvt_pal.primary : lvt_pal.dim);

    lv_snprintf(b, sizeof b, "T%u %s", (unsigned)(s + 1u), d.engine_name[s]);
    lvt_text(lvt_home.track, b);
    lv_snprintf(b, sizeof b, "P%02u  %s %s  O%s%d", (unsigned)(d.preset[s] % 100u),
                HOME_NOTE_NAMES[d.root % 12u], d.scale < 12u ? HOME_SCALE_NAMES[d.scale] : "?",
                d.oct > 0 ? "+" : d.oct < 0 ? "-" : "", d.oct < 0 ? -d.oct : d.oct);
    lvt_text(lvt_home.detail, b);

    lv_bar_set_value(lvt_home.level, d.level > 100 ? 100 : d.level < 0 ? 0 : d.level, LV_ANIM_OFF);
    lvt_bg_color(lvt_home.level, lvt_pal.primary, LV_PART_INDICATOR);
    lvt_bg_color(lvt_home.level, lvt_pal.line, 0);

    /* the envelope: A, D, a quarter of sustain, R */
    k = (uint32_t)d.atk | (uint32_t)d.dec << 8 | (uint32_t)d.sus << 16 | (uint32_t)d.rel << 24;
    if (k != lvt_home.env_sig) {
        int32_t bw = HOME_ENV_W, bh = HOME_ENV_H;
        int32_t aw = d.atk * bw / 400, dw = d.dec * bw / 400, sw = bw / 4;
        int32_t sh = bh * d.sus / 100, rw = d.rel * bw / 400;
        if (aw < 4) aw = 4;
        if (dw < 4) dw = 4;
        if (rw < 4) rw = 4;
        if (aw + dw + sw + rw > bw) aw = dw = sw = rw = bw / 4;
        lvt_home.env_sig = k;
        lvt_home.pts[0].x = 0;                 lvt_home.pts[0].y = bh;
        lvt_home.pts[1].x = aw;                lvt_home.pts[1].y = 0;
        lvt_home.pts[2].x = aw + dw;           lvt_home.pts[2].y = bh - sh;
        lvt_home.pts[3].x = aw + dw + sw;      lvt_home.pts[3].y = bh - sh;
        lvt_home.pts[4].x = aw + dw + sw + rw; lvt_home.pts[4].y = bh;
        lv_line_set_points(lvt_home.env, lvt_home.pts, 5);
    }
}

static void lvt_home_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel, *row, *l;

    lvt_cards(body);
    panel = lvt_panel(body);

    row = lvt_box(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lvt_home.play = lv_label_create(row);
    lv_label_set_text_static(lvt_home.play, "");
    lvt_home.rec = lv_obj_create(row);
    lv_obj_remove_style_all(lvt_home.rec);
    lv_obj_set_size(lvt_home.rec, 10, 10);
    lv_obj_set_style_radius(lvt_home.rec, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(lvt_home.rec, LV_OPA_COVER, 0);
    lvt_home.bpm = lv_label_create(row);
    lv_obj_set_style_text_font(lvt_home.bpm, &lv_font_montserrat_20, 0);
    lv_label_set_text_static(lvt_home.bpm, "");
    lvt_caption(row, "BPM");
    lvt_rule(panel);

    lvt_home.track = lv_label_create(panel);
    lv_obj_add_style(lvt_home.track, &lvt_st_theme, 0);
    lv_label_set_text_static(lvt_home.track, "");
    lvt_home.detail = lv_label_create(panel);
    lv_obj_add_style(lvt_home.detail, &lvt_st_small, 0);
    lv_obj_add_style(lvt_home.detail, &lvt_st_mid, 0);
    lv_label_set_text_static(lvt_home.detail, "");

    lvt_home.level = lv_bar_create(panel);
    lv_obj_set_size(lvt_home.level, lv_pct(100), 3);
    lv_obj_set_style_radius(lvt_home.level, 1, 0);
    lv_obj_set_style_radius(lvt_home.level, 1, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(lvt_home.level, LV_OPA_COVER, 0);
    lv_bar_set_range(lvt_home.level, 0, 100);

    lvt_home.env = lv_line_create(panel);
    lv_obj_set_size(lvt_home.env, HOME_ENV_W, HOME_ENV_H + 2);
    lv_obj_set_style_line_width(lvt_home.env, 1, 0);
    lv_obj_add_style(lvt_home.env, &lvt_st_dim, 0);     /* its line colour too */
    lv_obj_set_style_margin_top(lvt_home.env, 6, 0);
    lvt_home.env_sig = 0xFFFFFFFFu;

    l = lvt_caption(panel, CONFIG_FIRMWARE_VERSION);
    lv_obj_set_flex_grow(l, 1);         /* the version sits at the bottom */
    lv_obj_set_style_align(l, LV_ALIGN_BOTTOM_LEFT, 0);
    lvt_footer("", "modd", "");
    lvt_home_show();
}

static int lvt_home_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        int32_t s = (int32_t)song.sel + clicks;
        if (s < 0) s = 0;
        if (s >= (int32_t)NTRK) s = (int32_t)NTRK - 1;
        song.sel = (uint8_t)s;
        return 1;
    }
    return 0;
}

#ifdef CONFIG_AUDIO
static int lvt_home_key(uint32_t note, uint32_t vel)
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
    .name   = "HOME",
    .title  = "modd",
    .view   = VIEW_HOME,
    .flags  = PAGEF_HOME,
    .enter  = lvt_home_enter,
    .status = lvt_home_show,
    .knob   = lvt_home_knob,
#ifdef CONFIG_AUDIO
    .key    = lvt_home_key,
#endif
    .help_text =
        "SELECT  Track 1-4\n"
        "HOME hold  Settings\n"
        "GLO hold   Log",
};
