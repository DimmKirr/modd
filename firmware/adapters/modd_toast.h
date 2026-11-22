// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Toast notification API.
 * Subsystem files call these; the modd theme defines them in chrome.c.
 * Without it, the stubs compile to nothing. */
#ifndef MODD_TOAST_H
#define MODD_TOAST_H

#if defined(CONFIG_UI_THEME_MODD) || defined(CONFIG_UI_THEME_FELUCCA)
static void toast_status(const char *msg);
static void toast_error(const char *msg);
static void toast_clear(void);
static int  toast_active(void);
#else
static inline void toast_status(const char *msg) { (void)msg; }
static inline void toast_error(const char *msg) { (void)msg; }
static inline void toast_clear(void) {}
static inline int  toast_active(void) { return 0; }
#endif

#endif
