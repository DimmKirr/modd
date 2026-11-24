/* SPDX-License-Identifier: GPL-3.0-only */
/* The SPI-flash layout of a 1 MiB FM-1 (flash offsets), in one place.
 *
 *   0x0000 .. 0x4000       head: SPL, ISD, directory (JieLi packer)
 *   0x4000 .. FLASH_APP_END   application area: the boot stub, then the body from FLASH_BODY_OFF
 *                          (XIP 0x02001000), up to FLASH_BODY_MAX bytes; the commit marker's sector
 *                          follows the body, one more sector (cfg/EQ tables) ends the area
 *   FLASH_APP_END .. FLASH_DATA_LO   store A (the Prophet banks above the app)
 *   FLASH_DATA_LO .. FLASH_DATA_HI   Melodee's main store (projects, user samples, dock modules)
 *   0xE0000 .. 0xE5000     OTA staging (the staged update loader)
 *   0xE5000 .. 0xFB000     bank extension (expanded presets, project copies)
 *   0xFC000 .. 0xFF000     Melodee globals
 *
 * Only two numbers differ between builds and are Kconfig symbols (kconf/flash.kconfig, from which
 * config.h takes them): CONFIG_FLASH_CHIP_SIZE and CONFIG_FLASH_APP_END. Every store above the app
 * area keeps its size and order and follows FLASH_APP_END (the emulator map: an 8 MiB chip, the
 * area to 0x600000, the stores shifted up). The app, the boot stub (firmware/stub) and the update loader
 * (firmware/loader) all include this file; tools/build/flashmap.py repeats the formulas for the
 * package builder and the linker (--defsym FLASH_BODY_MAX). Nothing else may spell a layout number. */
#ifndef FM1_FLASH_MAP_H
#define FM1_FLASH_MAP_H

#include <stdint.h>

/* The app gets the CONFIG_FLASH_* values from config.h (-include). The stub and the loader are built
 * without it (the rest of config.h would change them): build.py passes the two as -D. */
#if !defined(CONFIG_FLASH_CHIP_SIZE) || !defined(CONFIG_FLASH_APP_END)
#error "CONFIG_FLASH_CHIP_SIZE and CONFIG_FLASH_APP_END come from kconf/flash.kconfig (config.h, or -D)"
#endif

#define FLASH_TOTAL        ((uint32_t)CONFIG_FLASH_CHIP_SIZE)
#define FLASH_SECTOR       0x1000u

#define FLASH_APP_LO       0x4000u                          /* the boot stub (and its directory entry) */
#define FLASH_BODY_OFF     0x5000u
#define FLASH_APP_END      ((uint32_t)CONFIG_FLASH_APP_END)
#define FLASH_MARK_SEC     (FLASH_APP_END - 0x2000u)        /* the commit marker's sector */
#define FLASH_MARK_OFF     (FLASH_MARK_SEC + 0xFE0u)        /* its last 32 bytes, in its last page */
#define FLASH_BODY_MAX     (FLASH_MARK_SEC - FLASH_BODY_OFF) /* the body ends below the marker's sector */

/* The FM-1's own map ends its app area here; a hardware address above it (CONFIG_SETTINGS_AT) is
 * moved by how far this build's area end is from it. */
#define FLASH_HW_APP_END   0x93000u
#define FLASH_ABOVE_APP(hw) ((uint32_t)(hw) + FLASH_APP_END - FLASH_HW_APP_END)

#define FLASH_STORE_A_LO   FLASH_APP_END
#define FLASH_STORE_A_HI   (FLASH_STORE_A_LO + 0x4000u)
#define FLASH_DATA_LO      FLASH_STORE_A_HI
#define FLASH_DATA_HI      (FLASH_DATA_LO + 0x49000u)
#define FLASH_OTA_LO       FLASH_DATA_HI
#define FLASH_OTA_LEN      0x5000u
#define FLASH_OTA_HI       (FLASH_OTA_LO + FLASH_OTA_LEN)
#define FLASH_BANK_EXT_LO  FLASH_OTA_HI
#define FLASH_BANK_EXT_HI  (FLASH_BANK_EXT_LO + 0x16000u)
#define FLASH_GLOB_LO      (FLASH_BANK_EXT_HI + 0x1000u)
#define FLASH_GLOB_HI      (FLASH_GLOB_LO + 0x3000u)
#define FLASH_KEYMAC_LO    FLASH_GLOB_HI                    /* the chip's key_mac sector, the last of the FM-1's */

#define FLASH_SMP_USER_LO   (FLASH_DATA_LO + 0x9000u)
#define FLASH_SMP_USER_SIZE 0x14000u
#define FLASH_SMP_USER_N    3
#define FLASH_SMP_USER(k)   (FLASH_SMP_USER_LO + (uint32_t)(k) * FLASH_SMP_USER_SIZE)

#define FLASH_ALIGNED(x) (((x) & (FLASH_SECTOR - 1u)) == 0u)
_Static_assert(FLASH_ALIGNED(FLASH_TOTAL) && FLASH_ALIGNED(FLASH_APP_END),
               "flash map: the chip size and the app area end are 4 KiB sector aligned");
_Static_assert(FLASH_BODY_OFF < FLASH_MARK_SEC && FLASH_MARK_SEC < FLASH_APP_END && FLASH_APP_END < FLASH_DATA_LO,
               "flash map: stub, body, marker, app end and the store are in order");
_Static_assert(FLASH_DATA_LO < FLASH_DATA_HI && FLASH_DATA_HI <= FLASH_OTA_LO && FLASH_OTA_HI <= FLASH_BANK_EXT_LO &&
               FLASH_BANK_EXT_LO < FLASH_BANK_EXT_HI && FLASH_BANK_EXT_HI <= FLASH_GLOB_LO &&
               FLASH_GLOB_LO < FLASH_GLOB_HI && FLASH_KEYMAC_LO + FLASH_SECTOR <= FLASH_TOTAL,
               "flash map: regions are ordered, do not overlap and lie inside the chip");
_Static_assert(FLASH_TOTAL <= 0x1000000u && 0x02000000u + FLASH_TOTAL - FLASH_APP_LO <= 0x04000000u,
               "flash map: the chip is addressable (24-bit SPI address, XIP window below 0x04000000)");
_Static_assert(FLASH_SMP_USER_LO >= FLASH_DATA_LO && FLASH_SMP_USER(FLASH_SMP_USER_N) <= FLASH_DATA_HI,
               "flash map: the user sample slots lie inside the main store");
_Static_assert(FLASH_MARK_OFF + 32u <= FLASH_MARK_SEC + FLASH_SECTOR, "flash map: the marker fits its sector");

#endif
