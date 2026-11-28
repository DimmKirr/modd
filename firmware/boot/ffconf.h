/* SPDX-License-Identifier: LicenseRef-FatFs
 * Copyright (C) 2022 ChaN (FatFs R0.15)
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * FatFs notice: LICENSES/FatFs.txt */
/* FatFs configuration for the OTA updater.
 * Read-only, minimal: f_open + f_read + f_lseek + f_close + f_opendir/readdir (file browser). */
#ifndef FFCONF_DEF
#define FFCONF_DEF  80286

#define FF_FS_READONLY   1
#define FF_FS_MINIMIZE   1   /* keep f_opendir, f_readdir, f_closedir for the file browser */
#define FF_USE_FIND      0
#define FF_USE_MKFS      0
#define FF_USE_FASTSEEK  0
#define FF_USE_EXPAND    0
#define FF_USE_CHMOD     0
#define FF_USE_LABEL     0
#define FF_USE_FORWARD   0
#define FF_USE_STRFUNC   0
#define FF_PRINT_LLI     0
#define FF_PRINT_FLOAT   0
#define FF_STRF_ENCODE   0

#define FF_CODE_PAGE     437
#ifdef CONFIG_BOOT_LFN
#define FF_USE_LFN       1   /* static work buffer on the stack */
#else
#define FF_USE_LFN       0
#endif
#define FF_MAX_LFN       255
#define FF_LFN_UNICODE   0
#define FF_LFN_BUF       255
#define FF_SFN_BUF       12
#define FF_FS_RPATH      0

#define FF_VOLUMES       1
#define FF_STR_VOLUME_ID 0
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS        512
#define FF_MAX_SS        512
#define FF_LBA64         0
#define FF_MIN_GPT       0x10000000
#define FF_USE_TRIM      0

#define FF_FS_TINY       0
#define FF_FS_EXFAT      0
#define FF_FS_NORTC      1
#define FF_NORTC_MON     1
#define FF_NORTC_MDAY    1
#define FF_NORTC_YEAR    2025
#define FF_FS_NOFSINFO   0
#define FF_FS_LOCK       0
#define FF_FS_REENTRANT  0

#endif
