// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB host enumeration: SET_ADDRESS, GET_DESCRIPTOR, find MSC bulk endpoints. */

/* Standard USB descriptor structures */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_desc_t;

/* Standard requests */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_SET_CONFIG      0x09

#define USB_DIR_IN   0x80
#define USB_DIR_OUT  0x00

/* One configuration descriptor at a time; an iPhone's tethering
 * configuration is ~94 bytes, composite audio devices run past 128. */
static uint8_t usb_enum_buf[256] __attribute__((aligned(4), section(".pool")));

static int usb_hub_discover(void);

static int usb_host_enumerate(void)
{
    usb_device_desc_t dev;
    int r;

    /* 1. GET_DESCRIPTOR(DEVICE) at addr 0, first 8 bytes for bMaxPacketSize0 */
    usb_host_sie_wr(SIE_FADDR, 0);
    usb_host_sie_wr(SIE_INDEX, 0);
    modd_log("usb: enum start");
    usb_host_log_hex8("usb: con0=", (FM1_USB_CON0 >> 16) & 0xFF);
    usb_host_log_hex8("usb: io0=", (FM1_USB_IO_CON0 >> 4) & 0xF);

    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_DEVICE << 8), 0, (uint8_t *)&dev, 8);
    if (r < 0) {
        usb_host_log_hex8("usb: setup fail csr=",
                          usb_host_sie_rd(SIE_CSR0));
        return -1;
    }

    modd_log("usb: got dev desc");
    usb_host_log_hex8("usb: bDeviceClass=", dev.bDeviceClass);
    usb_host_log_hex8("usb: bMaxPacketSize0=", dev.bMaxPacketSize0);
    uhost.ep0_mps = usb_ep0_mps_valid(dev.bMaxPacketSize0);

    /* 2. SET_ADDRESS(1) */
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_ADDRESS, 1, 0, 0, 0);
    if (r < 0) return -2;
    fm1_delay_ms(10);
    usb_host_sie_wr(SIE_FADDR, 1);

    /* 3. GET_DESCRIPTOR(DEVICE) full 18 bytes at addr 1 */
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_DEVICE << 8), 0, (uint8_t *)&dev, 18);
    if (r < 0) return -3;

    modd_log_int("usb: VID ", dev.idVendor);
    modd_log_int("usb: PID ", dev.idProduct);
    modd_log_int("usb: #cfg ", dev.bNumConfigurations);

    /* 4. GET_DESCRIPTOR(CONFIG) header to learn wTotalLength */
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_CONFIG << 8), 0, usb_enum_buf, 9);
    if (r < 0) return -4;

    usb_config_desc_t *cfg = (usb_config_desc_t *)usb_enum_buf;
    uint16_t total = cfg->wTotalLength;
    modd_log_int("usb: cfg total ", total);
    modd_log_int("usb: #iface ", cfg->bNumInterfaces);
    if (total > sizeof(usb_enum_buf)) total = sizeof(usb_enum_buf);

    /* 5. GET_DESCRIPTOR(CONFIG) full */
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_CONFIG << 8), 0, usb_enum_buf, total);
    if (r < 0) return -5;

    /* 6. Parse: find MSC BBB interface + bulk endpoints */
    uint8_t ep_in = 0, ep_out = 0;
    uint16_t ep_maxp = 64;
    uint8_t msc_found = 0, msc_iface = 0;
    uint32_t pos = cfg->bLength;

    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;

        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            usb_interface_desc_t *iface = (usb_interface_desc_t *)&usb_enum_buf[pos];
            usb_host_log_hex8("usb: iface cls=", iface->bInterfaceClass);
            usb_host_log_hex8("usb: iface sub=", iface->bInterfaceSubClass);
            usb_host_log_hex8("usb: iface proto=", iface->bInterfaceProtocol);
            if (iface->bInterfaceClass == USB_CLASS_MSC &&
                iface->bInterfaceSubClass == USB_MSC_SUBCLASS_SCSI &&
                iface->bInterfaceProtocol == USB_MSC_PROTO_BBB) {
                msc_found = 1;
                msc_iface = iface->bInterfaceNumber;
            } else {
                msc_found = 0;
            }
        }

        if (dtype == USB_DT_ENDPOINT && dlen >= 7 && msc_found) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&usb_enum_buf[pos];
            if ((ep->bmAttributes & 0x03) == 0x02) {  /* first bulk IN / OUT */
                if ((ep->bEndpointAddress & USB_EP_DIR_IN) && !ep_in) {
                    ep_in = ep->bEndpointAddress & 0x0F;
                    ep_maxp = ep->wMaxPacketSize;
                } else if (!(ep->bEndpointAddress & USB_EP_DIR_IN) && !ep_out) {
                    ep_out = ep->bEndpointAddress & 0x0F;
                }
            }
        }
        pos += dlen;
    }

    uhost.vid = dev.idVendor;
    uhost.pid = dev.idProduct;
    uhost.dev_class = dev.bDeviceClass;

    if (!ep_in || !ep_out) {
        if (dev.bDeviceClass == 0x09) {
            modd_log("usb: hub detected, running discovery");
            r = usb_hub_discover();
            if (r == 0) {
                modd_log("usb: hub MSC mounted");
                return 0;
            }
            return -6;
        }
        return -6;
    }

    /* 7. SET_CONFIGURATION */
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_CONFIG,
                        cfg->bConfigurationValue, 0, 0, 0);
    if (r < 0) return -7;
    fm1_delay_ms(50);
    uhost.addr = 1;
    uhost.ep0_mps_msc = uhost.ep0_mps;

    /* 8. Configure host bulk EP1: DMA buffers + SIE registers */
    usb_host_rxbuf = usb_enum_buf;
    fm1_usb_ep_txbuf(USB_BULK_HOST_EP, ep0buf);
    fm1_usb_ep_rxbuf(USB_BULK_HOST_EP, usb_host_rxbuf);
    usb_host_sie_wr(SIE_INDEX, USB_BULK_HOST_EP);
    usb_host_sie_wr(SIE_TXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_TXCSR1, 0x48u);
    usb_host_sie_wr(SIE_TXCSR2, 0x20u);
    usb_host_sie_wr(SIE_TXTYPE, (uint32_t)(0x20u | ep_out));
    usb_host_sie_wr(SIE_RXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_RXCSR1, 0x90u);
    usb_host_sie_wr(SIE_RXCSR2, 0);
    usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x20u | ep_in));
    fm1_usb_ep_enable(1u << USB_BULK_HOST_EP);

    uhost.ep_in = ep_in;
    uhost.ep_out = ep_out;
    uhost.iface = msc_iface;
    uhost.max_packet = ep_maxp;
    uhost.state = UHOST_ENUMERATED;
    modd_log("usb: enum ok");
    return 0;
}
