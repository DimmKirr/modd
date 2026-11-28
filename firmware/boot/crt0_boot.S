/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: forked from keremimo/melodee firmware/loader/crt0_ldr.S @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/loader/crt0_ldr.S */
/* modd OTA updater entry. The SPL decompresses the updater to its load
 * address (0x01C0A800) and calls its first byte. Interrupts off,
 * our own stacks, clear .bss, then C. Never returns. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	.ascii "MODD-FM-LOADER-1"	/* 16 bytes: modd recognises its updater in a package by this */
	.balign 2
1:	cli
	sp = _stack_top
	ssp = _sstack_top
	call boot_cstart
	goto .
