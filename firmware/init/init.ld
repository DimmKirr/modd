/* SPDX-License-Identifier: GPL-3.0-only
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: forked from keremimo/melodee firmware/stub/stub.ld @ v1.4.5 (398ada1e)
 *         modd-owned: boot init with commit marker, flash-map-driven layout, diagnostic mailbox */
/* The boot init (docs/BOOT-SAFETY.md): app.bin's first 0xEE0 bytes, flash 0x4120..0x4FFF, executed in
 * place. Code and constants only: nothing may land in RAM (tools/build.py fails on any other section with a size).
 * The stacks and the entry it leaves through are the app's (firmware/app.ld). */
MEMORY
{
    INIT (rx) : ORIGIN = 0x02000120, LENGTH = 0xEE0
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _init_end = .; } > INIT
    .ram (NOLOAD) : { *(.data*) *(.sdata*) *(.bss*) *(.sbss*) *(COMMON) *(.ram_text*) *(.noinit*) *(.pool*) } > INIT
    ASSERT(SIZEOF(.ram) == 0, "boot init must contain only code and constants")
    _app_entry = 0x02001000;        /* the body's _start (app.ld XIP ORIGIN) */
    _init_sp = 0x01C7A000;          /* app.ld _ustack_top */
    _init_ssp = 0x01C7C000;         /* app.ld _sstack_top */
    /DISCARD/ : { *(.comment) *(.note*) }
}
