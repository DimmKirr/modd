// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* The <inttypes.h> format macros LVGL's headers ask for (lv_conf.h LV_INTTYPES_INCLUDE).
 *
 * modd.c is compiled without a C library's headers: the compiler's own <inttypes.h> only
 * forwards to the library's, which is not on its include path. LVGL formats with its own
 * lv_snprintf (LV_STDLIB_BUILTIN), so all these have to match is the width of the types
 * on pi32v2: int and long are 32 bits, long long is 64, pointers are 32. */
#ifndef LVT_INTTYPES_H
#define LVT_INTTYPES_H

#include <stdint.h>

#define PRId8 "d"
#define PRIi8 "i"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRIX8 "X"
#define PRId16 "d"
#define PRIi16 "i"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRIX16 "X"
#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"
#define PRId64 "lld"
#define PRIi64 "lli"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIX64 "llX"
#define PRIdMAX "lld"
#define PRIiMAX "lli"
#define PRIuMAX "llu"
#define PRIxMAX "llx"
#define PRIXMAX "llX"
#define PRIdPTR "d"
#define PRIiPTR "i"
#define PRIuPTR "u"
#define PRIxPTR "x"
#define PRIXPTR "X"

#endif
