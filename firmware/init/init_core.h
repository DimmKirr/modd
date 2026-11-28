/* SPDX-License-Identifier: GPL-3.0-only
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: forked from keremimo/melodee firmware/stub/stub_core.h @ v1.4.5 (398ada1e)
 *         modd-owned: boot init with commit marker, flash-map-driven layout, diagnostic mailbox */
/* The boot init's decision, portable (docs/BOOT-SAFETY.md): the device part is init.c, the host tests are
 * tests/boot_init_test.c and the updater's interruption model in tests/boot_test.c. No data, no libcalls, no division.
 *
 * The init is app.bin's first 0xEE0 bytes (flash 0x4120..0x4FFF, XIP 0x02000120..0x02000FFF); the SPL enters it on
 * every boot. The app proper (the body) is linked from XIP 0x02001000 (flash 0x5000). The last 32 bytes of app.bin's
 * last whole sector (flash 0x91FE0) hold the commit marker, which the updater erases before it writes anything else
 * and writes last (firmware/boot/boot_core.c):
 *
 *   +0  magic   "CRBM"
 *   +4  format  1
 *   +8  len     the body's length from flash 0x5000, a multiple of 4, at most 0x8C000 (up to the marker's sector)
 *   +12 crc     CRC-32 (zlib's) of those len bytes, as they read through XIP (decrypted)
 *   +16 mcrc    CRC-32 of the 16 bytes above
 *   +20 comp    4 bytes that tools/fm1pkg_make.py solves so app.bin's CRC-16 in the directory is a constant (the
 *               init does not read them)
 *
 * The decision:
 *   no rescue key and the marker valid (with its body CRC)  -> INIT_APP: the app at 0x02001000
 *   otherwise rescue: a first arrival with a valid staged updater at flash 0xE0000 -> INIT_BOOT (the record the
 *   app's step 1 writes, then a core reset: the SPL runs the updater); a second arrival without a boot in between
 *   (the rescue word, RAM that survives a warm reset), or no valid updater -> INIT_UBOOT (the ROM's USB boot).
 * Strict: no valid marker, no boot. The rescue key is OCT- alone held at power-on (with OCT+ it is the app's
 * calibration gesture).
 *
 * Version 2 (version 1 did not see the key on the unit: it sampled for 3 ms right after the SPL's jump and gave up
 * when OCT- read open in the first four samples): the key is sampled through a fixed window on every boot and
 * judged by its tail (init_key_held), the samples go to the mailbox, and the running app can ask through the rescue
 * word for the updater check on a normal boot (INIT_REQ_TEST) or for the rescue itself (INIT_REQ_RESCUE).
 *
 * Version 3: the cause was found on the unit. PA2 (the LCD's "backlight" line, active low) is the enable of a rail
 * that also feeds the 595 chain and the LEDs: with PA2 high, as the SPL leaves it, no key reads closed and no LED
 * lights. The device side (init.c) now pulls PA2 low before it samples, as the app's fm1_lcd_hw_init does, and
 * drives it high again before a hand-over. Nothing in this file changed but the version. */
#ifndef INIT_CORE_H
#define INIT_CORE_H
#define INIT_VERSION 3                   /* in the init's header (crt0_init.S: no C suffix) and the RAM mailbox */
#ifndef __ASSEMBLER__
#include <stdint.h>
#include "fm1_flash_map.h"

#define INIT_LEN 0xEE0u                  /* app.bin offset of the body: flash 0x4120 + 0xEE0 = 0x5000 */
#define INIT_BODY_OFF FLASH_BODY_OFF     /* flash (hal/fm1_flash_map.h: the layout, from Kconfig) */
#define INIT_BODY_MAX FLASH_BODY_MAX     /* the body ends below the marker's sector */
#define INIT_MARK_SEC FLASH_MARK_SEC
#define INIT_MARK_OFF FLASH_MARK_OFF     /* the sector's last 32 bytes: in its last page, programmed last */
#define INIT_MARK_LEN 20u
#define INIT_MARK_MAGIC 0x4D425243u      /* "CRBM" */
#define INIT_MARK_FORMAT 1u
#define INIT_BOOT_OFF FLASH_OTA_LO            /* the staged updater (ota.c OTA_AREA, OTA_AREA_LEN) */
#define INIT_BOOT_LEN FLASH_OTA_LEN
#define INIT_PARM_LEN 112u               /* the UPDATA_PARM record (hal/fm1_sys.h FM1_UPDATA_PARM_LEN) */

#define INIT_RSC_MAGIC 0x52534300u       /* the rescue word: "RSC" in bits 8..31, the attempts in bits 0..7 */
#define INIT_REQ_TEST 0x54514552u        /* .. or a request the app left there before a restart ("REQT"): boot as
                                          * usual, and check the staged updater on the way (the detail's updater code) */
#define INIT_REQ_RESCUE 0x52514552u      /* .. ("REQR"): as if the rescue key were held */
#define INIT_REQ_BROWSE 0x42514552u      /* .. ("REQB"): as if OCT+ were held (updater file browser) */
#define INIT_INFO_MAGIC 0x53420000u      /* the mailbox's info word: "SB", the version, the decision */

enum { INIT_APP = 1, INIT_BOOT = 2, INIT_UBOOT = 3, INIT_BOOT_BROWSE = 4 };
enum { INIT_MK_OK, INIT_MK_MAGIC, INIT_MK_FORMAT, INIT_MK_SELF, INIT_MK_LENGTH, INIT_MK_BODY, INIT_MK_SKIPPED };
enum { INIT_LD_OK, INIT_LD_HEAD, INIT_LD_INNER, INIT_LD_NOT_OURS, INIT_LD_BLOCKS, INIT_LD_SKIPPED };

#ifndef INIT_PLAIN_WINDOW
#define INIT_PLAIN_WINDOW() ((void)0)    /* the device: flash above 0x93000 reads as plaintext through XIP */
#endif
#ifndef INIT_BODY_CRC
#define INIT_BODY_CRC 1                  /* 0: trust the marker alone (the updater's order makes it the commit) */
#endif

static uint32_t init_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t init_rd32(const uint8_t *p) { return init_rd16(p) | init_rd16(p + 2) << 16; }
static void init_wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* ota.c ota_crc16: poly 0x1021, init 0 (the JLFS heads, the record) */
static uint32_t init_crc16(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0, i;
    while (n--) {
        c ^= (uint32_t)*p++ << 8;
        for (i = 0; i < 8u; i++)
            c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1;
        c &= 0xFFFFu;
    }
    return c;
}

/* CRC-32 (zlib's: reflected 0xEDB88320, init and final xor 0xFFFFFFFF), four tables of 256 words built in RAM
 * (the caller's stack: 4 KiB; the init has no room for them in flash and no .data) and four bytes per step. */
typedef struct { uint32_t t[4][256]; } init_crc_t;

static void init_crc32_init(init_crc_t *k)
{
    uint32_t i, j, c;
    for (i = 0; i < 256u; i++) {
        c = i;
        for (j = 0; j < 8u; j++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        k->t[0][i] = c;
    }
    for (i = 0; i < 256u; i++)
        for (j = 1; j < 4u; j++)
            k->t[j][i] = (k->t[j - 1u][i] >> 8) ^ k->t[0][k->t[j - 1u][i] & 0xFFu];
}

/* words: little-endian, as the chip and every host we build on read them; n in bytes, a multiple of 4 */
static uint32_t init_crc32_words(const init_crc_t *k, const uint32_t *w, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (n >>= 2; n; n--) {
        c ^= *w++;
        c = k->t[3][c & 0xFFu] ^ k->t[2][(c >> 8) & 0xFFu] ^ k->t[1][(c >> 16) & 0xFFu] ^ k->t[0][c >> 24];
    }
    return ~c;
}

static uint32_t init_crc32_bytes(const init_crc_t *k, const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--)
        c = (c >> 8) ^ k->t[0][(c ^ *p++) & 0xFFu];
    return ~c;
}

/* the marker (mark: its 20 bytes, plain) and the body it commits (body: flash 0x5000 on, plain, word aligned) */
static uint32_t init_marker_check(const init_crc_t *k, const uint8_t *mark, const uint32_t *body)
{
    uint32_t len = init_rd32(mark + 8);
    if (init_rd32(mark) != INIT_MARK_MAGIC)
        return INIT_MK_MAGIC;                        /* (an erased sector decrypts to noise: here) */
    if (init_rd32(mark + 16) != init_crc32_bytes(k, mark, 16))
        return INIT_MK_SELF;                         /* a torn marker page */
    if (init_rd32(mark + 4) != INIT_MARK_FORMAT)
        return INIT_MK_FORMAT;
    if (!len || (len & 3u) || len > INIT_BODY_MAX)
        return INIT_MK_LENGTH;
#if INIT_BODY_CRC
    if (init_crc32_words(k, body, len) != init_rd32(mark + 12))
        return INIT_MK_BODY;
#else
    (void)body;
#endif
    return INIT_MK_OK;
}

static uint32_t init_memeq(const uint8_t *a, const char *b, uint32_t n)
{
    while (n--)
        if (*a++ != (uint8_t)*b++)
            return 0;
    return 1;
}

/* the staged updater (ld: flash 0xE0000..0xE4FFF, plain) as ota.c left it: the head ota_stage_head wrote last, then
 * the package's ota.bin from its byte 0x20 (the inner head with the load address, the LZ4 blocks build.py boot_wrap
 * made). The checks of ota_stage that the flash copy allows (the package's outer body CRC is not kept in flash),
 * and the block chain from end to end, so an erased or cut-off staging area is not handed to the SPL. Only our
 * updater passes (crt0_boot.S's "MODD-FM-LOADER-1"): the official one rewrites the head and the init with it. */
static uint32_t init_boot_check(const uint8_t *ld)
{
    uint32_t len, isize, p, end, total = 0, i, own = 0;
    if (init_crc16(ld + 2, 30) != init_rd16(ld) || init_rd32(ld + 4) != INIT_BOOT_OFF || ld[12] != 0x41u ||
        !init_memeq(ld + 16, "BOOT.BIN", 9))
        return INIT_LD_HEAD;
    len = init_rd32(ld + 8) - 32u;                   /* ota.bin's body: the inner head and the blocks */
    if (len < 32u + 8u || len > INIT_BOOT_LEN - 0x200u)
        return INIT_LD_HEAD;
    isize = init_rd32(ld + 0x20 + 4);
    if (init_crc16(ld + 0x22, 30) != init_rd16(ld + 0x20) || init_rd32(ld + 0x20 + 8) != 0x01C0A800u ||
        !isize || isize > 0x14000u)
        return INIT_LD_INNER;
    for (i = 0x48; i + 16u <= 0x48u + 64u && !own; i++)
        own = init_memeq(ld + i, "MODD-FM-LOADER-1", 16);
    if (!own)
        return INIT_LD_NOT_OURS;
    for (p = 0x40, end = 0x20u + len; p + 8u <= end;) {
        uint32_t clen = init_rd32(ld + p), dlen = init_rd32(ld + p + 4);
        if (!clen || clen > 4096u || !dlen || dlen > 4096u || clen > end - p - 8u)
            return INIT_LD_BLOCKS;
        p += 8u + clen;
        total += dlen;
        if (dlen != 4096u && p != end)
            return INIT_LD_BLOCKS;                   /* only the last block is short */
    }
    return p == end && total == isize ? INIT_LD_OK : INIT_LD_BLOCKS;
}

/* the UPDATA_PARM record, byte for byte ota.c ota_session's */
static void init_record(uint8_t *parm)
{
    static const char ID[12] = {'o', 't', 'a', '-', 'F', 'M', '-', '1', '_', '0', '1', '5'};
    uint32_t i;
    for (i = 0; i < INIT_PARM_LEN; i++)
        parm[i] = 0;
    init_wr16(parm + 2, 0x5A0Du);
    init_wr16(parm + 4, 0x5A01u);
    init_wr16(parm + 6, 0x5441u);
    for (i = 0; i < 12u; i++)
        parm[8 + i] = (uint8_t)ID[i];
    init_wr16(parm + 72, INIT_BOOT_OFF & 0xFFFFu);
    init_wr16(parm + 74, INIT_BOOT_OFF >> 16);
    init_wr16(parm, init_crc16(parm + 2, 78));
}

/* rescue attempts since the last boot: 0 unless the word carries the magic (power-on RAM is not a count) */
static uint32_t init_rescues(uint32_t word)
{
    return (word & 0xFFFFFF00u) == INIT_RSC_MAGIC ? word & 0xFFu : 0u;
}

/* the rescue key from 32 samples about 1.5 ms apart (48 ms, every boot), bit i = sample i, 1 = the row low: dn
 * with OCT-'s column latched, up with OCT+'s, idle with no column latched (a row stuck low, or the matrix not yet
 * alive, reads closed there too). Only the tail counts, the last 12 samples (30 ms into the window, 18 ms long):
 * OCT- closed in at least 10 of them, OCT+ and the idle read in at most 1 each. Whatever the first 20 samples
 * read, of either polarity, changes nothing: they are the time the rail behind PA2 has to come up (init.c
 * shw_rail; until it is up a held key reads open, as versions 1 and 2 read it throughout). Both keys: the app's
 * calibration, not ours. */
#define INIT_KEY_SAMPLES 32u
#define INIT_KEY_TAIL 12u
static uint32_t init_tail(uint32_t bits)
{
    uint32_t i, n = 0;
    for (i = INIT_KEY_SAMPLES - INIT_KEY_TAIL; i < INIT_KEY_SAMPLES; i++)
        n += (bits >> i) & 1u;
    return n;
}
static uint32_t init_key_held(uint32_t dn, uint32_t up, uint32_t idle)
{
    return init_tail(dn) >= INIT_KEY_TAIL - 2u && init_tail(up) <= 1u && init_tail(idle) <= 1u;
}
static uint32_t init_key_held_up(uint32_t dn, uint32_t up, uint32_t idle)
{
    return init_tail(up) >= INIT_KEY_TAIL - 2u && init_tail(dn) <= 1u && init_tail(idle) <= 1u;
}

typedef struct {
    const uint8_t *mark;                 /* flash INIT_MARK_OFF, plain */
    const uint32_t *body;                /* flash INIT_BODY_OFF, plain */
    const uint8_t *ldr;                  /* flash INIT_BOOT_OFF, plain once INIT_PLAIN_WINDOW() ran */
} init_view_t;

typedef struct {
    uint32_t decision;                   /* INIT_APP / INIT_BOOT / INIT_UBOOT / INIT_BOOT_BROWSE */
    uint32_t mk, ld;                     /* INIT_MK_*, INIT_LD_* */
    uint32_t prev;                       /* the rescue attempts found */
    uint32_t req;                        /* the app's request found: 0, 1 INIT_REQ_RESCUE, 2 INIT_REQ_TEST */
    uint32_t word;                       /* the rescue word to leave in RAM */
} init_result_t;

/* the whole decision; key: init_key_held's answer; key_up: init_key_held_up's answer; word: the rescue word as
 * found (a count, a request, or anything: power-on RAM is neither) */
static void init_decide(const init_view_t *v, init_crc_t *k, uint32_t key, uint32_t key_up, uint32_t word, init_result_t *r)
{
    r->prev = init_rescues(word);
    r->req = word == INIT_REQ_RESCUE ? 1u : word == INIT_REQ_TEST ? 2u : word == INIT_REQ_BROWSE ? 3u : 0u;
    r->ld = INIT_LD_SKIPPED;
    r->mk = INIT_MK_SKIPPED;
    r->word = 0;                                     /* a boot, or the ROM: the next rescue is a first one again;
                                                      * a request is answered once */
    /* OCT+ held at power-on, or the app asked for the file browser: run the updater in browse mode */
    if (key_up || r->req == 3u) {
        INIT_PLAIN_WINDOW();
        r->ld = init_boot_check(v->ldr);
        if (r->ld == INIT_LD_OK) {
            r->decision = INIT_BOOT_BROWSE;
            r->word = INIT_REQ_BROWSE;               /* the updater reads this to enter file-browser mode */
            return;
        }
        r->decision = INIT_UBOOT;                   /* no valid updater: fall through to ROM */
        return;
    }
    if (!key && r->req != 1u) {
        init_crc32_init(k);
        r->mk = init_marker_check(k, v->mark, v->body);
        if (r->mk == INIT_MK_OK) {
            if (r->req == 2u) {                      /* the test: the rescue's read of the updater, then the boot */
                INIT_PLAIN_WINDOW();
                r->ld = init_boot_check(v->ldr);
            }
            r->decision = INIT_APP;
            return;
        }
    }
    r->decision = INIT_UBOOT;
    if (r->prev)                                     /* the hand-over was tried and we are here again */
        return;
    INIT_PLAIN_WINDOW();
    r->ld = init_boot_check(v->ldr);
    if (r->ld != INIT_LD_OK)
        return;
    r->decision = INIT_BOOT;
    r->word = INIT_RSC_MAGIC | 1u;
}

/* the mailbox's words 1 and 3 (hal/fm1_sys.h fm1_init_word; SysEx 73 DEBUG shows them). Detail: the marker's
 * code, the updater's << 8, the rescue attempts found << 16, bit 24 the rescue key (OCT-), bits 25..26 the app's
 * request (1 rescue, 2 test, 3 browse), bit 27 the browse key (OCT+) */
static uint32_t init_info(uint32_t decision) { return INIT_INFO_MAGIC | (uint32_t)INIT_VERSION << 8 | decision; }
static uint32_t init_detail(const init_result_t *r, uint32_t key, uint32_t key_up)
{
    return r->mk | r->ld << 8 | r->prev << 16 | key << 24 | r->req << 25 | key_up << 27;
}
#endif /* __ASSEMBLER__ */
#endif
