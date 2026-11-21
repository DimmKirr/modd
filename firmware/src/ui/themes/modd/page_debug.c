// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* LOG: the firmware's log (modd_log's ring) as LVGL widgets.
 * GLO held opens it and returns (shell.c).
 *
 *   cards    LOG lines logged, FROM the first line shown (and where in the ring), BUF the
 *            ring's lines, LEN a line's bytes
 *   rows     mm:ss.mmm and the line; a line longer than 22 characters wraps once
 *   footer   LVGL's heap in use / its size, the most used, and the longest frame: how to
 *            size LV_MEM_SIZE (lv_conf.h) and the UI task's stack (modd.c): visit every
 *            page, then read it here
 *
 * ALGO scrolls; while the view is at the bottom it follows new lines. */

#define LVT_LOG_ROWS 10u
#define LVT_LOG_WRAP 22u

static struct {
    lv_obj_t *ts[LVT_LOG_ROWS], *msg[LVT_LOG_ROWS];
    uint32_t  shown;                    /* count and scroll the rows show, in one number */
    uint32_t  last_count, mem_ms;
} lvt_log;

static uint32_t lvt_log_n(void)
{
    return modd_log_count < MODD_LOG_LINES ? modd_log_count : MODD_LOG_LINES;
}

static uint32_t lvt_log_max(void)
{
    uint32_t n = lvt_log_n();
    return n > LVT_LOG_ROWS ? n - LVT_LOG_ROWS : 0;
}

static void lvt_log_show(void)
{
    uint32_t n = lvt_log_n(), row = 0, i, sig;
    char b[16];

    sig = modd_log_count * 64u + modd_log_scroll + ((uint32_t)lvt_pal.trk << 28);
    if (sig == lvt_log.shown)
        return;
    lvt_log.shown = sig;

    lv_snprintf(b, sizeof b, "%u", (unsigned)modd_log_count);
    lvt_card_set(0, "LOG", b, "", lvt_pal.theme, -1);
    lv_snprintf(b, sizeof b, "%u", (unsigned)(n ? modd_log_scroll + 1u : 0u));
    lvt_card_set(1, "FROM", b, "", lvt_pal.accent,
                 lvt_log_max() ? (int32_t)(modd_log_scroll * 1000u / lvt_log_max()) : 0);
    lv_snprintf(b, sizeof b, "%u", (unsigned)MODD_LOG_LINES);
    lvt_card_set(2, "BUF", b, "", lvt_pal.dim, -1);
    lv_snprintf(b, sizeof b, "%u", (unsigned)MODD_LOG_LINE_LEN);
    lvt_card_set(3, "LEN", b, "", lvt_pal.dim, -1);

    for (i = 0; row < LVT_LOG_ROWS && modd_log_scroll + i < n; i++) {
        uint32_t idx = modd_log_scroll + i, ms = modd_log_time(idx);
        const char *line = modd_log_line(idx);
        lv_snprintf(b, sizeof b, "%02u:%02u.%03u", (unsigned)((ms / 60000u) % 100u),
                    (unsigned)((ms / 1000u) % 60u), (unsigned)(ms % 1000u));
        lvt_text(lvt_log.ts[row], b);
        lvt_text(lvt_log.msg[row], line);
        row++;
        if (lv_strlen(line) > LVT_LOG_WRAP && row < LVT_LOG_ROWS) {
            lvt_text(lvt_log.ts[row], "");
            lvt_text(lvt_log.msg[row], line + LVT_LOG_WRAP);
            row++;
        }
    }
    for (; row < LVT_LOG_ROWS; row++) {
        lvt_text(lvt_log.ts[row], "");
        lvt_text(lvt_log.msg[row], row == 0 && n == 0 ? "(no log)" : "");
    }
}

static void lvt_log_mem(void)
{
    lv_mem_monitor_t m;
    char b[48];
    lv_mem_monitor(&m);
    lv_snprintf(b, sizeof b, "heap %u/%uK max %uK  %u ms",
                (unsigned)((m.total_size - m.free_size + 512u) / 1024u),
                (unsigned)(m.total_size / 1024u), (unsigned)((m.max_used + 512u) / 1024u),
                (unsigned)lvt_draw_max_ms);
    lvt_footer("", b, "");
}

static void lvt_log_status(void)
{
    if (modd_log_count != lvt_log.last_count) {
        /* at the bottom (or before the screen filled): follow the new lines */
        uint32_t n_before = lvt_log.last_count < MODD_LOG_LINES ? lvt_log.last_count : MODD_LOG_LINES;
        uint32_t max_before = n_before > LVT_LOG_ROWS ? n_before - LVT_LOG_ROWS : 0;
        if (modd_log_scroll >= max_before)
            modd_log_scroll = lvt_log_max();
        lvt_log.last_count = modd_log_count;
    }
    lvt_log_show();
    if (fm1_ms - lvt_log.mem_ms >= 1000u) {
        lvt_log.mem_ms = fm1_ms;
        lvt_log_mem();
    }
}

static void lvt_log_enter(void)
{
    lv_obj_t *body = lvt_page_begin(), *panel;
    uint32_t i;
    lvt_cards(body);
    panel = lvt_panel(body);
    lv_obj_set_style_pad_row(panel, 0, 0);
    for (i = 0; i < LVT_LOG_ROWS; i++) {
        lv_obj_t *row = lvt_box(panel, LV_FLEX_FLOW_ROW);
        lv_obj_set_height(row, 13);
        lvt_log.ts[i] = lvt_caption(row, "");
        lv_obj_set_width(lvt_log.ts[i], 54);
        lvt_log.msg[i] = lv_label_create(row);
        lv_obj_add_style(lvt_log.msg[i], &lvt_st_small, 0);
        lv_obj_set_flex_grow(lvt_log.msg[i], 1);
        lv_label_set_long_mode(lvt_log.msg[i], LV_LABEL_LONG_MODE_CLIP);
        lv_label_set_text_static(lvt_log.msg[i], "");
    }
    modd_log_scroll = lvt_log_max();
    lvt_log.last_count = modd_log_count;
    lvt_log.shown = 0xFFFFFFFFu;
    lvt_log.mem_ms = fm1_ms - 1000u;
    lvt_log_status();
}

static int lvt_log_knob(uint32_t id, int32_t clicks)
{
    int32_t s;
    if (id != ENC_ALGO)
        return 0;
    s = (int32_t)modd_log_scroll + clicks;
    if (s < 0) s = 0;
    if ((uint32_t)s > lvt_log_max()) s = (int32_t)lvt_log_max();
    modd_log_scroll = (uint32_t)s;
    return 1;
}

static const modd_page_t PAGE_DEBUG = {
    .name   = "LOG",
    .title  = "Log",
    .view   = VIEW_GLO,
    .flags  = PAGEF_SYSTEM,
    .enter  = lvt_log_enter,
    .status = lvt_log_status,
    .knob   = lvt_log_knob,
    .help_text =
        "ALGO    Scroll log",
};
