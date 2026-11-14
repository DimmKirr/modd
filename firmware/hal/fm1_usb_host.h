// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB host controller HAL for AC791N OTG.
 * Switches the USB PHY into host mode and provides low-level transfer
 * primitives for Bulk-Only Transport (Mass Storage Class). */
#ifndef FM1_USB_HOST_H
#define FM1_USB_HOST_H
#include <stdint.h>

/* SIE host registers (MUSB byte map, JieLi numbering) */
#define SIE_FADDR      0
#define SIE_POWER      1
#define SIE_INTRTX1    2
#define SIE_INTRTX2    3
#define SIE_INTRRX1    4
#define SIE_INTRRX2    5
#define SIE_INTRUSB    6
#define SIE_INTRTX1E   7
#define SIE_INTRTX2E   8
#define SIE_INTRRX1E   9
#define SIE_INTRRX2E  10
#define SIE_INTRUSBE  11
#define SIE_INDEX     14
#define SIE_DEVCTL    15
#define SIE_TXMAXP    16
#define SIE_CSR0      17
#define SIE_TXCSR1    17
#define SIE_TXCSR2    18
#define SIE_RXMAXP    19
#define SIE_RXCSR1    20
#define SIE_RXCSR2    21
#define SIE_COUNT0    22
#define SIE_RXCOUNT1  22
#define SIE_RXCOUNT2  23
#define SIE_TXTYPE    24
#define SIE_TXINTERVAL 25
#define SIE_RXTYPE    26
#define SIE_RXINTERVAL 27

#define USB_BULK_HOST_EP  1
#define USB_EP0_MAXP_DEFAULT 64

/* bMaxPacketSize0 as a full-speed device may declare it; anything else
 * (a truncated read) falls back to 8, which every device accepts. */
static uint8_t usb_ep0_mps_valid(uint8_t v)
{
    return (v == 16 || v == 32 || v == 64) ? v : 8;
}

static void usb_host_sie_wr(uint32_t r, uint32_t v)
{
    if (!fm1_usb_sie_on()) return;
    fm1_usb_sie_wr_start(r, v);
    uint32_t t = 20000;
    while (!fm1_usb_sie_done() && --t) {}
}

static uint32_t usb_host_sie_rd(uint32_t r)
{
    if (!fm1_usb_sie_on()) return 0;
    fm1_usb_sie_rd_start(r);
    uint32_t t = 20000;
    while (!fm1_usb_sie_done() && --t) {}
    return fm1_usb_sie_data();
}

/* simplified host state */
#define UHOST_DISCONNECTED  0
#define UHOST_ATTACHED      1
#define UHOST_ENUMERATED    2
#define UHOST_MSC_READY     3
#define UHOST_ERROR         0xFF

typedef struct {
    uint8_t  state;
    int8_t   last_err;     /* negative error code from enum/msc init */
    uint8_t  ep_in;
    uint8_t  ep_out;
    uint8_t  iface;        /* MSC interface number (for class requests) */
    uint8_t  max_lun;      /* GET_MAX_LUN result */
    uint16_t max_packet;
    uint16_t vid;
    uint16_t pid;
    uint8_t  dev_class;
    uint32_t capacity;     /* sectors */
    uint32_t sector_size;  /* typically 512 */
    uint8_t  ep0_mps;      /* bMaxPacketSize0 of the device FADDR points at */
    uint8_t  addr;         /* the MSC device's address: every MSC transfer sets FADDR to it */
    uint8_t  ep0_mps_msc;  /* its bMaxPacketSize0, restored with FADDR */
    uint8_t  lun;          /* LUN used in CBWs */
    uint8_t  csw_status;   /* last CSW bCSWStatus */
    uint8_t  sense_key;    /* last REQUEST_SENSE: key, ASC, ASCQ */
    uint8_t  asc;
    uint8_t  ascq;
    uint8_t  inq_pdt;      /* INQUIRY byte 0 (peripheral device type) */
    uint8_t  inq_rmb;      /* INQUIRY byte 1 (bit 7: removable) */
    uint8_t  tur_tries;    /* TEST_UNIT_READY attempts on the last LUN */
    uint8_t  ctl_rt;       /* last failed control transfer: bmRequestType, */
    uint8_t  ctl_rq;       /*   bRequest, stage (0 setup, 1 data, 2 status), */
    uint8_t  ctl_stage;    /*   result, CSR0 and FADDR at failure */
    int8_t   ctl_r;
    uint8_t  ctl_csr0;
    uint8_t  ctl_faddr;
} usb_host_t;

static usb_host_t uhost;

/* CBW/CSW for Bulk-Only Transport */
#define CBW_SIGNATURE  0x43425355
#define CSW_SIGNATURE  0x53425355

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} usb_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} usb_csw_t;

static void usb_host_init(void *ep0)
{
    uhost.state = UHOST_DISCONNECTED;
    uhost.last_err = 0;
    uhost.ep_in = 0;
    uhost.ep_out = 0;
    uhost.iface = 0;
    uhost.max_lun = 0;
    uhost.max_packet = 64;
    uhost.vid = 0;
    uhost.pid = 0;
    uhost.dev_class = 0;
    uhost.capacity = 0;
    uhost.sector_size = 512;
    uhost.ep0_mps = USB_EP0_MAXP_DEFAULT;
    uhost.lun = 0;
    fm1_usb_reset();
    fm1_delay_ms(25);
    fm1_usb_host_attach(ep0);
    fm1_delay_ms(25);
    usb_host_sie_wr(SIE_INTRUSBE, 0);
    usb_host_sie_wr(SIE_INTRTX1E, 0);
    usb_host_sie_wr(SIE_INTRTX2E, 0);
    usb_host_sie_wr(SIE_INTRRX1E, 0);
    usb_host_sie_wr(SIE_INTRRX2E, 0);
    fm1_usb_ep_enable(0x1Fu);
    usb_host_sie_wr(SIE_FADDR, 0);
    usb_host_sie_wr(SIE_DEVCTL, 0x03u);
}

static void usb_host_deinit(void)
{
    uhost.state = UHOST_DISCONNECTED;
    fm1_usb_off();
}

static int usb_host_connected(void)
{
    return fm1_usb_line() != 0;
}

/* Forward declarations for Phase 2-5 (defined in udisk/*.c, included after) */
static int usb_host_enumerate(void);
static int msc_init_device(void);
static int msc_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count);
static int msc_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count);

/* glue for diskio.c */
static int usb_host_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count)
{
    if (uhost.state != UHOST_MSC_READY) return -1;
    return msc_read_sectors(lba, buf, count);
}

static int usb_host_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count)
{
    if (uhost.state != UHOST_MSC_READY) return -1;
    return msc_write_sectors(lba, buf, count);
}

static int usb_host_read_capacity(uint32_t *sectors, uint32_t *sector_sz)
{
    if (uhost.state != UHOST_MSC_READY) return -1;
    *sectors = uhost.capacity;
    *sector_sz = uhost.sector_size;
    return 0;
}

static void usb_host_log_hex8(const char *prefix, uint32_t v)
{
    char line[MODD_LOG_LINE_LEN];
    int n = 0;
    const char *p = prefix;
    while (*p && n < MODD_LOG_LINE_LEN - 4) line[n++] = *p++;
    static const char hex[] = "0123456789ABCDEF";
    line[n++] = hex[(v >> 4) & 0xF];
    line[n++] = hex[v & 0xF];
    line[n] = 0;
    modd_log(line);
}

static void usb_host_log_fail(int err)
{
    char line[MODD_LOG_LINE_LEN];
    uint32_t ln = fm1_usb_line();
    uint32_t dc = usb_host_sie_rd(SIE_DEVCTL);
    uint32_t pw = usb_host_sie_rd(SIE_POWER);
    uint32_t iu = usb_host_sie_rd(SIE_INTRUSB);
    int n = 0;
    const char *p = "enum ";
    while (*p) line[n++] = *p++;
    if (err < 0) { line[n++] = '-'; err = -err; }
    line[n++] = (char)('0' + err);
    p = " ln:"; while (*p) line[n++] = *p++;
    line[n++] = (char)('0' + ln);
    p = " dc:"; while (*p) line[n++] = *p++;
    if (dc >= 100) line[n++] = (char)('0' + dc / 100);
    if (dc >= 10) line[n++] = (char)('0' + (dc / 10) % 10);
    line[n++] = (char)('0' + dc % 10);
    p = " pw:"; while (*p) line[n++] = *p++;
    line[n++] = "0123456789ABCDEF"[(pw >> 4) & 0xF];
    line[n++] = "0123456789ABCDEF"[pw & 0xF];
    p = " iu:"; while (*p) line[n++] = *p++;
    if (iu >= 10) line[n++] = (char)('0' + (iu / 10) % 10);
    line[n++] = (char)('0' + iu % 10);
    line[n] = 0;
    modd_log(line);
}

static int usb_host_bus_reset(void)
{
    uint32_t attempt;
    for (attempt = 0; attempt < 3; attempt++) {
        usb_host_sie_wr(SIE_POWER, 0x08u);
        fm1_delay_ms(30);
        usb_host_sie_wr(SIE_POWER, 0);
        fm1_delay_ms(150);
        uint32_t dc = usb_host_sie_rd(SIE_DEVCTL);
        if (dc & 0x40u) return 1;   /* FSDev detected */
        if (dc & 0x20u) return -1;  /* LSDev: not supported */
        usb_host_log_hex8("usb: reset retry dc=", dc);
        fm1_delay_ms(200);
    }
    return 0;
}

static void usb_host_poll(void)
{
    if (uhost.state == UHOST_DISCONNECTED) {
        if (usb_host_connected()) {
            uhost.state = UHOST_ATTACHED;
            fm1_delay_ms(200);
            if (!usb_host_connected()) {
                uhost.state = UHOST_DISCONNECTED;
                return;
            }
            uint32_t dc = usb_host_sie_rd(SIE_DEVCTL);
            if (!(dc & 0x10u))
                modd_log("usb: warn VBUS low");
            int speed = usb_host_bus_reset();
            if (speed <= 0) {
                uhost.state = UHOST_ERROR;
                uhost.last_err = speed < 0 ? -8 : -9;
                modd_log_int("usb: no FS device, speed=", speed);
                return;
            }
            int r = usb_host_enumerate();
            if (r == 0) {
                modd_log_int("USB host: VID ", uhost.vid);
                modd_log_int("USB host: PID ", uhost.pid);
                modd_log("USB host: device connected");
                if (uhost.state == UHOST_MSC_READY) {
                    modd_log_int("USB host: sectors ", (int32_t)uhost.capacity);
                } else {
                    r = msc_init_device();
                    if (r == 0) {
                        modd_log_int("USB host: sectors ", (int32_t)uhost.capacity);
                    } else {
                        uhost.last_err = (int8_t)(r - 10);
                        uhost.state = UHOST_ERROR;
                        modd_log_int("USB host: MSC fail ", r);
                    }
                }
            } else {
                uhost.last_err = (int8_t)r;
                uhost.state = UHOST_ERROR;
                usb_host_log_fail(r);
            }
        }
    } else if (uhost.state >= UHOST_ATTACHED) {
        if (!usb_host_connected()) {
            modd_log("USB: device removed");
            usb_host_log_hex8("usb: devctl=", usb_host_sie_rd(SIE_DEVCTL));
            usb_host_log_hex8("usb: intrusb=", usb_host_sie_rd(SIE_INTRUSB));
            uhost.state = UHOST_DISCONNECTED;
        }
    }
}

/* D_USB debug dump lives in udisk/usb_hub.c (needs the hub device table). */
static uint32_t usb_debug_fill(uint8_t *b, uint32_t max);

#endif /* FM1_USB_HOST_H */
