// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* FatFs disk I/O glue: routes FatFs sector reads to the USB host driver. */

static DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0) return RES_PARERR;
    if (uhost.state != UHOST_MSC_READY) return RES_NOTRDY;
    int r = usb_host_write_sectors(sector, buff, count);
    if (r != 0) {
        modd_log_int("usb: fat write err lba=", (int32_t)sector);
        return RES_ERROR;
    }
    return RES_OK;
}

static DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (uhost.state != UHOST_MSC_READY) return STA_NOINIT;
    uint32_t cap, ssz;
    if (usb_host_read_capacity(&cap, &ssz) != 0) {
        modd_log("usb: fat disk_init cap fail");
        return STA_NOINIT;
    }
    uhost.capacity = cap;
    uhost.sector_size = ssz;
    modd_log_int("usb: fat sectors=", (int32_t)cap);
    return 0;
}

static DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (uhost.state != UHOST_MSC_READY) return STA_NODISK;
    return 0;
}

static DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0) return RES_PARERR;
    if (uhost.state != UHOST_MSC_READY) return RES_NOTRDY;
    int r = usb_host_read_sectors(sector, buff, count);
    if (r != 0) {
        modd_log_int("usb: fat read err lba=", (int32_t)sector);
        return RES_ERROR;
    }
    return RES_OK;
}

static DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) return RES_PARERR;
    if (uhost.state != UHOST_MSC_READY) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = uhost.capacity;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)uhost.sector_size;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
