/* SPDX-License-Identifier: GPL-3.0-only */
/* Origin: keremimo/melodee firmware/src/ota.c @ v1.6.1 (165d177a)
 * Wrapper: the M-UPGRADE updater is the upstream file, included through the patched copy
 * (patches/melodee/0002-ota-build-hooks.patch; class PATCHED). modd supplies the staging area (flash map),
 * the package identity and loader tag, the pool-placed scratch and decode buffers, and drops the
 * pinned m-bootloader installer path. The hooks ota_wire_send .. ota_commit are in adapters/modd_ota.h. */
#include "fm1_flash_map.h"                       /* FLASH_OTA_LO / FLASH_OTA_LEN */
#ifndef OTA_AREA                                 /* (adapters/modd_ota.h defines the same) */
#define OTA_AREA FLASH_OTA_LO                    /* staged loader, 5 x 4 KiB */
#define OTA_AREA_LEN FLASH_OTA_LEN
#endif

#define MELODEE_ID MODD_LOADER_ID                /* package identity answered to the handshake */
#define OTA_LOADER_TAG "MODD-FM-LOADER-1"        /* first LZ4 literals of modd's own loader */
#define OTA_USB_ID "ota-FM-1_900"                /* the loader's own USB identity */
#define OTA_BOOT_INSTALLER 0                     /* no SHA-256-pinned m-bootloader installer */

/* 2 KiB of work space while staging (pool), and the decoded host message */
static uint8_t ota_scratch[0x800] __attribute__((section(".pool")));
#define OTA_SCRATCH ota_scratch
static uint8_t ota_dec[640u];   /* == OTA_FRAME_MAX */
#define OTA_DEC_DEFINED

#include "upstream/melodee/firmware/src/ota.c"
_Static_assert(sizeof ota_dec == OTA_FRAME_MAX, "ota_dec holds a decoded frame");
