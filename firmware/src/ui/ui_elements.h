// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Element API: shell-agnostic drawing primitives.
 * Pages call these functions; the active shell's renderers do the work.
 * Each function dispatches through the registered ui_layout_t. */
#ifndef UI_ELEMENTS_H
#define UI_ELEMENTS_H
#include <stdint.h>

void ui_header(void);
void ui_footer(const char *left, const char *center, const char *right);
void ui_card(uint32_t slot, const char *label, const char *val,
             const char *unit, uint16_t color, int32_t ratio, uint32_t icon);

#endif /* UI_ELEMENTS_H */
