// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test for the pure part of the dock adapter (firmware/adapters/modd_dock.h):
 * header checks, CRC and relocation of a synthetic module image. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CONFIG_DOCK 1
#include "modd_dock.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* a file: header, 2 relocs, image of 512 B code + 16 B data + 0 B rt at base 0x02200000 */
static uint8_t file[32768];

static hb_file_t *mk(uint32_t base)
{
    hb_file_t *f = (hb_file_t *)file;
    uint32_t *rel = (uint32_t *)(file + sizeof *f);
    uint32_t io, i;
    memset(file, 0xFF, sizeof file);
    memset(f, 0, sizeof *f);
    f->magic = HB_MAGIC;
    f->abi = HB_ABI;
    strcpy(f->name, "TEST");
    f->code_size = 512;
    f->data_size = 16;
    f->bss_size = 1024;
    f->rt_size = 0;
    f->n_relocs = 2;
    f->desc = 64;
    f->base = base;
    io = dock_image_offset(f->n_relocs);
    rel[0] = 8;
    rel[1] = 100;
    for (i = 0; i < f->code_size + f->data_size; i++)
        file[io + i] = (uint8_t)i;
    /* a word that holds a module address: base + 0x40 */
    {
        uint32_t v = base + 0x40u;
        memcpy(file + io + 8, &v, 4);
        v = base + 0x200u;
        memcpy(file + io + 100, &v, 4);
    }
    /* the descriptor: abi first */
    {
        uint32_t abi = HB_ABI;
        memcpy(file + io + f->desc, &abi, 4);
    }
    f->crc = dock_file_crc(f);
    return f;
}

/* a real .dock file (fm1-dock's docs/modules): the header, the CRC, a relocation, the CRC again */
static int check_real(const char *path)
{
    static uint8_t buf[HB_MOD_RAM_SIZE];
    FILE *fp = fopen(path, "rb");
    size_t n;
    hb_file_t *f = (hb_file_t *)buf;
    uint32_t crc, at = 0x02099100u;
    if (!fp) { printf("FAIL open %s\n", path); return 1; }
    n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    if (dock_head_ok(f, (uint32_t)n) != DOCK_OK) { printf("FAIL head %s\n", path); return 1; }
    if (dock_file_size(f) != n) { printf("FAIL size %s\n", path); return 1; }
    crc = f->crc;
    if (dock_file_crc(f) != crc) { printf("FAIL crc %s\n", path); return 1; }
    if (dock_desc_abi(f) != HB_ABI) { printf("FAIL desc %s\n", path); return 1; }
    dock_relocate(f, at);
    if (f->base != at || dock_file_crc(f) != f->crc || f->crc == crc) { printf("FAIL reloc %s\n", path); return 1; }
    dock_relocate(f, 0x02200000u);
    if (dock_file_crc(f) != crc || f->crc != crc) { printf("FAIL reloc back %s\n", path); return 1; }
    printf("ok %s %s\n", path, f->name);
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        fails += check_real(argv[i]);
    hb_file_t *f = mk(0x02200000u);
    uint32_t io = dock_image_offset(2);
    CHECK(io == 256u);
    CHECK(dock_image_offset(0) == 256u);
    CHECK(dock_image_offset(45) == 512u);       /* 80 + 180 = 260 -> 512 */

    CHECK(dock_head_ok(f, sizeof file) == DOCK_OK);
    CHECK(dock_file_size(f) == io + 528u);
    CHECK(dock_file_crc(f) == f->crc);

    /* corruptions */
    f->magic ^= 1; CHECK(dock_head_ok(f, sizeof file) == DOCK_BAD_HEAD); f->magic ^= 1;
    f->abi = 1;    CHECK(dock_head_ok(f, sizeof file) == DOCK_BAD_HEAD); f->abi = HB_ABI;
    f->flags = HB_F_PROBE; CHECK(dock_head_ok(f, sizeof file) == DOCK_BAD_HEAD); f->flags = 0;
    f->bss_size = HB_MOD_RAM_SIZE; CHECK(dock_head_ok(f, sizeof file) == DOCK_TOO_BIG); f->bss_size = 1024;
    f->rt_size = DOCK_RT_MAX + 4u; CHECK(dock_head_ok(f, sizeof file) == DOCK_TOO_BIG); f->rt_size = 0;
    CHECK(dock_head_ok(f, 500) == DOCK_BAD_HEAD);           /* the file is shorter than its image */
    f->desc = 600; CHECK(dock_head_ok(f, sizeof file) == DOCK_BAD_HEAD); f->desc = 64;
    {
        uint32_t *rel = (uint32_t *)(file + sizeof *f);
        rel[1] = 600; CHECK(dock_head_ok(f, sizeof file) == DOCK_BAD_HEAD); rel[1] = 100;
    }
    file[io + 7] ^= 0x55; CHECK(dock_file_crc(f) != f->crc); file[io + 7] ^= 0x55;

    /* relocation: every reloc word moves by the delta, base follows, crc re-verifies */
    dock_relocate(f, 0x02300100u);
    {
        uint32_t v;
        memcpy(&v, file + io + 8, 4);   CHECK(v == 0x02300140u);
        memcpy(&v, file + io + 100, 4); CHECK(v == 0x02300300u);
        CHECK(f->base == 0x02300100u);
        CHECK(dock_file_crc(f) == f->crc);
        CHECK(dock_head_ok(f, sizeof file) == DOCK_OK);
    }
    dock_relocate(f, 0x02200000u);
    {
        uint32_t v;
        memcpy(&v, file + io + 8, 4);   CHECK(v == 0x02200040u);
        CHECK(dock_file_crc(f) == f->crc);
    }

    /* the descriptor's abi is read from the image */
    CHECK(dock_desc_abi(f) == HB_ABI);

    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
