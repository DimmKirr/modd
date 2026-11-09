// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd DEBUG: scrollable system log viewer.
 * Adapted from Felucca's page_debug.c for the modd theme (AF_XS font,
 * T_* palette tokens, mt_panel canvas + ui_card status bar). Opened via
 * GLO tap toggle from any page when CONFIG_DEBUG_LOG is enabled. */

#define MDBG_ROW_H    13
#define MDBG_VIS      (MT_H_PANEL / MDBG_ROW_H)
#define MDBG_X_PAD    4
#define MDBG_TS_W     56
#define MDBG_MSG_X    (MDBG_X_PAD + MDBG_TS_W)
#define MDBG_MSG_W    (MT_W - MDBG_MSG_X - 2)
#define MDBG_WRAP_AT  22

static uint32_t mdbg_last_count;

static void mdbg_fmt_ts(char *out, uint32_t ms)
{
    uint32_t mm  = (ms / 60000u) % 100u;
    uint32_t ss  = (ms / 1000u) % 60u;
    uint32_t frac = ms % 1000u;
    out[0] = '0' + (char)(mm / 10u);
    out[1] = '0' + (char)(mm % 10u);
    out[2] = ':';
    out[3] = '0' + (char)(ss / 10u);
    out[4] = '0' + (char)(ss % 10u);
    out[5] = '.';
    out[6] = '0' + (char)(frac / 100u);
    out[7] = '0' + (char)((frac / 10u) % 10u);
    out[8] = '0' + (char)(frac % 10u);
    out[9] = 0;
}

static void mdbg_fmt_u32(char *out, uint32_t v)
{
    char tmp[8];
    uint32_t i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v && i < 7) { tmp[i++] = '0' + (char)(v % 10u); v /= 10u; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

static int mdbg_at_bottom(void)
{
    uint32_t n = modd_log_count < MODD_LOG_LINES ? modd_log_count : MODD_LOG_LINES;
    if (n <= MDBG_VIS) return 1;
    return modd_log_scroll >= n - MDBG_VIS;
}

static void mdbg_draw(void)
{
    uint32_t n = modd_log_count < MODD_LOG_LINES ? modd_log_count : MODD_LOG_LINES;
    uint32_t row = 0, i;

    /* info cards: LOG count, FROM scroll position, BUF capacity */
    {
        char cnt[8];
        mdbg_fmt_u32(cnt, modd_log_count);
        ui_card(0, "LOG", cnt, "", T_THEME, -1, 0);
    }
    {
        char scr[8];
        mdbg_fmt_u32(scr, n > 0 ? modd_log_scroll + 1 : 0);
        int32_t ratio = n > MDBG_VIS ? (int32_t)modd_log_scroll * 1000 / (int32_t)(n - MDBG_VIS) : 0;
        ui_card(1, "FROM", scr, "", T_ACCENT, ratio, 0);
    }
    {
        char buf[8];
        mdbg_fmt_u32(buf, MODD_LOG_LINES);
        ui_card(2, "BUF", buf, "", T_DIM, -1, 0);
    }
    {
        char len[8];
        mdbg_fmt_u32(len, MODD_LOG_LINE_LEN);
        ui_card(3, "LEN", len, "", T_DIM, -1, 0);
    }

    /* log text area: full panel, no inline header */
    mt_panel_begin();

    if (n == 0) {
        cv_text_c(MT_W / 2, ty_y(MT_H_PANEL / 2, &AF_XS), &AF_XS,
                  "(no log)", T_DIM, T_BG);
    } else {
        for (i = 0; row < MDBG_VIS && (modd_log_scroll + i) < n; i++) {
            uint32_t idx = modd_log_scroll + i;
            const char *line = modd_log_line(idx);
            int32_t ry = (int32_t)(row * MDBG_ROW_H);
            uint32_t len = 0;
            char ts[10];

            while (line[len]) len++;

            mdbg_fmt_ts(ts, modd_log_time(idx));
            cv_text_fit(MDBG_X_PAD, ry + 1, &AF_XS, ts, T_DIM, T_BG, MDBG_TS_W - 2);
            cv_text_fit(MDBG_MSG_X, ry + 1, &AF_XS, line, T_TEXT, T_BG, MDBG_MSG_W);
            row++;

            if (len > MDBG_WRAP_AT && row < MDBG_VIS) {
                ry = (int32_t)(row * MDBG_ROW_H);
                cv_text_fit(MDBG_MSG_X, ry + 1, &AF_XS,
                            line + MDBG_WRAP_AT, T_TEXT, T_BG, MDBG_MSG_W);
                row++;
            }
        }
    }

    mt_panel_end();
    ui_footer("debug", "---", "LOG");
}

static int mdbg_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_ALGO) {
        uint32_t n = modd_log_count < MODD_LOG_LINES ? modd_log_count : MODD_LOG_LINES;
        int32_t s = (int32_t)modd_log_scroll + clicks;
        uint32_t max = n > MDBG_VIS ? n - MDBG_VIS : 0;
        if (s < 0) s = 0;
        if ((uint32_t)s > max) s = (int32_t)max;
        modd_log_scroll = (uint32_t)s;
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static void mdbg_enter(void)
{
    uint32_t n = modd_log_count < MODD_LOG_LINES ? modd_log_count : MODD_LOG_LINES;
    modd_log_scroll = n > MDBG_VIS ? n - MDBG_VIS : 0;
    mdbg_last_count = modd_log_count;
}

static void mdbg_status(void)
{
    if (modd_log_count != mdbg_last_count) {
        int was_bottom = mdbg_at_bottom();
        mdbg_last_count = modd_log_count;
        if (was_bottom) {
            uint32_t n = modd_log_count < MODD_LOG_LINES ? modd_log_count : MODD_LOG_LINES;
            modd_log_scroll = n > MDBG_VIS ? n - MDBG_VIS : 0;
        }
        nav_dirty = 1;
    }
}

static const modd_page_t PAGE_DEBUG = {
    .name   = "LOG",
    .title  = "Log",
    .view   = VIEW_GLO,
    .flags  = PAGEF_SYSTEM,
    .enter  = mdbg_enter,
    .draw   = mdbg_draw,
    .status = mdbg_status,
    .knob   = mdbg_knob,
};
