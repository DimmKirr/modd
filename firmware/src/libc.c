/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/libc.c @ v1.6.1 (165d177a425944f3d30b0b669255a6ed0050c3a5)
 *         https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/libc.c */
/* Freestanding libc: upstream file included unchanged, then modd's extras
 * (memmove, strlen, strncmp, strchr) that the compiler or vendored libraries need. */
#include "upstream/melodee/firmware/src/libc.c"

void *memmove(void *d, const void *s, unsigned n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp < sp) {
        while (n--) *dp++ = *sp++;
    } else {
        dp += n; sp += n;
        while (n--) *--dp = *--sp;
    }
    return d;
}

unsigned strlen(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

int strncmp(const char *a, const char *b, unsigned n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

/* Used by the vendored FatFs (ff.c). c == 0 finds the terminator. */
char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c) return (char *)s;
    return c ? 0 : (char *)s;
}
