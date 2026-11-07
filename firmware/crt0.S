/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Origin: keremimo/melodee firmware/crt0.S @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/crt0.S */
#include "src/build_options.h"
/* The boot stub at 0x02000120 validates the committed body before entering
 * here at 0x02001000. It saved the SPL's STATUS; keep those original values. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = _ustack_top
	ssp = _sstack_top
	call fm1_cstart
	goto .

/* CPU1 entry follows cpu.a/startup.S.o: install separate supervisor/user
 * stacks, then return from the startup interrupt into the RAM polling loop.
 * No application interrupt is enabled on this core. */
#ifdef MELODEE_DUAL_CORE
#if MELODEE_DUAL_CORE
    .section .text.core1,"ax",@progbits
    .globl fm1_core1_start
fm1_core1_start:
    cli
    icfg = 0
    usp = _cpu1_ustack_top
    sp = _cpu1_sstack_top
    ssp = _cpu1_sstack_top
    reti = fm1_core1_main
    rti
#endif
#endif
