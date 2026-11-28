/* SPDX-License-Identifier: GPL-3.0-only
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); the entry is Felucca's crt0.S
 * (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments)
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: forked from keremimo/melodee firmware/stub/crt0_stub.S @ v1.4.5 (398ada1e)
 *         modd-owned: boot init with commit marker, flash-map-driven layout, diagnostic mailbox */
/* The boot init's entry (docs/BOOT-SAFETY.md). The SPL maps app.bin at XIP 0x02000120 and jumps to its
 * first byte, here, with r0 = its boot-parameter block; it expects `goto +4; rts` as the first instruction. What
 * the SPL left (icfg, psr, r0, sp, ssp) goes to STATUS exactly as the app's crt0.S did when it was the entry (the
 * app's no longer writes it); then interrupts off, the app's two stacks, init_main. It returns only to boot the
 * app: the body's entry at 0x02001000 (firmware/crt0.S), which sets the stacks again. */
#include "init_core.h"
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	goto 2f
	.balign 4
	.ascii "CRSTUB"			/* app.bin + 8: tools/fm1pkg_make.py knows an init image by this */
	.short INIT_VERSION
2:	r1 = icfg
	r2 = psr
	cli
	r3 = 0x1C07F00		/* STATUS (debug area) */
	[r3 + 0x20] = r1	/* icfg as handed over */
	[r3 + 0x24] = r2	/* psr */
	[r3 + 0x28] = r0	/* boot-parameter pointer */
	r1 = sp
	[r3 + 0x2C] = r1
	r1 = ssp
	[r3 + 0x30] = r1
	sp = _init_sp
	ssp = _init_ssp
	call init_main
	goto _app_entry
