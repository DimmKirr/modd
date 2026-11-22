// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test of the 64-bit division helpers LVGL needs
 * (firmware/src/ui/shells/modd/rt64.c) against the host's own `/` and `%`.
 * The helpers are renamed: the host's compiler runtime may have these names too. */
#include <stdint.h>
#include <stdio.h>

#define __udivmoddi4 t_udivmoddi4
#define __udivdi3    t_udivdi3
#define __umoddi3    t_umoddi3
#define __divdi3     t_divdi3
#define __moddi3     t_moddi3
#include "ui/shells/modd/rt64.c"

static int fails;

static void check_u(uint64_t n, uint64_t d)
{
    unsigned long long r = 12345;
    uint64_t q = t_udivmoddi4(n, d, &r);
    uint64_t eq = d ? n / d : 0, er = d ? n % d : 0;
    if (q != eq || r != er || t_udivdi3(n, d) != eq || t_umoddi3(n, d) != er ||
        t_udivmoddi4(n, d, 0) != eq) {
        if (fails++ < 10)
            printf("FAIL u %llx / %llx: q %llx r %llx, want %llx %llx\n",
                   (unsigned long long)n, (unsigned long long)d,
                   (unsigned long long)q, r, (unsigned long long)eq, (unsigned long long)er);
    }
}

static void check_s(int64_t n, int64_t d)
{
    int64_t eq, er;
    if (d == 0) {
        eq = 0; er = 0;
    } else if (n == INT64_MIN && d == -1) {
        eq = INT64_MIN; er = 0;         /* overflows in C; the helpers wrap, as the CPU would */
    } else {
        eq = n / d; er = n % d;
    }
    if (t_divdi3(n, d) != eq || t_moddi3(n, d) != er) {
        if (fails++ < 10)
            printf("FAIL s %lld / %lld: q %lld r %lld, want %lld %lld\n",
                   (long long)n, (long long)d, (long long)t_divdi3(n, d),
                   (long long)t_moddi3(n, d), (long long)eq, (long long)er);
    }
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint64_t rng(void)               /* xorshift64 */
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

int main(void)
{
    static const uint64_t edge[] = {
        0, 1, 2, 3, 7, 10, 1000, 0x7fffffffull, 0x80000000ull, 0xffffffffull,
        0x100000000ull, 0x100000001ull, 0x1ffffffffull, 0x123456789abcdefull,
        0x7fffffffffffffffull, 0x8000000000000000ull, 0x8000000000000001ull,
        0xfffffffffffffffeull, 0xffffffffffffffffull,
    };
    const unsigned n_edge = sizeof edge / sizeof edge[0];
    unsigned i, j;
    long k;

    for (i = 0; i < n_edge; i++)
        for (j = 0; j < n_edge; j++) {
            check_u(edge[i], edge[j]);
            check_s((int64_t)edge[i], (int64_t)edge[j]);
        }
    /* random operands of every width: both the 32-bit fast path and the long way */
    for (k = 0; k < 400000; k++) {
        uint64_t n = rng() >> (rng() & 63), d = rng() >> (rng() & 63);
        check_u(n, d);
        check_s((int64_t)n, (int64_t)d);
        check_s(-(int64_t)(n >> 1), (int64_t)(d >> 1));
        check_s((int64_t)(n >> 1), -(int64_t)(d >> 1));
        check_s(-(int64_t)(n >> 1), -(int64_t)(d >> 1));
    }
    if (fails) {
        printf("%d failed\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
