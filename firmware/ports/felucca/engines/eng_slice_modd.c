/* SPDX-License-Identifier: GPL-3.0-only */
/* Origin: hugelton/Felucca firmware/src/eng_slice.c @ v1.8.2 (d0260074)
 * Wrapper: bridges symbols that Felucca's audio core defines after engines.c
 * in modd's unity build (RING_PUBLISH, ENV_EXP). */

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define _SLICE_RING_PUBLISH_LOCAL
#endif

/* ENV_EXP comes from upstream's generated felucca_tables.h (included earlier in modd.c). */

#undef slc_user_scan

#include "upstream/felucca/firmware/src/eng_slice.c"

#ifdef _SLICE_RING_PUBLISH_LOCAL
#undef RING_PUBLISH
#undef _SLICE_RING_PUBLISH_LOCAL
#endif
