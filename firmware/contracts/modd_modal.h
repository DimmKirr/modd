// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Modal overlay contract: a temporary UI that intercepts shell dispatch.
 * The keyboard, confirm dialogs, and error popups implement this.
 * When active, the modal draws on top of the current page and receives
 * all input before the page does. */
#ifndef MODD_MODAL_H
#define MODD_MODAL_H
#include <stdint.h>

typedef struct modd_modal {
    void (*draw)(void);
    int  (*knob)(uint32_t id, int32_t clicks);
    int  (*button)(uint32_t id, uint32_t down);
    int  (*key)(uint32_t note, uint32_t vel);
    void (*open)(void);
    void (*close)(void);
} modd_modal_t;

#endif /* MODD_MODAL_H */
