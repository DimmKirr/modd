/* SPDX-License-Identifier: GPL-3.0-only */
/* Origin: hugelton/Felucca firmware/src/eng_sample.c @ v1.8.2 (d0260074)
 * Wrapper: include upstream eng_sample.c with modd's fm1_xip.h already in
 * scope.  The upstream file unconditionally includes ../bsp/chip/fm1_xip.h
 * which redefines fm1_xip_ptr.  Temporarily hide the function name so the
 * duplicate definition compiles as a harmless renamed copy. */
#define fm1_xip_ptr _felucca_fm1_xip_ptr
#include "upstream/felucca/firmware/src/eng_sample.c"
#undef fm1_xip_ptr
