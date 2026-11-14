/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Joakim Eriksson (fm1-dock)
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: joakimeriksson/fm1-dock firmware/dock/hb_module.h @ v0.8.2 (6199d250)
 *         https://github.com/joakimeriksson/fm1-dock/blob/6199d250c10376fca0270b917da2e79c7c604691/firmware/dock/hb_module.h
 *         joakimeriksson/fm1-dock firmware/src/dock.c @ v0.8.2 (6199d250) (crc_upd, check_module, d_commit's relocation)
 *         https://github.com/joakimeriksson/fm1-dock/blob/6199d250c10376fca0270b917da2e79c7c604691/firmware/src/dock.c */
/* Dock adapter: fm1-dock's module ABI (what a .dock module exports and what the host gives it)
 * and the pure helpers over a module file: header checks, CRC-32, relocation. No HAL here, so
 * tests/dock_test.c compiles this on the host. The device side (slot table, flash, start/stop,
 * the audio hook) is firmware/src/dock/dock.c; the disk source is dock_disk.c.
 *
 * A module runs in place from flash with one fixed RAM region (HB_MOD_RAM) and one .ram_text
 * window (HB_MOD_RT); only one runs at a time. The tables are the ABI: a module links against
 * nothing of the host's and the host checks `abi` before any call. */
#ifndef MODD_DOCK_H
#define MODD_DOCK_H
#include <stdint.h>

#define HB_ABI 2u                               /* 2: flags, midi(), blit(), text_w(), key events */

/* memory a module may use (fm1-dock's modules/module.ld places .data / .bss here) */
#define HB_MOD_RAM      0x01C30000u             /* .data + .bss, 272 KiB */
#define HB_MOD_RAM_SIZE 0x44000u
#define HB_MOD_RT       0x01C04000u             /* .ram_text, 16 KiB, called through a pointer */
#define HB_MOD_RT_SIZE  0x4000u
/* modd keeps the boot stub's STATUS block at 0x01C07F00: a module's .ram_text ends below it */
#define DOCK_RT_MAX     0x3F00u

#define HB_TOP 24                               /* screen rows above this are the host's */

/* hb_module_t.flags */
#define HB_F_SCREEN 1u
#define HB_F_PANEL  2u
#define HB_F_DATA   4u
#define HB_F_XBTN   8u

/* ui() events: kind in the top byte, the id below; arg is the encoder's clicks */
enum {
    HB_EV_BTN_DOWN = 0x01000000u,
    HB_EV_BTN_UP   = 0x02000000u,
    HB_EV_ENC      = 0x03000000u,
    HB_EV_KEY_DOWN = 0x04000000u,
    HB_EV_KEY_UP   = 0x05000000u,
    HB_EV_KEY_PRESSURE = 0x06000000u,
};
#define HB_EV_KIND(ev) ((ev) & 0xFF000000u)
#define HB_EV_ID(ev)   ((ev) & 0xFFu)

enum { HB_BTN_OCT_DOWN = 0, HB_BTN_OCT_UP = 1, HB_BTN_FX = 2, HB_BTN_SEL = 3, HB_BTN_ENV = 4, HB_BTN_LFO = 5,
       HB_BTN_EDIT = 6, HB_BTN_GLO = 7, HB_BTN_HOME = 8, HB_BTN_SAVE = 9, HB_BTN_ARP = 10, HB_BTN_SEQ = 11,
       HB_BTN_PLAY = 12, HB_BTN_REC = 13 };
enum { HB_ENC_SELECT = 0, HB_ENC_ALGORITHM = 1, HB_ENC_KNOB1 = 2, HB_ENC_KNOB2 = 3, HB_ENC_KNOB3 = 4,
       HB_ENC_KNOB4 = 5, HB_ENC_PRESETS = 6 };
#define HB_LED_KEY(k) (14u + (k))

typedef struct hb_key_sense {
    uint8_t state;
    uint8_t vel;
    uint16_t v;
    uint16_t d;
    uint16_t dland;
} hb_key_sense_t;
#define HB_SENSE_DBASE 185u

/* what the host gives a module (passed to init); field order is the ABI, later versions append */
typedef struct hb_host {
    uint32_t abi;                               /* HB_ABI */
    uint32_t size;                              /* sizeof(hb_host_t) */
    uint32_t (*ms)(void);
    uint32_t (*ticks)(void);                    /* the 24 MHz counter */
    void (*log)(const char *s);
    void (*log_u32)(const char *s, uint32_t v);
    void (*fill)(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
    void (*text)(int32_t x, int32_t y, int32_t w, uint32_t big, const char *s, uint16_t fg, uint32_t center);
    void (*cv_begin)(int32_t w, int32_t h, uint16_t bg);
    void (*cv_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
    void (*cv_line)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c);
    void (*cv_pixel)(int32_t x, int32_t y, uint16_t c);
    void (*cv_text)(int32_t x, int32_t y, uint32_t big, const char *s, uint16_t fg);
    void (*cv_blit)(int32_t x, int32_t y);
    void (*led)(uint32_t id, uint32_t on);
    uint32_t (*rand)(void);
    int (*load)(void *p, uint32_t n);           /* the module's settings record: 0 loaded */
    void (*save)(const void *p, uint32_t n);
    void (*midi_send)(uint32_t st, uint32_t d1, uint32_t d2);
    uint32_t (*master)(void);                   /* the MASTER pot as a gain, Q12 (the host applies it) */
    /* ABI 2 */
    void (*blit)(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px);
    int32_t (*text_w)(uint32_t big, const char *s);
    int (*bank_load)(void *p, uint32_t n);
    int (*bank_save)(const void *p, uint32_t n);
    uint32_t (*sense_keys)(void);
    int (*key_sense)(uint32_t k, hb_key_sense_t *out);
    void (*sense_set)(uint32_t on, uint32_t drive, uint32_t curve, uint32_t ceil);
    int (*bank_load_n)(uint32_t idx, void *p, uint32_t n);
    int (*bank_save_n)(uint32_t idx, const void *p, uint32_t n);
    uint32_t (*bank_free)(void);
    int (*c1_run)(void (*fn)(uint32_t), uint32_t arg);
    int (*c1_wait)(void);
    uint32_t board;                             /* HB_BOARD_FM1 */
    void (*key_base)(uint32_t n);
} hb_host_t;
enum { HB_BOARD_FM1 = 0, HB_BOARD_SMK37 = 1 };
#define HB_HOST_HAS(h, f) ((h)->size >= (uint32_t)(__builtin_offsetof(hb_host_t, f) + sizeof (h)->f))
#define HB_BANK_MAX 8176u
#define HB_BANK_IDX_MAX 255u

typedef struct hb_param {
    const char *name;
    int32_t lo, hi, def;
} hb_param_t;

/* the descriptor: at the start of the module's image */
typedef struct hb_module {
    uint32_t abi;                               /* HB_ABI */
    const char *name;
    int  (*init)(const hb_host_t *h);           /* 0: ready */
    void (*release)(void);
    void (*note)(uint32_t ch, uint32_t key, uint32_t vel);   /* vel 0: note off */
    void (*cc)(uint32_t ch, uint32_t num, uint32_t val);
    void (*render)(int32_t *out, uint32_t n);   /* audio interrupt: add n stereo frames (L R, Q15) into out */
    void (*ui)(uint32_t ev, int32_t arg);
    void (*draw)(uint32_t full);
    const hb_param_t *params;
    uint32_t n_params;
    uint32_t flags;                             /* HB_F_* */
    void (*midi)(uint32_t st, uint32_t d1, uint32_t d2);
    int (*data)(const uint8_t *p, uint32_t n);  /* HB_F_DATA only */
} hb_module_t;
#define HB_DESC_MIN ((uint32_t)__builtin_offsetof(hb_module_t, data))
#define HB_DATA_MAX 512u

/* A module in flash and in a .dock file: this header, the relocations, then the image
 * (.text + .rodata, .data's initial values, .ram_text). The image starts at the first 256-byte
 * boundary after the relocations, linked to run at `base`; relocs[] are offsets of 32-bit words
 * in the image that hold module addresses. crc is CRC-32 over the header up to crc, the
 * relocations and the image. */
#define HB_MAGIC 0x52425248u                    /* "HRBR" */
typedef struct {
    uint32_t magic, abi, flags;
    char name[24];
    uint32_t version;
    uint32_t code_size, data_size, bss_size, rt_size;
    uint32_t worst_cycles;
    uint32_t desc;                              /* the descriptor's offset in the image */
    uint32_t base;                              /* the image's run address */
    uint32_t n_relocs;
    uint32_t reserved;
    uint32_t crc;
} hb_file_t;                                    /* 80 bytes */
#define HB_F_PROBE      0x100u                  /* hb_file_t.flags: a probe, never installed */
#define HB_F_BOOT       0x200u                  /* a whole firmware for fm1-dock's boot slot: not here */

/* the slot table entry (fm1-dock's format, so its tools read ours) */
#define HB_SLOT_MAGIC 0x544F4C53u               /* "SLOT" */
#define HB_SLOTS 32u
typedef struct {
    uint32_t magic;
    uint32_t at;                                /* XIP address of the hb_file_t */
    uint32_t size;                              /* header + relocs + image */
    uint32_t crc;                               /* the module's hb_file_t.crc */
} hb_slot_t;

/* outcomes, shared by the checks, the installer and the UI (DOCK_NAME[]) */
enum { DOCK_OK, DOCK_BAD_CRC, DOCK_BAD_HEAD, DOCK_TOO_BIG, DOCK_INIT_FAILED, DOCK_NO_SPACE,
       DOCK_FLASH, DOCK_VERIFY, DOCK_READ, DOCK_NO_DISK, DOCK_BUSY, DOCK_N_STATES };
static const char *const DOCK_NAME[DOCK_N_STATES] = {
    "OK", "BAD CRC", "BAD HEADER", "TOO BIG", "INIT FAILED", "NO SPACE",
    "FLASH ERROR", "VERIFY FAILED", "READ ERROR", "NO DISK", "USB NET ON",
};

/* a chunk source for the installer: the disk today, HTTP later. read fills exactly n bytes
 * (n <= 4096) from the current position, 0 on success; size is the whole file's. */
typedef struct dock_src {
    int (*read)(void *ctx, uint8_t *dst, uint32_t n);
    void *ctx;
    uint32_t size;
} dock_src_t;

/* ---- pure helpers (host-testable) ---- */

static uint32_t dock_crc(uint32_t c, const uint8_t *p, uint32_t n)
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

static uint32_t dock_rd32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void dock_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* where the image starts in the file: after the header and relocations, 256-byte aligned */
static uint32_t dock_image_offset(uint32_t n_relocs)
{
    return (uint32_t)((sizeof(hb_file_t) + 4u * n_relocs + 255u) & ~255u);
}

static uint32_t dock_image_size(const hb_file_t *f)
{
    return f->code_size + f->data_size + f->rt_size;
}

/* header + relocations + image */
static uint32_t dock_file_size(const hb_file_t *f)
{
    return dock_image_offset(f->n_relocs) + dock_image_size(f);
}

/* the header's own checks over a file of `avail` bytes in memory (RAM or XIP): no CRC yet.
 * Relocation offsets are checked too (each names a whole word inside the image). */
static uint32_t dock_head_ok(const hb_file_t *f, uint32_t avail)
{
    const uint32_t *rel = (const uint32_t *)(const void *)(f + 1);
    uint32_t io, isz, i;
    if (avail < sizeof *f || f->magic != HB_MAGIC || f->abi != HB_ABI || f->n_relocs > 65536u ||
        (f->flags & (HB_F_PROBE | HB_F_BOOT)))
        return DOCK_BAD_HEAD;
    if (f->code_size > HB_MOD_RAM_SIZE || f->data_size > HB_MOD_RAM_SIZE || f->rt_size > HB_MOD_RT_SIZE)
        return DOCK_BAD_HEAD;
    io = dock_image_offset(f->n_relocs);
    isz = dock_image_size(f);
    if (isz < 4u || io > avail || isz > avail - io || f->desc + HB_DESC_MIN > f->code_size || (f->desc & 3u))
        return DOCK_BAD_HEAD;
    if (f->data_size + f->bss_size > HB_MOD_RAM_SIZE || f->rt_size > DOCK_RT_MAX)
        return DOCK_TOO_BIG;
    for (i = 0; i < f->n_relocs; i++)
        if (rel[i] > isz - 4u)
            return DOCK_BAD_HEAD;
    return DOCK_OK;
}

/* CRC-32 over the header up to crc, the relocations and the image, from the file in memory */
static uint32_t dock_file_crc(const hb_file_t *f)
{
    const uint8_t *p = (const uint8_t *)f;
    uint32_t head = (uint32_t)((const uint8_t *)&f->crc - p);
    uint32_t c = dock_crc(0, p, head);
    c = dock_crc(c, p + sizeof *f, 4u * f->n_relocs);
    return dock_crc(c, p + dock_image_offset(f->n_relocs), dock_image_size(f));
}

/* the descriptor's abi word, read from the image in the file (not from base) */
static uint32_t dock_desc_abi(const hb_file_t *f)
{
    return dock_rd32((const uint8_t *)f + dock_image_offset(f->n_relocs) + f->desc);
}

/* relocate a file in RAM to run at `base`: every reloc word moves by the delta, the header's
 * base and crc follow (fm1-dock's d_commit, hbmod.move) */
static void dock_relocate(hb_file_t *f, uint32_t base)
{
    const uint32_t *rel = (const uint32_t *)(const void *)(f + 1);
    uint8_t *img = (uint8_t *)f + dock_image_offset(f->n_relocs);
    uint32_t d = base - f->base, i;
    for (i = 0; i < f->n_relocs; i++)
        dock_wr32(img + rel[i], dock_rd32(img + rel[i]) + d);
    f->base = base;
    f->crc = dock_file_crc(f);
}

#endif /* MODD_DOCK_H */
