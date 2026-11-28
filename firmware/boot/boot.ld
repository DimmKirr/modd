/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: forked from keremimo/melodee firmware/loader/loader.ld @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/loader/loader.ld */
/* modd OTA updater: one RAM image at the address the SPL loads it to.
 * Everything (code, constants, data) lives in RAM: the updater rewrites the
 * flash, so nothing may execute from XIP. Stacks far above, clear of the
 * update record (0x01C7FD88) and the crash record (0x01C7C000). */
MEMORY
{
    BOOT (rwx) : ORIGIN = 0x01C0A800, LENGTH = 0x14000
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0)) *(.text*) *(.ram_text*) *(.rodata*) *(.data*) *(.sdata*) *(.pool*)
        . = ALIGN(4); _image_end = .;
    } > BOOT
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > BOOT
    _stack_top = 0x01C70000;
    _sstack_top = 0x01C72000;
    /DISCARD/ : { *(.comment) *(.note*) *(.noinit*) }
}
