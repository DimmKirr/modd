/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/app.ld @ v1.4.5 (398ada1e)
 *         https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/app.ld */
/* modd memory map with fm1-dock modules (CONFIG_DOCK; tools/build/build.py picks this file).
 * As app.ld, except that a module's fixed regions stay free:
 *   0x01C04000..0x01C07F00  the module's .ram_text (HB_MOD_RT; modd's own RAM code is below 0x01C04000,
 *                           the boot stub's STATUS keeps the last 256 bytes: DOCK_RT_MAX)
 *   0x01C30000..0x01C74000  the module's .data + .bss (HB_MOD_RAM, 272 KiB), which doubles as the
 *                           audio resource arena and holds the network memory (.pool_net: lwIP pools,
 *                           the USB link, HTTP): a module, modd's engines and a network link never
 *                           run together (dock.c / usbnet.c refuse each other).
 * RAM shrinks to 64 KiB (0x01C08000..0x01C18000) and POOL moves to 0x01C18000..0x01C30000 (96 KiB).
 * ASSERTs below fail the link when either overflows. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = 0x8C000   /* body ends before commit-marker sector 0x91000 */
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x4000    /* below the module's .ram_text window */
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 64K
    POOL   (rwx) : ORIGIN = 0x01C18000, LENGTH = 0x18000
    DOCK   (rwx) : ORIGIN = 0x01C30000, LENGTH = 0x44000
    NOINIT (rw)  : ORIGIN = 0x01C7C020, LENGTH = 0x3D30
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
    _resource_low_end = 0x01C04000;
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    _resource_data_start = _bss_end;
    _resource_data_end = ORIGIN(RAM) + LENGTH(RAM);
    .pool_net (NOLOAD) : { _pool_net_start = .; *(.pool_net*) . = ALIGN(4); _pool_net_end = .; } > DOCK
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .audio_text : { _audio_start = .; *(.audio_text*) . = ALIGN(4); _audio_end = .; } > POOL AT > XIP
    _audio_load = LOADADDR(.audio_text);
    _guard_ram_end = SIZEOF(.audio_text) ? _audio_end : _dsp_end;
    .cpu1_stacks (NOLOAD) : {
        . = ALIGN(32); _cpu1_stacks_lo = .;
        KEEP(*(.cpu1_stacks))
        _cpu1_stacks_hi = .;
    } > POOL
    _cpu1_ustack_top = _cpu1_stacks_lo + 4096;
    _cpu1_sstack_top = _cpu1_stacks_lo + 8192;
    /* the audio working arena is the module region: one or the other owns it */
    .dock (NOLOAD) : { _dock_start = .; _resource_start = .; } > DOCK
    _resource_end = ORIGIN(DOCK) + LENGTH(DOCK);
    _resource_minimum = DEFINED(AUDIO_RESIDENT) ? 48K : 139K;
    ASSERT(_resource_end - _resource_start >= _resource_minimum, "audio working arena below worst-case budget")
    .noinit (NOLOAD) : { *(.noinit*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    .audio_scratch (NOLOAD) : { . = ALIGN(32); *(.audio_scratch*) . = ALIGN(4); _audio_scratch_end = .; } > NOINIT
    _resource_retained_start = _audio_scratch_end;
    _resource_retained_end = ORIGIN(NOINIT) + LENGTH(NOINIT);
    _resource_cache_start = ORIGIN(CACHE);
    _resource_cache_end = ORIGIN(CACHE) + LENGTH(CACHE);
    ASSERT(_resource_low_start <= _resource_low_end, "RAM code overlaps the module's .ram_text window")
    ASSERT(_resource_data_start <= _resource_data_end, "data overlaps resource pool")
    ASSERT(_cpu1_stacks_hi <= ORIGIN(DOCK), "POOL overlaps the module's RAM (HB_MOD_RAM)")
    ASSERT(_resource_retained_start <= _resource_retained_end, "retained state overlaps boot records")
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
