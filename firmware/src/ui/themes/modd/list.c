// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* The list widget: rows from a modd_list_src_t (modd_list.h) with a cursor, a window and a
 * status line under the rows, built from LVGL
 * labels. FILES shows LIST_UDISK with it, Settings > INSTR shows LIST_INSTR (CONFIG_DOCK).
 *
 *   rows     a window of `vis` entries; the cursor's row raised; a folder marked "/";
 *            the detail right, in THEME (active), ACCENT (info) or DIM
 *   status   the last action's message (THEME), else the source's status, "(empty)", or
 *            the bound controls ("+ok  -back")
 *
 * The page gives it one encoder (enc) and OCT- / OCT+: tapped or held past
 * modd_controls.hold_us they run the source's four action slots. The rows are labels made
 * once: a long list costs no LVGL heap. */

#define LVT_LIST_ROWS_MAX 9u

typedef struct {
    const modd_list_src_t *src;
    uint32_t cursor, top, vis;
    uint8_t  enc, press_id, dirty;
    uint32_t press_us;                  /* an OCT button went down (0: none) */
    const char *msg;                    /* the last action's message, until the next move */
    lv_obj_t *row[LVT_LIST_ROWS_MAX], *name[LVT_LIST_ROWS_MAX], *detail[LVT_LIST_ROWS_MAX];
    lv_obj_t *status;
} lvt_list_t;

static void lvt_list_init(lvt_list_t *l, const modd_list_src_t *src, uint8_t enc)
{
    lv_memzero(l, sizeof *l);
    l->src = src;
    l->enc = enc;
}

/* `vis` rows of `row_h` and the status line, into `parent` (a column) */
static void lvt_list_build(lvt_list_t *l, lv_obj_t *parent, uint32_t vis, uint32_t row_h)
{
    uint32_t i;
    l->vis = vis < LVT_LIST_ROWS_MAX ? vis : LVT_LIST_ROWS_MAX;
    for (i = 0; i < l->vis; i++) {
        lv_obj_t *row = lvt_row(parent, row_h), *t;
        t = lv_label_create(row);
        lv_obj_set_flex_grow(t, 1);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text_static(t, "");
        l->name[i] = t;
        t = lv_label_create(row);
        lv_obj_add_style(t, &lvt_st_small, 0);
        lv_label_set_text_static(t, "");
        l->detail[i] = t;
        l->row[i] = row;
    }
    l->status = lv_label_create(parent);
    lv_obj_add_style(l->status, &lvt_st_small, 0);
    lv_obj_set_width(l->status, lv_pct(100));
    lv_obj_set_style_text_align(l->status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(l->status, "");
    l->dirty = 1;
}

static void lvt_list_clamp(lvt_list_t *l)
{
    uint32_t n = l->src->count();
    if (n == 0) { l->cursor = l->top = 0; return; }
    if (l->cursor >= n) l->cursor = n - 1u;
    if (l->cursor < l->top) l->top = l->cursor;
    if (l->vis && l->cursor >= l->top + l->vis) l->top = l->cursor - l->vis + 1u;
}

static const char *const LVT_LIST_GLYPH[LIST_N_ACTIONS] = {"-", "--", "+", "++"};
static const char *const LVT_LIST_LABEL[LIST_N_ACTIONS] = {"back", 0, "ok", 0};

/* every slot that does something, "glyph label", as ui_list_hints() */
static void lvt_list_hints(const lvt_list_t *l, char *out, uint32_t max)
{
    const modd_list_src_t *s = l->src;
    uint32_t n = 0, a;
    for (a = 0; a < LIST_N_ACTIONS; a++) {
        const modd_list_action_t *act = &s->actions[a];
        const char *g = act->glyph ? act->glyph : LVT_LIST_GLYPH[a];
        const char *t = act->label ? act->label : LVT_LIST_LABEL[a];
        const char *p;
        int bound = act->fn != 0 || (a == LIST_SHORT_PLUS && s->enter) || (a == LIST_SHORT_MINUS && s->up);
        if (!bound || !t) continue;
        if (n && n + 2u < max) { out[n++] = ' '; out[n++] = ' '; }
        for (p = g; *p && n + 1u < max; p++) out[n++] = *p;
        for (p = t; *p && n + 1u < max; p++) out[n++] = *p;
    }
    out[n] = 0;
}

static void lvt_list_show(lvt_list_t *l)
{
    const modd_list_src_t *s = l->src;
    uint32_t n = s->count(), r;
    const char *st;
    char hints[48];
    lvt_list_clamp(l);
    for (r = 0; r < l->vis; r++) {
        uint32_t idx = l->top + r;
        modd_list_entry_t e = {"", "", LIST_FILE};
        char b[72];
        if (idx >= n) {
            lv_obj_set_hidden(l->row[r], true);
            continue;
        }
        s->entry(idx, &e);
        lv_obj_set_hidden(l->row[r], false);
        lv_obj_set_state(l->row[r], LV_STATE_CHECKED, idx == l->cursor);
        lv_snprintf(b, sizeof b, "%s%s", e.kind == LIST_FOLDER ? "/" : "", e.name ? e.name : "");
        lvt_text(l->name[r], b);
        lvt_text(l->detail[r], e.detail ? e.detail : "");
        lvt_text_color(l->detail[r], e.kind == LIST_ACTIVE ? lvt_pal.theme
                                     : e.kind == LIST_INFO ? lvt_pal.accent : lvt_pal.dim);
    }
    st = l->msg ? l->msg : (s->status ? s->status() : 0);
    if (!st && n == 0)
        st = "(empty)";
    if (!st) {
        lvt_list_hints(l, hints, sizeof hints);
        st = hints;
    }
    lvt_text(l->status, st);
    lvt_text_color(l->status, l->msg ? lvt_pal.theme : lvt_pal.dim);
    l->dirty = 0;
}

static void lvt_list_open(lvt_list_t *l)
{
    if (l->src->open) l->src->open();
    l->msg = 0;
    l->dirty = 1;
}

static void lvt_list_close(lvt_list_t *l)
{
    if (l->src->close) l->src->close();
}

/* every frame, from the page's status: follow the source, show what changed */
static void lvt_list_poll(lvt_list_t *l)
{
    if (l->src->poll && l->src->poll())
        l->dirty = 1;
    if (l->dirty && l->status)
        lvt_list_show(l);
}

/* the list's encoder moves the cursor; 1 when taken */
static int lvt_list_input(lvt_list_t *l, uint32_t id, int32_t clicks)
{
    uint32_t n;
    int32_t c;
    if (l->enc == ENC_NONE || id != l->enc)
        return 0;
    n = l->src->count();
    c = (int32_t)l->cursor + clicks;
    if (c < 0) c = 0;
    if (n && (uint32_t)c >= n) c = (int32_t)(n - 1u);
    l->cursor = n ? (uint32_t)c : 0;
    l->msg = 0;
    l->dirty = 1;
    return 1;
}

static int lvt_list_up(lvt_list_t *l)            /* OCT-: 1 when the source went up a level */
{
    if (l->src->up && l->src->up() == 0) {
        l->cursor = l->top = 0;
        l->msg = 0;
        l->dirty = 1;
        return 1;
    }
    return 0;
}

/* run slot `a` on the cursor's entry; 0 when nothing is bound (the page may close) */
static int lvt_list_run(lvt_list_t *l, uint32_t a)
{
    const modd_list_src_t *s = l->src;
    const modd_list_action_t *act = &s->actions[a];
    uint32_t n = s->count();
    if (act->fn) {
        if (n == 0) return 1;
        l->msg = act->fn(l->cursor);
        l->dirty = 1;
        return 1;
    }
    if (a == LIST_SHORT_PLUS && s->enter && n) {
        modd_list_entry_t e = {"", "", LIST_FILE};
        s->entry(l->cursor, &e);
        if (e.kind == LIST_FOLDER && s->enter(l->cursor) == 0) {
            l->cursor = l->top = 0;
            l->msg = 0;
            l->dirty = 1;
            return 1;
        }
        return 0;
    }
    if (a == LIST_SHORT_MINUS)
        return lvt_list_up(l);
    return 0;
}

/* OCT- / OCT+ down: remembered; up: the slot by hold time. 1 when taken; 0 on a release
 * nothing was bound to, so the page may close or navigate. */
static int lvt_list_button(lvt_list_t *l, uint32_t id, uint32_t down)
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
    return lvt_list_run(l, a);
}
