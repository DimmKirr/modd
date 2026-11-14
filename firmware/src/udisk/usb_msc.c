// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB Mass Storage Class: Bulk-Only Transport (BBB) + SCSI commands. */

static uint32_t msc_tag;

/* SCSI opcodes */
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_REQUEST_SENSE    0x03
#define SCSI_INQUIRY          0x12
#define SCSI_READ_CAPACITY10  0x25
#define SCSI_READ10           0x28
#define SCSI_WRITE10          0x2A

static void msc_bot_recovery(void);

/* The SIE has one FADDR: a second active device (USB network on host EP2)
 * may have moved it, so every MSC transfer points it back at the disk. */
static void msc_select(void)
{
    usb_host_sie_wr(SIE_FADDR, uhost.addr);
    uhost.ep0_mps = uhost.ep0_mps_msc;
}

#define MSC_BULK_OUT(ep, data, len) \
    usb_bulk_out(USB_BULK_HOST_EP, ep0buf, uhost.max_packet, (ep), (data), (len))
#define MSC_BULK_IN(ep, data, len) \
    usb_bulk_in(USB_BULK_HOST_EP, usb_host_rxbuf, uhost.max_packet, (ep), (data), (len))

static int msc_bot_xfer(uint8_t *cb, uint8_t cb_len, uint8_t dir_in,
                        uint8_t *data, uint32_t data_len)
{
    usb_cbw_t cbw;
    usb_csw_t csw;

    msc_select();
    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag = ++msc_tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = dir_in ? 0x80 : 0x00;
    cbw.bCBWLUN = uhost.lun;
    cbw.bCBWCBLength = cb_len;

    uint32_t i;
    for (i = 0; i < 16; i++)
        cbw.CBWCB[i] = (i < cb_len) ? cb[i] : 0;

    /* send CBW */
    int r = MSC_BULK_OUT(uhost.ep_out,(const uint8_t *)&cbw, 31);
    if (r < 0) {
        modd_log_int("usb: cbw fail r=", r);
        return -1;
    }

    /* data phase */
    if (data_len && data) {
        if (dir_in) {
            r = MSC_BULK_IN(uhost.ep_in,data, data_len);
        } else {
            r = MSC_BULK_OUT(uhost.ep_out,data, data_len);
        }
        if (r < 0) {
            modd_log_int("usb: data fail r=", r);
            return -2;
        }
    }

    /* read CSW */
    r = MSC_BULK_IN(uhost.ep_in,(uint8_t *)&csw, 13);
    if (r < 13) {
        modd_log_int("usb: csw fail r=", r);
        return -3;
    }
    if (csw.dCSWSignature != CSW_SIGNATURE) {
        modd_log_hex("usb: csw bad sig ", (const uint8_t *)&csw, 4);
        return -4;
    }
    if (csw.dCSWTag != cbw.dCBWTag) {
        modd_log_int("usb: csw tag ", (int32_t)csw.dCSWTag);
        return -5;
    }
    uhost.csw_status = csw.bCSWStatus;
    if (csw.bCSWStatus != 0) {
        modd_log_int("usb: csw st=", csw.bCSWStatus);
        if (csw.dCSWDataResidue)
            modd_log_int("usb: csw residue=", (int32_t)csw.dCSWDataResidue);
        /* 2 = phase error: the BOT spec requires a reset recovery before
         * the device will accept another CBW */
        if (csw.bCSWStatus == 2) msc_bot_recovery();
        return -6;
    }
    return 0;
}

static int msc_test_unit_ready(void)
{
    uint8_t cb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return msc_bot_xfer(cb, 6, 0, 0, 0);
}

static int msc_request_sense(uint8_t *buf)
{
    uint8_t cb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0 };
    return msc_bot_xfer(cb, 6, 1, buf, 18);
}

static int msc_inquiry(uint8_t *buf)
{
    uint8_t cb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    return msc_bot_xfer(cb, 6, 1, buf, 36);
}

static uint32_t msc_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int msc_read_capacity(uint32_t *last_lba, uint32_t *block_size)
{
    uint8_t buf[8];
    uint8_t cb[10] = { SCSI_READ_CAPACITY10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int r = msc_bot_xfer(cb, 10, 1, buf, 8);
    if (r) return r;
    *last_lba = msc_be32(buf);
    *block_size = msc_be32(buf + 4);
    return 0;
}

static int msc_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count)
{
    uint8_t cb[10];
    cb[0] = SCSI_READ10;
    cb[1] = 0;
    cb[2] = (uint8_t)(lba >> 24);
    cb[3] = (uint8_t)(lba >> 16);
    cb[4] = (uint8_t)(lba >> 8);
    cb[5] = (uint8_t)(lba);
    cb[6] = 0;
    cb[7] = (uint8_t)(count >> 8);
    cb[8] = (uint8_t)(count);
    cb[9] = 0;
    return msc_bot_xfer(cb, 10, 1, buf, count * uhost.sector_size);
}

static int msc_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count)
{
    uint8_t cb[10];
    cb[0] = SCSI_WRITE10;
    cb[1] = 0;
    cb[2] = (uint8_t)(lba >> 24);
    cb[3] = (uint8_t)(lba >> 16);
    cb[4] = (uint8_t)(lba >> 8);
    cb[5] = (uint8_t)(lba);
    cb[6] = 0;
    cb[7] = (uint8_t)(count >> 8);
    cb[8] = (uint8_t)(count);
    cb[9] = 0;
    return msc_bot_xfer(cb, 10, 0, (uint8_t *)buf, count * uhost.sector_size);
}

/* MSC class-specific control requests (Bulk-Only Transport spec) */
static void msc_clear_halt(uint8_t ep_addr)
{
    msc_select();
    usb_control_msg(0x02, 0x01, 0, ep_addr, 0, 0);
}

static void msc_bot_recovery(void)
{
    msc_select();
    usb_control_msg(0x21, 0xFF, 0, uhost.iface, 0, 0);
    msc_clear_halt(0x80u | uhost.ep_in);
    msc_clear_halt(uhost.ep_out);
    usb_host_sie_wr(SIE_INDEX, USB_BULK_HOST_EP);
    usb_host_sie_wr(SIE_TXCSR1, 0x48u);
    usb_host_sie_wr(SIE_RXCSR1, 0x90u);
    msc_tag = 0;
}

static int msc_get_max_lun(uint8_t *lun)
{
    msc_select();
    int r = usb_control_msg(0xA1, 0xFE, 0, uhost.iface, lun, 1);
    if (r < 0) {
        modd_log_int("usb: get_max_lun fail r=", r);
        *lun = 0;
    }
    return 0;
}

/* "usb: inq pdt=00 rmb=80 SanDisk ": device type, removable bit, vendor. */
static void msc_log_inquiry(const uint8_t *inq)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[MODD_LOG_LINE_LEN];
    int n = 0;
    const char *p = "usb: inq pdt=";
    while (*p) line[n++] = *p++;
    line[n++] = hex[inq[0] >> 4]; line[n++] = hex[inq[0] & 0xF];
    p = " rmb="; while (*p) line[n++] = *p++;
    line[n++] = hex[inq[1] >> 4]; line[n++] = hex[inq[1] & 0xF];
    line[n++] = ' ';
    uint32_t i;
    for (i = 8; i < 16; i++)
        line[n++] = (inq[i] >= 0x20 && inq[i] < 0x7F) ? (char)inq[i] : '.';
    line[n] = 0;
    modd_log(line);
}

/* TEST_UNIT_READY until the LUN is ready. REQUEST_SENSE decides between
 * waiting and giving up: no medium (02/3A) means an empty slot, so the
 * caller moves to the next LUN or device instead of burning the retry
 * budget on it. Returns 0 ready, -6 not ready, other: transport error. */
#define MSC_TUR_TRIES 30

static int msc_wait_ready(uint8_t *sense)
{
    uint8_t key = 0, asc = 0, ascq = 0, last_key = 0xFF, last_asc = 0xFF;
    int r = -6;
    uint32_t t;
    for (t = 0; t < MSC_TUR_TRIES; t++) {
        uhost.tur_tries = (uint8_t)(t + 1);
        r = msc_test_unit_ready();
        if (r == 0) return 0;
        if (r != -6) return r;
        if (msc_request_sense(sense) == 0) {
            key = sense[2] & 0x0F;
            asc = sense[12];
            ascq = sense[13];
        } else {
            key = 0xFF; asc = 0xFF; ascq = 0xFF;
        }
        uhost.sense_key = key;
        uhost.asc = asc;
        uhost.ascq = ascq;
        if (key != last_key || asc != last_asc) {
            uint8_t s[3] = { key, asc, ascq };
            modd_log_hex("usb: sense ", s, 3);
            last_key = key;
            last_asc = asc;
        }
        if (key == 0x02 && asc == 0x3A) return -6;        /* no medium */
        if (key == 0x06) continue;                        /* unit attention: retry now */
        usb_mdelay(200);
    }
    return r;
}

static int msc_init_device(void)
{
    uint8_t buf[36];
    int r, retries;

    msc_get_max_lun(&uhost.max_lun);
    modd_log_int("usb: msc luns=", uhost.max_lun + 1);

    for (uhost.lun = 0; uhost.lun <= uhost.max_lun; uhost.lun++) {
        modd_log_int("usb: msc lun ", uhost.lun);
        for (retries = 0; retries < 3; retries++) {
            r = msc_inquiry(buf);
            if (r == 0) break;
            modd_log_int("usb: msc inquiry fail ", r);
            msc_bot_recovery();
            usb_mdelay(200);
        }
        if (r) return r;
        msc_log_inquiry(buf);
        uhost.inq_pdt = buf[0];
        uhost.inq_rmb = buf[1];
        if ((buf[0] & 0x1F) != 0x00) {
            modd_log("usb: msc not a disk, skip");
            continue;
        }

        r = msc_wait_ready(buf);
        modd_log_int("usb: msc TUR tries=", uhost.tur_tries);
        if (r == -6) { modd_log("usb: msc not ready, skip"); continue; }
        if (r) { modd_log_int("usb: msc TUR fail ", r); return r; }

        uint32_t last_lba, block_size;
        r = msc_read_capacity(&last_lba, &block_size);
        if (r) { modd_log_int("usb: msc cap fail ", r); return r; }

        modd_log_int("usb: msc lba=", (int32_t)last_lba);
        modd_log_int("usb: msc blk=", (int32_t)block_size);
        if (block_size != 512) {
            modd_log("usb: msc blk != 512, skip");
            continue;
        }
        uhost.capacity = last_lba + 1;
        uhost.sector_size = block_size;
        uhost.state = UHOST_MSC_READY;
        modd_log("usb: msc ready");
        return 0;
    }
    uhost.lun = 0;
    modd_log("usb: msc no ready LUN");
    return -6;
}
