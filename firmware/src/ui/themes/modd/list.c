// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd list widget: rows from a modd_list_src_t (modd_list.h) with a cursor, a scroll window and a
 * status line under the rows. Draws into the caller's canvas with the Melodee renderer (cv_*), so
 * it is included after gfx.c. Nothing here touches a disk: a source answers from its cache. */


static void ui_list_hints(const ui_list_t *l, char *out, uint32_t max);

static void ui_list_init(ui_list_t *l, const modd_list_src_t *src, uint8_t row_h)
{
    l->src = src;
    l->cursor = 0;
    l->scroll = 0;
    l->vis = 4;
    l->row_h = row_h;
    l->msg = 0;
    l->press_us = 0;
    l->press_id = 0;
}

static void ui_list_clamp(ui_list_t *l)
{
    uint32_t n = l->src->count();
    if (n == 0) { l->cursor = l->scroll = 0; return; }
    if (l->cursor >= n) l->cursor = n - 1u;
    if (l->cursor < l->scroll) l->scroll = l->cursor;
    if (l->vis && l->cursor >= l->scroll + l->vis) l->scroll = l->cursor - l->vis + 1u;
}

static uint16_t ui_list_detail_col(uint8_t kind)
{
    switch (kind) {
    case LIST_ACTIVE: return T_THEME;
    case LIST_INFO:   return T_ACCENT;
    default:          return T_DIM;
    }
}

/* rows from y0 over h pixels of the current canvas; the last row is the status line */
static void ui_list_draw(ui_list_t *l, int32_t y0, int32_t h)
{
    const modd_list_src_t *s = l->src;
    uint32_t n = s->count(), r, rows = (uint32_t)h / l->row_h;
    const char *status;
    if (rows == 0) return;
    l->vis = rows - 1u;
    if (l->vis == 0) l->vis = 1;
    ui_list_clamp(l);
    for (r = 0; r < l->vis && l->scroll + r < n; r++) {
        uint32_t idx = l->scroll + r;
        int32_t y = y0 + (int32_t)r * l->row_h;
        uint8_t sel = idx == l->cursor;
        uint16_t bg = sel ? T_SURF : T_BG;
        uint16_t fg = sel ? T_THEME : T_TEXT;
        modd_list_entry_t e = {"", "", LIST_FILE};
        int32_t x = UI_INSET;
        s->entry(idx, &e);
        if (sel)
            cv_rrect(2, y, UI_W - 4, l->row_h - 2, 4, T_SURF, T_BG);
        if (e.kind == LIST_FOLDER) {
            cv_text_fit(x, UI_TEXT_Y(y, l->row_h), TY_LB, "/", T_ACCENT, bg, 8);
            x += 8;
        }
        cv_text_fit(x, UI_TEXT_Y(y, l->row_h), TY_LB, e.name ? e.name : "", fg, bg, 150);
        if (e.detail && e.detail[0])
            cv_text_r(UI_W - UI_INSET, UI_TEXT_Y(y, l->row_h), TY_LB, e.detail, ui_list_detail_col(e.kind), bg);
    }
    if (n > l->vis) {                           /* the position mark: a thumb on the right edge */
        int32_t track = (int32_t)l->vis * l->row_h - 2, thumb, ty;
        thumb = track * (int32_t)l->vis / (int32_t)n;
        if (thumb < 6) thumb = 6;
        ty = (track - thumb) * (int32_t)l->scroll / (int32_t)(n - l->vis);
        cv_rect(UI_W - 3, y0, 2, track, T_LINE);
        cv_rect(UI_W - 3, y0 + ty, 2, thumb, T_MID);
    }
    status = l->msg ? l->msg : (s->status ? s->status() : 0);
    if (!status && n == 0)
        status = "(empty)";
    if (!status) {
        static char hints[48];
        ui_list_hints(l, hints, sizeof hints);
        status = hints;
    }
    if (status[0])
        cv_text_c(UI_W / 2, y0 + (int32_t)l->vis * l->row_h + 2, TY_LB, status,
                  l->msg ? T_THEME : T_DIM, T_BG);
}

static void ui_list_knob(ui_list_t *l, int32_t clicks)
{
    uint32_t n = l->src->count();
    int32_t c = (int32_t)l->cursor + clicks;
    if (c < 0) c = 0;
    if (n && (uint32_t)c >= n) c = (int32_t)(n - 1u);
    l->cursor = n ? (uint32_t)c : 0;
    l->msg = 0;
    ui_list_clamp(l);
    nav_dirty = 1;
}

static const char *const UI_LIST_GLYPH[LIST_N_ACTIONS] = {"-", "--", "+", "++"};
static const char *const UI_LIST_LABEL[LIST_N_ACTIONS] = {"back", 0, "select", 0};

static int ui_list_up(ui_list_t *l);

/* the hint line: every slot that does something, "glyph label" */
static void ui_list_hints(const ui_list_t *l, char *out, uint32_t max)
{
    const modd_list_src_t *s = l->src;
    uint32_t n = 0, a;
    for (a = 0; a < LIST_N_ACTIONS; a++) {
        const modd_list_action_t *act = &s->actions[a];
        const char *g = act->glyph ? act->glyph : UI_LIST_GLYPH[a];
        const char *t = act->label ? act->label : UI_LIST_LABEL[a];
        const char *p;
        int bound = act->fn != 0 || (a == LIST_SHORT_PLUS && s->enter) || (a == LIST_SHORT_MINUS && s->up);
        if (!bound || !t) continue;
        if (n && n + 2u < max) { out[n++] = ' '; out[n++] = ' '; }
        for (p = g; *p && n + 1u < max; p++) out[n++] = *p;
        for (p = t; *p && n + 1u < max; p++) out[n++] = *p;
    }
    out[n] = 0;
}

/* run slot a on the cursor's entry; 1 when something happened */
static int ui_list_run(ui_list_t *l, uint32_t a)
{
    const modd_list_src_t *s = l->src;
    const modd_list_action_t *act = &s->actions[a];
    uint32_t n = s->count();
    if (act->fn) {
        if (n == 0) return 1;
        l->msg = act->fn(l->cursor);
        ui_list_clamp(l);
        nav_dirty = 1;
        return 1;
    }
    if (a == LIST_SHORT_PLUS && s->enter && n) {
        modd_list_entry_t e = {"", "", LIST_FILE};
        s->entry(l->cursor, &e);
        if (e.kind == LIST_FOLDER && s->enter(l->cursor) == 0) {
            l->cursor = l->scroll = 0;
            l->msg = 0;
            nav_dirty = 1;
            return 1;
        }
        return 0;
    }
    if (a == LIST_SHORT_MINUS)
        return ui_list_up(l);
    return 0;
}

static void ui_list_act(ui_list_t *l) { (void)ui_list_run(l, LIST_SHORT_PLUS); }

/* OCT- / OCT+ down: remembered; up: the slot by hold time (modd_controls.hold_us). 1 when
 * taken; 0 on a release nothing was bound to, so the page may close or navigate. */
static int ui_list_button(ui_list_t *l, uint32_t id, uint32_t down)
{
    uint32_t held, a;
    if (id != BTN_OCTUP && id != BTN_OCTDN)
        return 0;
    if (down) {
        l->press_us = fm1_micros() ? fm1_micros() : 1u;
        l->press_id = (uint8_t)id;
        return 1;
    }
    if (!l->press_us || l->press_id != id)
        return 0;
    held = fm1_micros() - l->press_us;
    l->press_us = 0;
    a = id == BTN_OCTUP ? LIST_SHORT_PLUS : LIST_SHORT_MINUS;
    if (held > modd_controls.hold_us && l->src->actions[a + 1u].fn)
        a += 1u;                                /* the long slot, when bound */
    return ui_list_run(l, a);
}

static int ui_list_up(ui_list_t *l)                   /* OCT-: 1 when the source went up a level */
{
    if (l->src->up && l->src->up() == 0) {
        l->cursor = l->scroll = 0;
        l->msg = 0;
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static void ui_list_open(ui_list_t *l)
{
    if (l->src->open) l->src->open();
    l->msg = 0;
    ui_list_clamp(l);
}

static void ui_list_close(ui_list_t *l)
{
    if (l->src->close) l->src->close();
}

static void ui_list_poll(ui_list_t *l)                /* every frame, from the page's status hook */
{
    if (l->src->poll && l->src->poll()) {
        ui_list_clamp(l);
        nav_dirty = 1;
    }
}
