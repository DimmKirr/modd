/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_META_H
#define FM1_META_H

#include <stdint.h>

#define FW_META_MAGIC  (('M') | ('O' << 8) | ('D' << 16) | ('D' << 24))
#define FW_META_VER    1
#define FW_META_OFFSET 0x80  /* fixed byte offset from body start (XIP + 0x80) */

typedef struct {
    uint32_t magic;         /*  0: FW_META_MAGIC */
    uint16_t meta_ver;      /*  4: FW_META_VER */
    uint16_t flags;         /*  6: reserved, 0 */
    char     name[16];      /*  8: null-terminated firmware name */
    char     version[32];   /* 24: null-terminated version string */
    uint32_t build_time;    /* 56: unix epoch seconds */
    char     git_sha[12];   /* 60: short hex SHA, null-terminated */
    uint8_t  _reserved[24]; /* 72: future use, zero */
} fw_meta_t;                /* 96 bytes total */

#endif
