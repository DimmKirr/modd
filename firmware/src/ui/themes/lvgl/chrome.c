// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* What every page shares: the styles, the frame around the content, the cards, the
 * hints and the toasts. The same parts as the modd theme (ui/themes/modd/theme.c,
 * shell.c, toast.c), drawn by LVGL:
 *
 *   header  24   page title ............. WiFi bars, E(thernet), disk, page n/m
 *   body   188   the page's widgets; pages with cards start with a row of four (48)
 *   footer  28   [OCT- pill]        context        [OCT+ pill]
 *
 *   hints   GLO tap: the page's .help_text over the body, badges over the footer pills
 *   toast   toast_status / toast_error (modd_toast.h) over the footer, 3 s / 5 s
 *
 * A page is an LVGL screen. lvt_page_begin() builds the next one, shows it and deletes
 * the one before, so only the page on screen holds heap. Hints and toasts live on LVGL's
 * top layer, built once. Colours come from the palette (palette.c): every style here is
 * re-coloured in place when it changes, and LVGL redraws what uses it. */

#define LVT_HEAD_H 24
#define LVT_FOOT_H 28
#define LVT_CARDS_H 48
#define LVT_PANEL_Y (LVT_HEAD_H + LVT_CARDS_H + 10)    /* where the hints panel starts */

static lv_style_t lvt_st_scr;       /* the screen: background, text */
static lv_style_t lvt_st_head;      /* header strip, a rule under it */
static lv_style_t lvt_st_foot;      /* footer strip */
static lv_style_t lvt_st_pill;      /* an OCT-/OCT+ action in the footer ... */
static lv_style_t lvt_st_pill_hint; /* ... framed while the hints show (LV_STATE_CHECKED) */
static lv_style_t lvt_st_card;      /* a card */
static lv_style_t lvt_st_row_sel;   /* the selected row of a list (LV_STATE_CHECKED) */
static lv_style_t lvt_st_small;     /* captions, log lines */
static lv_style_t lvt_st_dim;
static lv_style_t lvt_st_mid;
static lv_style_t lvt_st_theme;     /* text in THEME, the selected track's colour */
static lv_style_t lvt_st_line;      /* a rule */
static lv_style_t lvt_st_panel;     /* hint panel, toast: an opaque box */

static lv_obj_t *lvt_scr;           /* the page on screen */

/* ------------------------------------------------------------- styles --- */
static void lvt_styles_apply(void)
{
    lv_style_set_bg_color(&lvt_st_scr, lvt_pal.bg);
    lv_style_set_text_color(&lvt_st_scr, lvt_pal.text);
    lv_style_set_bg_color(&lvt_st_head, lvt_pal.bg);
    lv_style_set_border_color(&lvt_st_head, lvt_pal.line);
    lv_style_set_bg_color(&lvt_st_foot, lvt_pal.surf);
    lv_style_set_bg_color(&lvt_st_pill, lvt_mix(lvt_pal.surf, lvt_pal.theme, 18));
    lv_style_set_text_color(&lvt_st_pill, lvt_pal.text);
    lv_style_set_border_color(&lvt_st_pill_hint, lvt_pal.accent);
    lv_style_set_bg_color(&lvt_st_card, lvt_pal.surf);
    lv_style_set_bg_color(&lvt_st_row_sel, lvt_pal.surf);
    lv_style_set_text_color(&lvt_st_row_sel, lvt_pal.theme);
    lv_style_set_text_color(&lvt_st_dim, lvt_pal.dim);
    lv_style_set_line_color(&lvt_st_dim, lvt_pal.dim);
    lv_style_set_text_color(&lvt_st_mid, lvt_pal.mid);
    lv_style_set_text_color(&lvt_st_theme, lvt_pal.theme);
    lv_style_set_bg_color(&lvt_st_line, lvt_pal.line);
    lv_style_set_bg_color(&lvt_st_panel, lvt_pal.bg);
    lv_style_set_border_color(&lvt_st_panel, lvt_pal.line);
    lv_style_set_text_color(&lvt_st_panel, lvt_pal.text);
}

/* LVGL's default theme in the palette's colours: switches, bars, lines */
static void lvt_theme_apply(void)
{
    lv_display_set_theme(lvt_disp, lv_theme_default_init(lvt_disp, lvt_pal.theme, lvt_pal.accent,
                                                         !lvt_pal.light, LV_FONT_DEFAULT));
}

/* the palette or the selected track changed: re-colour everything on screen */
static void lvt_pal_apply(void)
{
    lvt_styles_apply();
    lvt_theme_apply();                  /* reports the style change for every object */
    lv_obj_report_style_change(NULL);
}

static void lvt_styles_init(void)
{
    lv_style_init(&lvt_st_scr);
    lv_style_set_bg_opa(&lvt_st_scr, LV_OPA_COVER);
    lv_style_set_text_font(&lvt_st_scr, &lv_font_montserrat_14);

    lv_style_init(&lvt_st_head);
    lv_style_set_bg_opa(&lvt_st_head, LV_OPA_COVER);
    lv_style_set_border_width(&lvt_st_head, 1);
    lv_style_set_border_side(&lvt_st_head, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_pad_hor(&lvt_st_head, 6);
    lv_style_set_pad_column(&lvt_st_head, 5);

    lv_style_init(&lvt_st_foot);
    lv_style_set_bg_opa(&lvt_st_foot, LV_OPA_COVER);
    lv_style_set_pad_hor(&lvt_st_foot, 4);
    lv_style_set_pad_column(&lvt_st_foot, 4);

    lv_style_init(&lvt_st_pill);
    lv_style_set_bg_opa(&lvt_st_pill, LV_OPA_COVER);
    lv_style_set_radius(&lvt_st_pill, 4);
    lv_style_set_pad_hor(&lvt_st_pill, 6);
    lv_style_set_pad_ver(&lvt_st_pill, 2);
    lv_style_set_text_font(&lvt_st_pill, &lv_font_montserrat_10);

    lv_style_init(&lvt_st_pill_hint);
    lv_style_set_border_width(&lvt_st_pill_hint, 1);

    lv_style_init(&lvt_st_card);
    lv_style_set_bg_opa(&lvt_st_card, LV_OPA_COVER);
    lv_style_set_radius(&lvt_st_card, 4);
    lv_style_set_pad_all(&lvt_st_card, 3);
    lv_style_set_pad_row(&lvt_st_card, 1);

    lv_style_init(&lvt_st_row_sel);
    lv_style_set_bg_opa(&lvt_st_row_sel, LV_OPA_COVER);

    lv_style_init(&lvt_st_small);
    lv_style_set_text_font(&lvt_st_small, &lv_font_montserrat_10);
    lv_style_init(&lvt_st_dim);
    lv_style_init(&lvt_st_mid);
    lv_style_init(&lvt_st_theme);

    lv_style_init(&lvt_st_line);
    lv_style_set_bg_opa(&lvt_st_line, LV_OPA_COVER);

    lv_style_init(&lvt_st_panel);
    lv_style_set_bg_opa(&lvt_st_panel, LV_OPA_COVER);
    lv_style_set_border_width(&lvt_st_panel, 1);
    lv_style_set_radius(&lvt_st_panel, 6);
    lv_style_set_pad_all(&lvt_st_panel, 6);
    lv_style_set_pad_row(&lvt_st_panel, 2);

    lvt_styles_apply();
}

/* ------------------------------------------------------------ helpers --- */
/* a container that only lays out its children: none of the theme's card look */
static lv_obj_t *lvt_box(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *lvt_caption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_add_style(l, &lvt_st_small, 0);
    lv_obj_add_style(l, &lvt_st_dim, 0);
    lv_label_set_text_static(l, text);
    return l;
}

/* a rule across the parent */
static lv_obj_t *lvt_rule(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &lvt_st_line, 0);
    lv_obj_set_size(o, lv_pct(100), 1);
    return o;
}

/* a label's text, only when it differs: setting the same text still redraws it */
static void lvt_text(lv_obj_t *l, const char *s)
{
    const char *cur = lv_label_get_text(l);
    if (!s) s = "";
    if (cur && !lv_strcmp(cur, s))
        return;
    lv_label_set_text(l, s);
}

/* Local style setters redraw even when the value is the same; pages update every frame,
 * so these set only what changed. */
static void lvt_text_color(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), c))
        lv_obj_set_style_text_color(o, c, 0);
}

static void lvt_bg_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t part)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, part), c))
        lv_obj_set_style_bg_color(o, c, part);
}

static void lvt_width(lv_obj_t *o, int32_t w)
{
    if (lv_obj_get_style_width(o, 0) != w)
        lv_obj_set_width(o, w);
}

static void lvt_height(lv_obj_t *o, int32_t h)
{
    if (lv_obj_get_style_height(o, 0) != h)
        lv_obj_set_height(o, h);
}

/* a row of the list on screen: selected rows in the selection style */
static lv_obj_t *lvt_row(lv_obj_t *parent, uint32_t h)
{
    lv_obj_t *row = lvt_box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(row, (int32_t)h);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_radius(row, 3, 0);
    lv_obj_add_style(row, &lvt_st_row_sel, LV_STATE_CHECKED);
    return row;
}

/* ------------------------------------------------------------- cards --- */
/* Four cards across the top of the body, as the modd theme's ui_card(): a label, a value
 * and a gauge (0..1000, hidden below 0) in a colour. */
static struct {
    lv_obj_t *box, *label, *value, *unit, *gauge;
} lvt_card[4];

static lv_obj_t *lvt_cards(lv_obj_t *body)
{
    lv_obj_t *row = lvt_box(body, LV_FLEX_FLOW_ROW);
    uint32_t k;
    lv_obj_set_height(row, LVT_CARDS_H);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (k = 0; k < 4u; k++) {
        lv_obj_t *c = lvt_box(row, LV_FLEX_FLOW_COLUMN), *g;
        lv_obj_add_style(c, &lvt_st_card, 0);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_height(c, lv_pct(100));
        lvt_card[k].box = c;
        lvt_card[k].label = lv_label_create(c);
        lv_obj_add_style(lvt_card[k].label, &lvt_st_small, 0);
        lv_obj_add_style(lvt_card[k].label, &lvt_st_mid, 0);
        lv_label_set_text_static(lvt_card[k].label, "");
        g = lvt_box(c, LV_FLEX_FLOW_ROW);       /* the value, then its unit */
        lv_obj_set_style_pad_column(g, 2, 0);
        lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lvt_card[k].value = lv_label_create(g);
        lv_label_set_long_mode(lvt_card[k].value, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_max_width(lvt_card[k].value, 50, 0);
        lv_label_set_text_static(lvt_card[k].value, "");
        lvt_card[k].unit = lv_label_create(g);
        lv_obj_add_style(lvt_card[k].unit, &lvt_st_small, 0);
        lv_obj_add_style(lvt_card[k].unit, &lvt_st_mid, 0);
        lv_label_set_text_static(lvt_card[k].unit, "");
        g = lv_obj_create(c);           /* the gauge: a track and a fill */
        lv_obj_remove_style_all(g);
        lv_obj_add_style(g, &lvt_st_line, 0);
        lv_obj_set_size(g, lv_pct(100), 3);
        lv_obj_set_style_radius(g, 1, 0);
        lv_obj_set_scrollable(g, false);
        lv_obj_set_ignore_layout(g, true);
        lv_obj_align(g, LV_ALIGN_BOTTOM_MID, 0, 0);
        {
            lv_obj_t *f = lv_obj_create(g);
            lv_obj_remove_style_all(f);
            lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(f, 1, 0);
            lv_obj_set_height(f, 3);
            lv_obj_set_width(f, 0);
        }
        lvt_card[k].gauge = g;
    }
    return row;
}

/* `ratio` 0..1000 fills the gauge, below 0 hides it; `col` is the value's and the gauge's */
static void lvt_card_set(uint32_t k, const char *label, const char *value, const char *unit,
                         lv_color_t col, int32_t ratio)
{
    lv_obj_t *fill = lv_obj_get_child(lvt_card[k].gauge, 0);
    lvt_text(lvt_card[k].label, label);
    lvt_text(lvt_card[k].value, value);
    lvt_text(lvt_card[k].unit, unit);
    {   /* a long value in the small type, as the modd theme's ty_fit() */
        const lv_font_t *f = lv_strlen(value ? value : "") > 4u ? &lv_font_montserrat_10 : &lv_font_montserrat_14;
        if (lv_obj_get_style_text_font(lvt_card[k].value, 0) != f)
            lv_obj_set_style_text_font(lvt_card[k].value, f, 0);
    }
    lvt_text_color(lvt_card[k].value, col);
    lv_obj_set_hidden(lvt_card[k].gauge, ratio < 0);
    if (ratio >= 0) {
        if (ratio > 1000) ratio = 1000;
        lvt_bg_color(fill, col, 0);
        lvt_width(fill, lv_pct(ratio / 10));
    }
}

/* ------------------------------------------------------------- header --- */
static struct {
    lv_obj_t *title, *pos, *disk, *eth, *wifi, *bar[3];
    uint32_t  sig;                      /* what the icons show, in one number */
} lvt_head;

#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
static uint32_t lvt_wifi_bars(void)     /* as the modd theme's mt_wifi_bars() */
{
    int st = wifi_get_state();
    if (st == WIFI_STATE_CONNECTED) return 3;
    if (st == WIFI_STATE_WPA || st == WIFI_STATE_ASSOC) return 2;
    if (st == WIFI_STATE_AUTH || st == WIFI_STATE_SCANNING) return 1;
    return 0;
}
#endif

/* the icons follow the firmware: polled every frame, redrawn when they change */
static void lvt_head_poll(void)
{
    uint32_t sig = 0, k;
#ifdef CONFIG_USB_HOST
    if (udisk_mounted) sig |= 1u;
#endif
#ifdef CONFIG_USB_NET
    if (ecm_is_up()) sig |= net_get_ip() ? 6u : 2u;
#endif
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
    {
        uint32_t wb = lvt_wifi_bars();
        /* no icon once WiFi is switched off in Settings, though the radio stays up until reboot */
        if (wb > 0 || (wifi_get_state() != WIFI_STATE_OFF && sys_settings.wifi_on))
            sig |= 8u | (wb << 4);
    }
#endif
    sig |= (uint32_t)lvt_pal.idx << 8 | (uint32_t)lvt_pal.trk << 16;
    if (sig == lvt_head.sig || !lvt_head.title)
        return;
    lvt_head.sig = sig;
    lv_obj_set_hidden(lvt_head.disk, !(sig & 1u));
    lv_obj_set_hidden(lvt_head.eth, !(sig & 2u));
    lv_obj_set_style_text_color(lvt_head.eth, (sig & 4u) ? lvt_pal.text : lvt_pal.dim, 0);
    lv_obj_set_hidden(lvt_head.wifi, !(sig & 8u));
    for (k = 0; k < 3u; k++)
        lv_obj_set_style_bg_color(lvt_head.bar[k], k < ((sig >> 4) & 3u)
                                  ? (((sig >> 4) & 3u) == 3u ? lvt_pal.theme : lvt_pal.mid)
                                  : lvt_pal.line, 0);
}

static void lvt_head_build(lv_obj_t *scr, const char *title)
{
    lv_obj_t *bar = lvt_box(scr, LV_FLEX_FLOW_ROW), *l;
    uint32_t k;
    lv_obj_add_style(bar, &lvt_st_head, 0);
    lv_obj_set_height(bar, LVT_HEAD_H);

    l = lv_label_create(bar);
    lv_label_set_text_static(l, title);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lvt_head.title = l;

    lvt_head.wifi = lvt_box(bar, LV_FLEX_FLOW_ROW);     /* three bars, rising */
    lv_obj_set_size(lvt_head.wifi, LV_SIZE_CONTENT, 11);
    lv_obj_set_style_pad_column(lvt_head.wifi, 1, 0);
    lv_obj_set_flex_align(lvt_head.wifi, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (k = 0; k < 3u; k++) {
        lv_obj_t *b = lv_obj_create(lvt_head.wifi);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_size(b, 3, (int32_t)(4u + 3u * k));
        lvt_head.bar[k] = b;
    }
    lvt_head.eth = lv_label_create(bar);
    lv_obj_add_style(lvt_head.eth, &lvt_st_small, 0);
    lv_label_set_text_static(lvt_head.eth, "E");
    lvt_head.disk = lv_label_create(bar);
    lv_obj_add_style(lvt_head.disk, &lvt_st_small, 0);
    lv_label_set_text_static(lvt_head.disk, LV_SYMBOL_DRIVE);
    lvt_head.pos = lv_label_create(bar);
    lv_obj_add_style(lvt_head.pos, &lvt_st_small, 0);
    lv_obj_add_style(lvt_head.pos, &lvt_st_mid, 0);
    if (nav_npage > 1u)
        lv_label_set_text_fmt(lvt_head.pos, "%u/%u", (unsigned)(nav_cur + 1u), (unsigned)nav_npage);
    else
        lv_obj_set_hidden(lvt_head.pos, true);
    lvt_head.sig = 0xFFFFFFFFu;
    lvt_head_poll();
}

/* ------------------------------------------------------------- footer --- */
static struct {
    lv_obj_t *pill[2], *text[2], *center;
} lvt_foot;

static void lvt_hints_frame(void);

/* OCT- action, context, OCT+ action, as the modd theme's ui_footer(): an empty action
 * hides its pill. The strings must outlive the call or be copied: labels copy them. */
static void lvt_footer(const char *left, const char *center, const char *right)
{
    if (!lvt_foot.center)
        return;
    lvt_text(lvt_foot.text[0], left);
    lvt_text(lvt_foot.text[1], right);
    lvt_text(lvt_foot.center, center);
    lv_obj_set_hidden(lvt_foot.pill[0], !left || !*left);
    lv_obj_set_hidden(lvt_foot.pill[1], !right || !*right);
    lvt_hints_frame();
}

static lv_obj_t *lvt_pill(lv_obj_t *parent, uint32_t i)
{
    lv_obj_t *p = lvt_box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(p, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_style(p, &lvt_st_pill, 0);
    lv_obj_add_style(p, &lvt_st_pill_hint, LV_STATE_CHECKED);
    lvt_foot.text[i] = lv_label_create(p);
    lv_label_set_text_static(lvt_foot.text[i], "");
    lvt_foot.pill[i] = p;
    return p;
}

static void lvt_foot_build(lv_obj_t *scr)
{
    lv_obj_t *bar = lvt_box(scr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(bar, &lvt_st_foot, 0);
    lv_obj_set_height(bar, LVT_FOOT_H);
    lvt_pill(bar, 0);
    lvt_foot.center = lv_label_create(bar);
    lv_obj_add_style(lvt_foot.center, &lvt_st_small, 0);
    lv_obj_add_style(lvt_foot.center, &lvt_st_mid, 0);
    lv_obj_set_flex_grow(lvt_foot.center, 1);
    lv_obj_set_style_text_align(lvt_foot.center, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lvt_foot.center, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text_static(lvt_foot.center, "");
    lvt_pill(bar, 1);
    lvt_footer("", "", "");
}

/* -------------------------------------------------------------- hints --- */
/* GLO tap (shell.c) toggles hints_visible, a page change clears it (ui_nav.c). Shown: the
 * page's .help_text in a panel over the body, the pills framed and badged with their
 * buttons. Built once on the top layer; filled when shown. */
#define LVT_HINT_LINES 7u

static struct {
    lv_obj_t *panel, *line[LVT_HINT_LINES], *badge[2];
    uint8_t   shown;
} lvt_hint;

static void lvt_hints_init(void)
{
    lv_obj_t *top = lv_layer_top(), *p, *l;
    uint32_t i;
    p = lvt_box(top, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(p, &lvt_st_panel, 0);
    lv_obj_set_size(p, LVT_W - 8, LVT_H - LVT_PANEL_Y - LVT_FOOT_H - 4);
    lv_obj_set_pos(p, 4, LVT_PANEL_Y);
    l = lvt_caption(p, "PAGE CONTROLS");
    lvt_rule(p);
    for (i = 0; i < LVT_HINT_LINES; i++) {
        lvt_hint.line[i] = lv_label_create(p);
        lv_label_set_text_static(lvt_hint.line[i], "");
    }
    lv_obj_set_hidden(p, true);
    lvt_hint.panel = p;
    for (i = 0; i < 2u; i++) {
        l = lv_label_create(top);
        lv_obj_add_style(l, &lvt_st_small, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(l, 3, 0);
        lv_obj_set_style_pad_hor(l, 3, 0);
        lv_label_set_text_static(l, i ? "OCT+" : "OCT-");
        lv_obj_set_hidden(l, true);
        lvt_hint.badge[i] = l;
    }
    (void)l;
}

/* the footer pills' frames and badges follow hints_visible and the pills */
static void lvt_hints_frame(void)
{
    uint32_t i;
    if (!lvt_hint.panel || !lvt_foot.center)
        return;
    for (i = 0; i < 2u; i++) {
        bool on = hints_visible && !lv_obj_is_hidden(lvt_foot.pill[i]);
        lv_obj_set_state(lvt_foot.pill[i], LV_STATE_CHECKED, on);
        lv_obj_set_hidden(lvt_hint.badge[i], !on);
        if (on) {
            lv_obj_set_style_bg_color(lvt_hint.badge[i], lvt_pal.accent, 0);
            lv_obj_set_style_text_color(lvt_hint.badge[i], lvt_pal.bg, 0);
            lv_obj_align(lvt_hint.badge[i], i ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT,
                         i ? -4 : 4, -(LVT_FOOT_H - 2));
        }
    }
}

/* every frame: show or hide what hints_visible says, for the page on screen */
static void lvt_hints_poll(const modd_page_t *p)
{
    uint32_t i = 0;
    const char *s;
    uint8_t want = hints_visible ? 1u : 0u;
    if (want == lvt_hint.shown)
        return;
    lvt_hint.shown = want;
    lvt_hints_frame();
    if (!want || !p || !p->help_text) {
        lv_obj_set_hidden(lvt_hint.panel, true);
        return;
    }
    for (s = p->help_text; i < LVT_HINT_LINES; i++) {
        char line[40];
        uint32_t n = 0;
        while (*s && *s != '\n' && n < sizeof line - 1u)
            line[n++] = *s++;
        line[n] = 0;
        if (*s == '\n')
            s++;
        lv_label_set_text(lvt_hint.line[i], line);
        lv_obj_set_hidden(lvt_hint.line[i], n == 0);
    }
    lv_obj_set_hidden(lvt_hint.panel, false);
}

/* -------------------------------------------------------------- toast --- */
/* modd_toast.h, as the modd theme's toast.c: one slot, the newest wins. A status toast
 * (three dots running) goes after 3 s, an error ("!") after 5 s or the next button. */
#define TOAST_NONE   0
#define TOAST_STATUS 1
#define TOAST_ERROR  2
#define TOAST_STATUS_MS 3000u
#define TOAST_ERROR_MS  5000u
#define TOAST_DOT_MS    300u

static struct {
    const char *msg;
    uint8_t     kind, shown_kind, dot;
    uint32_t    start_ms, dot_ms;
    const char *shown_msg;
    lv_obj_t   *box, *mark, *dots[3], *text;
} toast;

static void toast_status(const char *msg)
{
    if (toast.kind == TOAST_STATUS && toast.msg == msg) return;
    toast.msg = msg;
    toast.kind = TOAST_STATUS;
    toast.start_ms = toast.dot_ms = fm1_ms;
    toast.dot = 0;
}

static void toast_error(const char *msg)
{
    toast.msg = msg;
    toast.kind = TOAST_ERROR;
    toast.start_ms = fm1_ms;
}

static void toast_clear(void)
{
    toast.kind = TOAST_NONE;
    toast.msg = 0;
}

static int toast_active(void) { return toast.kind != TOAST_NONE; }

static void lvt_toast_init(void)
{
    lv_obj_t *b = lvt_box(lv_layer_top(), LV_FLEX_FLOW_ROW);
    uint32_t i;
    lv_obj_add_style(b, &lvt_st_panel, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_style_pad_column(b, 4, 0);
    lv_obj_set_size(b, LVT_W, LVT_FOOT_H);
    lv_obj_set_pos(b, 0, LVT_H - LVT_FOOT_H);
    for (i = 0; i < 3u; i++) {
        lv_obj_t *d = lv_obj_create(b);
        lv_obj_remove_style_all(d);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(d, 7, 7);
        toast.dots[i] = d;
    }
    toast.mark = lv_label_create(b);
    lv_label_set_text_static(toast.mark, "!");
    toast.text = lv_label_create(b);
    lv_obj_add_style(toast.text, &lvt_st_small, 0);
    lv_obj_set_flex_grow(toast.text, 1);
    lv_label_set_long_mode(toast.text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_hidden(b, true);
    toast.box = b;
}

/* every frame: time out, run the dots, show what is raised */
static void lvt_toast_tick(void)
{
    uint32_t i;
    if (toast.kind != TOAST_NONE &&
        fm1_ms - toast.start_ms >= (toast.kind == TOAST_ERROR ? TOAST_ERROR_MS : TOAST_STATUS_MS))
        toast_clear();
    if (toast.kind == TOAST_STATUS && fm1_ms - toast.dot_ms >= TOAST_DOT_MS) {
        toast.dot = (uint8_t)((toast.dot + 1u) % 3u);
        toast.dot_ms = fm1_ms;
        toast.shown_kind = 0xFFu;               /* the dots move: show again */
    }
    if (toast.kind == toast.shown_kind && toast.msg == toast.shown_msg)
        return;
    toast.shown_kind = toast.kind;
    toast.shown_msg = toast.msg;
    if (toast.kind == TOAST_NONE || !toast.msg) {
        lv_obj_set_hidden(toast.box, true);
        return;
    }
    lv_obj_set_style_bg_color(toast.box, lvt_mix(lvt_pal.surf, lvt_pal.bg, 10), 0);
    for (i = 0; i < 3u; i++) {
        lv_obj_set_hidden(toast.dots[i], toast.kind != TOAST_STATUS);
        lv_obj_set_style_bg_color(toast.dots[i], i == toast.dot ? lvt_pal.theme : lvt_pal.dim, 0);
    }
    lv_obj_set_hidden(toast.mark, toast.kind != TOAST_ERROR);
    lv_obj_set_style_text_color(toast.mark, lvt_pal.rec, 0);
    lv_obj_set_style_text_color(toast.text, toast.kind == TOAST_ERROR ? lvt_pal.text : lvt_pal.mid, 0);
    lvt_text(toast.text, toast.msg);
    lv_obj_set_hidden(toast.box, false);
}

/* --------------------------------------------------------------- page --- */
/* Build the next page's screen and show it. Returns the body: the page adds its widgets
 * to it. The title is the page's (.title, else .name), as in the modd theme's header. */
static lv_obj_t *lvt_page_begin(void)
{
    const modd_page_t *p = nav_active();
    lv_obj_t *old = lvt_scr, *body;

    lvt_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(lvt_scr);
    lv_obj_add_style(lvt_scr, &lvt_st_scr, 0);
    lv_obj_set_flex_flow(lvt_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(lvt_scr, false);

    lvt_head_build(lvt_scr, p ? (p->title ? p->title : p->name) : "");

    body = lvt_box(lvt_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_all(body, 2, 0);
    lv_obj_set_style_pad_row(body, 2, 0);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lvt_foot_build(lvt_scr);

    lv_screen_load(lvt_scr);
    if (old)
        lv_obj_delete(old);
    lvt_hint.shown = 0xFFu;             /* follow hints_visible for the new page */
    return body;
}

/* the area under the cards, as the modd theme's panel: a column filling the rest */
static lv_obj_t *lvt_panel(lv_obj_t *body)
{
    lv_obj_t *p = lvt_box(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(p, 1);
    lv_obj_set_style_pad_hor(p, 4, 0);
    lv_obj_set_style_pad_top(p, 4, 0);
    lv_obj_set_style_pad_row(p, 2, 0);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return p;
}
