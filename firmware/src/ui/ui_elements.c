// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Element dispatch: routes element calls to the active shell.
 * Included in the unity build after ui_layout.h and before the shell. */

static const ui_layout_t *ui_active_layout;

static void ui_set_layout(const ui_layout_t *t) { ui_active_layout = t; }

static void ui_header(void)
{
    if (ui_active_layout && ui_active_layout->render_header)
        ui_active_layout->render_header();
}

static void ui_footer(const char *left, const char *center, const char *right)
{
    if (toast_active()) return;
    if (ui_active_layout && ui_active_layout->render_footer)
        ui_active_layout->render_footer(left, center, right);
}

static void ui_card(uint32_t slot, const char *label, const char *val,
                    const char *unit, uint16_t color, int32_t ratio,
                    uint32_t icon)
{
    if (ui_active_layout && ui_active_layout->render_card)
        ui_active_layout->render_card(slot, label, val, unit, color, ratio, icon);
}
