/* SPDX-License-Identifier: GPL-3.0-only */
/* Origin: hugelton/Felucca firmware/src/eng_slice.c @ v1.6.1 (a53ecf42)
 * Wrapper: bridges symbols that Felucca's audio core defines after engines.c
 * in modd's unity build (RING_PUBLISH, ENV_EXP). */

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define _SLICE_RING_PUBLISH_LOCAL
#endif

/* ENV_EXP[i]: Q16 exponential decay coefficient for envelope time index i.
 * Generated from: tau = ms[i]/1000/4.6; k = 65536*(1-exp(-CTL/(tau*FS)))
 * where ms[i] = 10000^(i/127), FS=44100, CTL=128. */
#ifndef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED   /* the stack build takes ENV_EXP from upstream's generated felucca_tables.h */
static const uint16_t ENV_EXP[128] = {
    65535,65535,65535,65534,65532,65529,65524,65514,65498,65473,65433,65375,65291,65174,65016,64807,
    64537,64197,63779,63273,62672,61971,61165,60254,59236,58115,56893,55578,54175,52693,51142,49532,
    47873,46176,44452,42712,40964,39218,37484,35768,34079,32421,30802,29224,27693,26211,24780,23403,
    22081,20814,19602,18446,17344,16297,15302,14359,13466,12621,11823,11070,10361,9692,9064,8473,
    7917,7396,6907,6448,6019,5616,5239,4887,4557,4249,3961,3692,3440,3206,2986,2782,
    2591,2413,2247,2093,1948,1814,1689,1572,1463,1362,1268,1180,1098,1022,951,884,
    823,766,712,663,617,574,534,496,462,430,400,372,346,322,299,278,
    259,241,224,208,194,180,167,156,145,135,125,116,108,101,94,87,
};
#endif

#undef slc_user_scan

#include "upstream/felucca/firmware/src/eng_slice.c"

#ifdef _SLICE_RING_PUBLISH_LOCAL
#undef RING_PUBLISH
#undef _SLICE_RING_PUBLISH_LOCAL
#endif
