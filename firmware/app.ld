/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Origin: keremimo/melodee firmware/app.ld @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/app.ld */
/* MELODEE memory map.
 * Code + constants execute in place from XIP (body offset 0 = 0x02001000; boot init at 0x02000120).
 * 0x01C00000..0x01C07F00 holds .ram_text (code that must run while the flash
 * is busy: the flash driver). Unused RAMTEXT/debug space ends at 0x01C07F00:
 * the boot init writes STATUS in the last 256 bytes. Linker-derived SRAM tails
 * join the audio resource banks without changing retained state or stacks.
 * 0x01C7C000..0x01C7C01F belongs to the boot init, across warm resets.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = FLASH_BODY_MAX   /* body ends before the commit-marker sector (hal/fm1_flash_map.h); build.py passes --defsym */
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rwx) : ORIGIN = 0x01C20000, LENGTH = POOL_SIZE  /* kconf/memory.kconfig; build.py passes --defsym */
    NOINIT (rw)  : ORIGIN = 0x01C20000 + POOL_SIZE + 0x8020, LENGTH = 0x01C7FD50 - (0x01C20000 + POOL_SIZE + 0x8020)
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 28K
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0))
        . = ORIGIN(XIP) + 0x80;
        KEEP(*(.fw_meta))
        *(.text*) *(.rodata*)
        . = ALIGN(4); _etext = .;
    } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .dsp_text : { _dsp_start = .; *(.dsp_text*) . = ALIGN(4); _dsp_end = .; } > RAMTEXT AT > XIP
    _dsp_load = LOADADDR(.dsp_text);
    .dsp_tables : { _dt_start = .; *(.dsp_tables*) . = ALIGN(4); _dt_end = .; } > RAMTEXT AT > XIP
    _dt_load = LOADADDR(.dsp_tables);
    _resource_low_start = ALIGN(_dt_end, 4);
    _resource_low_end = 0x01C07F00;
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    _resource_data_start = _bss_end;
    _resource_data_end = ORIGIN(RAM) + LENGTH(RAM);
    .pool (NOLOAD) : { _pool_start = .; _pool_net_start = .; *(.pool_net*) . = ALIGN(4); _pool_net_end = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .audio_text : { _audio_start = .; *(.audio_text*) . = ALIGN(4); _audio_end = .; } > POOL AT > XIP
    _audio_load = LOADADDR(.audio_text);
    _guard_ram_end = SIZEOF(.audio_text) ? _audio_end : _dsp_end;
    .cpu1_stacks (NOLOAD) : {
        . = ALIGN(32); _cpu1_stacks_lo = .;
        KEEP(*(.cpu1_stacks))
        _cpu1_stacks_hi = .; _resource_start = .;
    } > POOL
    _cpu1_ustack_top = _cpu1_stacks_lo + 4096;
    _cpu1_sstack_top = _cpu1_stacks_lo + 8192;
    _resource_end = ORIGIN(POOL) + LENGTH(POOL);
    /* Complete resident interrupt/input graph trades arena capacity for code.
     * Four largest engine states must fit 48 KiB (ram_audio_test); effect
     * delay memory has its own admission/allocation limits. */
    _resource_minimum = ARENA_MIN;  /* kconf/memory.kconfig; build.py passes --defsym */
    ASSERT(_resource_end - _resource_start >= _resource_minimum, "audio working arena below worst-case budget")
    .noinit (NOLOAD) : { *(.noinit*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    /* These transfer/job buffers are always filled before use, never retained
     * musical state. Keep diagnostic .noinit addresses ahead of them intact. */
    .audio_scratch (NOLOAD) : { . = ALIGN(32); *(.audio_scratch*) . = ALIGN(4); _audio_scratch_end = .; } > NOINIT
    _resource_retained_start = _audio_scratch_end;
    _resource_retained_end = ORIGIN(NOINIT) + LENGTH(NOINIT);
    /* Never part of .bss or startup zeroing: use only after HAL self-test. */
    _resource_cache_start = ORIGIN(CACHE);
    _resource_cache_end = ORIGIN(CACHE) + LENGTH(CACHE);
    ASSERT(_resource_low_start <= _resource_low_end, "RAM code overlaps startup STATUS")
    ASSERT(_resource_data_start <= _resource_data_end, "data overlaps resource pool")
    ASSERT(_resource_retained_start <= _resource_retained_end, "retained state overlaps boot records")
    _pool_top = ORIGIN(POOL) + LENGTH(POOL);
    _guard0 = _pool_top; _ustack_lo = _pool_top + 0x100; _ustack_top = _pool_top + 0x6000;
    _guard1 = _pool_top + 0x6000; _sstack_lo = _pool_top + 0x6100; _sstack_top = _pool_top + 0x8000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
