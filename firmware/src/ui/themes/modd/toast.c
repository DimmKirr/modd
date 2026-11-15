// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Toast notification overlay for the modd theme.
 * Single slot, newest wins. Draws over the footer area (y=UI_Y_FOOT). */

#define TOAST_NONE   0
#define TOAST_STATUS 1
#define TOAST_ERROR  2

#define TOAST_STATUS_MS 3000u
#define TOAST_ERROR_MS  5000u
#define TOAST_DOT_MS    300u

static struct {
    const char *msg;
    uint8_t     kind;
    uint32_t    start_ms;
    uint32_t    dot_ms;
    uint8_t     dot_phase;
} toast;

static void toast_status(const char *msg)
{
    if (toast.kind == TOAST_STATUS && toast.msg == msg) return;
    toast.msg = msg;
    toast.kind = TOAST_STATUS;
    toast.start_ms = fm1_ms;
    toast.dot_ms = fm1_ms;
    toast.dot_phase = 0;
    nav_dirty = 1;
}

static void toast_error(const char *msg)
{
    toast.msg = msg;
    toast.kind = TOAST_ERROR;
    toast.start_ms = fm1_ms;
    nav_dirty = 1;
}

static void toast_clear(void)
{
    if (toast.kind == TOAST_NONE) return;
    toast.kind = TOAST_NONE;
    toast.msg = 0;
    nav_dirty = 1;
}

static int toast_active(void)
{
    return toast.kind != TOAST_NONE;
}

static void toast_tick(void)
{
    if (toast.kind == TOAST_NONE) return;

    if (toast.kind == TOAST_STATUS &&
        (fm1_ms - toast.start_ms) >= TOAST_STATUS_MS) {
        toast_clear();
        return;
    }

    if (toast.kind == TOAST_ERROR &&
        (fm1_ms - toast.start_ms) >= TOAST_ERROR_MS) {
        toast_clear();
        return;
    }

    if (toast.kind == TOAST_STATUS &&
        (fm1_ms - toast.dot_ms) >= TOAST_DOT_MS) {
        toast.dot_phase = (toast.dot_phase + 1u) % 3u;
        toast.dot_ms = fm1_ms;
        nav_dirty = 1;
    }
}

static void toast_draw(void)
{
    if (toast.kind == TOAST_NONE || !toast.msg) return;

    uint16_t pill_bg = ux_mix(T_SURF, T_BG, 10);
    int32_t tw;

    cv_begin(UI_W, UI_FOOT_H, pill_bg);
    cv_rrect(0, 0, UI_W, UI_FOOT_H, 6, pill_bg, pill_bg);

    if (toast.kind == TOAST_STATUS) {
        int32_t dx = 14, dot_y = UI_FOOT_H / 2;
        uint32_t i;
        for (i = 0; i < 3u; i++) {
            uint16_t c = (i == toast.dot_phase) ? T_THEME : T_DIM;
            cv_circle(dx + (int32_t)i * 8, dot_y, 4, c, pill_bg);
        }
        tw = 14 + 3 * 8 + 6;
    } else {
        cv_text(10, ty_y(UI_FOOT_H / 2, TY_LB), TY_LB, "!", T_REC);
        tw = 10 + text_w(TY_LB, "!") + 4;
    }

    cv_text_fit(tw, ty_y(UI_FOOT_H / 2, TY_LB), TY_LB, toast.msg,
                toast.kind == TOAST_ERROR ? T_TEXT : T_MID,
                pill_bg, UI_W - tw - 8);

    cv_blit(0, UI_Y_FOOT);
}
