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

static int msc_bot_xfer(uint8_t *cb, uint8_t cb_len, uint8_t dir_in,
                        uint8_t *data, uint32_t data_len)
{
    usb_cbw_t cbw;
    usb_csw_t csw;

    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag = ++msc_tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = dir_in ? 0x80 : 0x00;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = cb_len;

    uint32_t i;
    for (i = 0; i < 16; i++)
        cbw.CBWCB[i] = (i < cb_len) ? cb[i] : 0;

    /* send CBW */
    int r = usb_bulk_out(uhost.ep_out, (const uint8_t *)&cbw, 31);
    if (r < 0) return -1;

    /* data phase */
    if (data_len && data) {
        if (dir_in) {
            r = usb_bulk_in(uhost.ep_in, data, data_len);
        } else {
            r = usb_bulk_out(uhost.ep_out, data, data_len);
        }
        if (r < 0) return -2;
    }

    /* read CSW */
    r = usb_bulk_in(uhost.ep_in, (uint8_t *)&csw, 13);
    if (r < 0) return -3;
    if (csw.dCSWSignature != CSW_SIGNATURE) return -4;
    if (csw.dCSWTag != cbw.dCBWTag) return -5;
    if (csw.bCSWStatus != 0) return -6;
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
    usb_control_msg(0x02, 0x01, 0, ep_addr, 0, 0);
}

static void msc_bot_recovery(void)
{
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
    int r = usb_control_msg(0xA1, 0xFE, 0, uhost.iface, lun, 1);
    if (r < 0) { *lun = 0; return 0; }
    return 0;
}

static int msc_init_device(void)
{
    uint8_t buf[36];
    int r, retries;

    msc_get_max_lun(&uhost.max_lun);
    modd_log_int("usb: msc luns=", uhost.max_lun + 1);

    for (retries = 0; retries < 3; retries++) {
        modd_log("usb: msc inquiry");
        r = msc_inquiry(buf);
        if (r == 0) break;
        modd_log_int("usb: msc inquiry fail ", r);
        msc_bot_recovery();
        usb_mdelay(200);
    }
    if (r) return r;

    for (retries = 0; retries < 10; retries++) {
        r = msc_test_unit_ready();
        if (r == 0) break;
        msc_request_sense(buf);
        usb_mdelay(100);
    }
    modd_log_int("usb: msc TUR retries=", retries);
    if (r) { modd_log_int("usb: msc TUR fail ", r); return r; }

    uint32_t last_lba, block_size;
    r = msc_read_capacity(&last_lba, &block_size);
    if (r) { modd_log_int("usb: msc cap fail ", r); return r; }

    modd_log_int("usb: msc lba=", (int32_t)last_lba);
    modd_log_int("usb: msc blk=", (int32_t)block_size);
    uhost.capacity = last_lba + 1;
    uhost.sector_size = block_size;
    uhost.state = UHOST_MSC_READY;
    modd_log("usb: msc ready");
    return 0;
}
