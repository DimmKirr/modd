/* SPDX-License-Identifier: GPL-3.0-only
 * Emulator-only memory map: app.ld's regions with a larger POOL, the stacks
 * and NOINIT moved up by 0x20000, and FBUF for the shadow framebuffer in the
 * RAM only fm1-live has (1 MiB; an FM-1 ends at 0x01C80000).
 * The chip's boot info, mailbox and interrupt vectors stay where they are on
 * an FM-1, 0x01C7FD50..0x01C7FFFF (hal/fm1_irq.h FM1_VEC, hal/fm1_sys.h), in
 * the middle of this POOL: nothing may be placed over them (the ASSERT below).
 * No arena budget assert: the emulator does not run real-time audio.
 * Based on app.ld (Melodee memory map). */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = FLASH_BODY_MAX
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rwx) : ORIGIN = 0x01C20000, LENGTH = POOL_SIZE  /* kconf/memory.kconfig; build.py passes --defsym */
    NOINIT (rw)  : ORIGIN = 0x01C20000 + POOL_SIZE + 0x8020, LENGTH = 0x01CA0000 - (0x01C20000 + POOL_SIZE + 0x8020)
    FBUF   (rw)  : ORIGIN = 0x01CA0000, LENGTH = 0x40000  /* 256 KiB, emulator only */
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 28K
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
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
    .pool_lvgl (NOLOAD) : { *(.pool_lvgl*) . = ALIGN(4); } > FBUF
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
    .framebuffer (NOLOAD) : { *(.framebuffer*) . = ALIGN(4); } > FBUF
    _resource_end = 0x01C7FD50;    /* the arena stops below the chip's boot info, mailbox and vectors (not at the end of POOL, which spans them) */
    ASSERT(_resource_start <= 0x01C7FD50, "POOL statics reach the boot info, mailbox and vectors at 0x01C7FD50")
    .noinit (NOLOAD) : { *(.noinit*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    .audio_scratch (NOLOAD) : { . = ALIGN(32); *(.audio_scratch*) . = ALIGN(4); _audio_scratch_end = .; } > NOINIT
    _resource_retained_start = _audio_scratch_end;
    _resource_retained_end = ORIGIN(NOINIT) + LENGTH(NOINIT);
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
