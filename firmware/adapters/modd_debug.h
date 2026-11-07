/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Joakim Eriksson (fm1-dock)
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: joakimeriksson/fm1-dock firmware/src/dock.c @ main (2118c729)
 *         https://github.com/joakimeriksson/fm1-dock/blob/2118c7299fdc5fabf81eb81962f1168a21792fd7/firmware/src/dock.c */
/* Debug DAP adapter: NOINIT byte ring buffer + SysEx debug protocol.
 * Ported from fm1-dock (dock.c) for USB-based log reading and diagnostics.
 * The host side uses tools/hbdeploy.py (D_HELLO, D_LOG commands). */
#ifndef MODD_DEBUG_H
#define MODD_DEBUG_H
#include <stdint.h>

#ifdef CONFIG_DEBUG_DAP

/* ---- NOINIT structure: survives resets ---- */
#define MODD_DBG_MAGIC    0x564E4246u      /* "FBNV" */
#define MODD_DBG_LOG_SIZE 4096u
#define MODD_DBG_CRASH_MAGIC 0x43525348u   /* "CRSH" */

typedef struct {
    uint32_t magic;
    uint32_t log_w;                        /* bytes written: ring position is log_w % LOG_SIZE */
    char     log[MODD_DBG_LOG_SIZE];
    struct { uint32_t magic, vec, pc, rets, ms; } crash;
    struct { uint32_t worst, overruns; } stats;
    uint32_t probe;                        /* 12i markers: b0 WL_CTL0 probe running, b1 init-time indicator rows running */
} modd_dbg_nv_t;

/* non-static: the compiler must not assume zero-init for .noinit objects */
modd_dbg_nv_t modd_dbg_nv __attribute__((section(".noinit")));

static void modd_debug_init(void)
{
    if (modd_dbg_nv.magic == MODD_DBG_MAGIC)
        return;
    memset(&modd_dbg_nv, 0, sizeof modd_dbg_nv);
    modd_dbg_nv.magic = MODD_DBG_MAGIC;
}

static void modd_debug_put(const char *s)
{
    while (*s)
        modd_dbg_nv.log[modd_dbg_nv.log_w++ % MODD_DBG_LOG_SIZE] = *s++;
}

/* ---- CRC-32 (zlib-compatible, bitwise) ---- */
static uint32_t modd_dbg_crc32(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t k;
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

/* ---- SysEx frame collection (filled by sysex_byte in usb.c) ---- */
static uint8_t  modd_dbg_sx[64];          /* incoming host request (small: D_HELLO, D_LOG) */
static uint32_t modd_dbg_sx_pos;
static volatile uint8_t modd_dbg_sx_ready;
static volatile uint8_t modd_dbg_sx_on;

/* ---- byte helpers (self-contained: ota_rd32/ota_wr32 are not yet visible) ---- */
static uint32_t dbg_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void dbg_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#else /* !CONFIG_DEBUG_DAP */

static void modd_debug_init(void) {}
static void modd_debug_put(const char *s) { (void)s; }

#endif /* CONFIG_DEBUG_DAP */

#endif /* MODD_DEBUG_H */
