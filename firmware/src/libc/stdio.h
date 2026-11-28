// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

/* Bare-metal stub: lwIP 2.2.1 mem.c includes <stdio.h> unconditionally
 * but only uses snprintf inside MEM_SANITY_REGION guards (disabled). */
#ifndef _STDIO_H_STUB
#define _STDIO_H_STUB
int snprintf(char *, unsigned long, const char *, ...);
#endif
