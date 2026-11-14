// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* List adapter: the contract between a scrolling-list page and what it lists. A source keeps
 * its entries in RAM (filled on open / poll, never while drawing or scrolling) and answers in
 * constant time; the widget (ui/themes/modd/list.c) owns the cursor, the window and the status
 * line. Sources: a USB disk directory (udisk.c), the dock instruments (dock/dock_list.c), later
 * an HTTP index. */
#ifndef MODD_LIST_H
#define MODD_LIST_H
#include <stdint.h>

enum { LIST_FILE, LIST_FOLDER, LIST_INSTALLED, LIST_ACTIVE, LIST_REMOTE, LIST_INFO };

typedef struct {
    const char *name;                           /* points into the source's cache */
    const char *detail;                         /* right column: "12K", "FLASH", "DISK", "" */
    uint8_t kind;                               /* LIST_*: colours the detail */
} modd_list_entry_t;

/* the four controls a list page has: OCT- and OCT+, tapped or held. A source binds what it
 * needs; an unbound short+ opens a folder ("select"), an unbound short- goes up ("back"), and
 * when there is nothing to go up to the page gets the release (it closes). glyph and label
 * make the hint line; 0 glyph: the slot's default ("-", "--", "+", "++"). */
enum { LIST_SHORT_MINUS, LIST_LONG_MINUS, LIST_SHORT_PLUS, LIST_LONG_PLUS, LIST_N_ACTIONS };
typedef struct {
    const char *glyph;
    const char *label;
    const char *(*fn)(uint32_t i);              /* the action on entry i; a status message or 0 */
} modd_list_action_t;

typedef struct modd_list_src {
    uint32_t (*count)(void);
    void (*entry)(uint32_t i, modd_list_entry_t *e);
    int (*enter)(uint32_t i);                   /* a folder: 0 entered; 0 fn: flat */
    int (*up)(void);                            /* 0 went up, -1 at the top; 0 fn: flat */
    modd_list_action_t actions[LIST_N_ACTIONS];
    const char *(*status)(void);                /* the status line when no message is pending; 0 none */
    const char *(*title)(void);                 /* a path bar; 0 none */
    void (*open)(void);                         /* the page shows it: resources on, cache filled */
    void (*close)(void);                        /* the page leaves: resources off, cache kept */
    int (*poll)(void);                          /* every frame: 1 when the entries changed */
} modd_list_src_t;

typedef struct {
    const modd_list_src_t *src;
    uint32_t cursor, scroll;
    uint32_t vis;                               /* rows the last draw had room for */
    uint8_t row_h;
    const char *msg;                            /* the last action's message, until the next move */
    uint32_t press_us;                          /* an OCT button went down (0: none) */
    uint8_t press_id;
} ui_list_t;

#endif /* MODD_LIST_H */
