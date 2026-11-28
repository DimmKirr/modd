// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

/* Shim string.h for lwIP and FatFs: declares the mem/str functions the firmware
 * provides in libc.c with matching signatures (unsigned, not size_t).
 * Placed on the -I path ahead of the JieLi toolchain sysroot. */
#ifndef MODD_STRING_H
#define MODD_STRING_H

void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int   memcmp(const void *a, const void *b, unsigned n);
void *memmove(void *d, const void *s, unsigned n);
unsigned strlen(const char *s);
int strncmp(const char *a, const char *b, unsigned n);
char *strchr(const char *s, int c);

#endif /* MODD_STRING_H */
