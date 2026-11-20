// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* 64-bit division for the compiler.
 *
 * pi32v2 clang turns a 64-bit `/` or `%` into a call to __udivdi3 and its relatives.
 * modd itself never divides 64-bit values and links no compiler runtime; LVGL does in
 * two places (its printf's long long conversion and image transforms), so the LVGL theme
 * brings these. Not static: the calls come from the LVGL library's objects too.
 *
 * Plain shift-and-subtract, one bit a round, with the CPU's own divider when both
 * operands fit 32 bits. Shifts are by constants only: a variable 64-bit shift would
 * itself be a runtime call. Dividing by zero returns 0 (C leaves it undefined).
 * Host test: tests/ui/themes/lvgl/rt64_test.py. */

unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long *rem)
{
    unsigned long long q = 0, r = 0;
    uint32_t i;
    if (d == 0) {
        if (rem)
            *rem = 0;
        return 0;
    }
    if ((n >> 32) == 0 && (d >> 32) == 0) {
        uint32_t a = (uint32_t)n, b = (uint32_t)d;
        if (rem)
            *rem = a % b;
        return a / b;
    }
    for (i = 0; i < 64u; i++) {
        r = (r << 1) | (n >> 63);
        n <<= 1;
        q <<= 1;
        if (r >= d) {
            r -= d;
            q |= 1u;
        }
    }
    if (rem)
        *rem = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long n, unsigned long long d)
{
    return __udivmoddi4(n, d, 0);
}

unsigned long long __umoddi3(unsigned long long n, unsigned long long d)
{
    unsigned long long r;
    __udivmoddi4(n, d, &r);
    return r;
}

/* signed: divide the magnitudes; the quotient is negative when the signs differ, the
 * remainder takes the dividend's sign (C99 truncation toward zero) */
long long __divdi3(long long n, long long d)
{
    unsigned long long un = n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n;
    unsigned long long ud = d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d;
    unsigned long long q = __udivmoddi4(un, ud, 0);
    return (long long)((n < 0) != (d < 0) ? 0ull - q : q);
}

long long __moddi3(long long n, long long d)
{
    unsigned long long un = n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n;
    unsigned long long ud = d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d;
    unsigned long long r;
    __udivmoddi4(un, ud, &r);
    return (long long)(n < 0 ? 0ull - r : r);
}
