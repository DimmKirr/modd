// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Layout descriptor: a shell's visual identity.
 * Each shell provides one static ui_layout_t with its layout metrics,
 * font pointers, and element render functions. The element layer
 * dispatches through whichever layout is active. */
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H
#include <stdint.h>

typedef struct ui_layout {
    uint16_t screen_w, screen_h;
    uint16_t header_h, footer_h;
    uint16_t card_h, card_w;

    const void *font_s;
    const void *font_m;
    const void *font_l;

    void (*render_header)(void);
    void (*render_footer)(const char *left, const char *center, const char *right);
    void (*render_card)(uint32_t slot, const char *label, const char *val,
                        const char *unit, uint16_t color, int32_t ratio,
                        uint32_t icon);
} ui_layout_t;

#endif /* UI_LAYOUT_H */
