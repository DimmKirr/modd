/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio), Adrien Tibi (Recursive Field Instruments)
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/usb_host.c @ v1.6.1 (165d177a)
 *         https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/usb_host.c
 *         (uh_hub_probe / uh_hub_port_probe / uh_hub_status: port power and
 *         power-good wait, port reset and C_PORT_RESET handshake, abandoning
 *         an unresponsive hub instead of repeating control timeouts; reimplemented) */
/* USB hub class: enumerate hub ports, discover downstream devices,
 * configure MUSB routing for the first MSC device found, hand off
 * to the existing MSC + FatFs pipeline. Non-MSC device classes
 * (ethernet, billboard, SD reader) are logged and stubbed. */

/* ---- Hub class constants (USB 2.0 Chapter 11) ---- */

#define USB_CLASS_HUB          0x09
#define USB_CLASS_BILLBOARD    0x11
#define USB_CLASS_CDC          0x02
#define USB_CLASS_VENDOR       0xFF

/* per-port enumeration policy (hub_enumerate_ports) */
#define HUB_ADDR_TRIES    3     /* reset + SET_ADDRESS attempts per port before it is given up */
#define HUB_ADDR_RETRY_MS 150   /* reset recovery before SET_ADDRESS on a retry (the spec minimum is 10 ms) */
#define HUB_PORT_OFF_MS   300   /* port power held off on the last attempt (per-port switching hubs only) */
#define USB_DT_HUB             0x29

#define USB_RT_HUB_GET         0xA0
#define USB_RT_PORT_GET        0xA3
#define USB_RT_PORT_SET        0x23
#define USB_RT_PORT_CLR        0x23

#define HUB_REQ_GET_DESCRIPTOR 0x06
#define HUB_REQ_GET_STATUS     0x00
#define HUB_REQ_SET_FEATURE    0x03
#define HUB_REQ_CLEAR_FEATURE  0x01

#define HUB_FEAT_PORT_POWER       8
#define HUB_FEAT_PORT_RESET       4
#define HUB_FEAT_C_PORT_CONNECTION 16
#define HUB_FEAT_C_PORT_RESET     20

#define HUB_PS_CONNECTION    0x0001
#define HUB_PS_ENABLE        0x0002
#define HUB_PS_OVERCURRENT   0x0008
#define HUB_PS_RESET         0x0010
#define HUB_PS_POWER         0x0100
#define HUB_PS_LOW_SPEED     0x0200
#define HUB_PS_HIGH_SPEED    0x0400

typedef struct __attribute__((packed)) {
    uint8_t  bDescLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;
    uint8_t  bHubContrCurrent;
} usb_hub_desc_t;

/* ---- Device table ---- */

#define USB_MAX_DEVICES  CONFIG_USB_HUB_MAX_DEVICES
#define USB_MAX_HUB_PORTS 7

typedef struct {
    uint8_t  addr;
    uint8_t  hub_addr;
    uint8_t  hub_port;
    uint8_t  dev_class;
} usb_dev_entry_t;

/* Per-device MSC endpoint info, saved during discovery. */
typedef struct {
    uint8_t  addr;
    uint8_t  hub_addr;
    uint8_t  hub_port;
    uint8_t  ep_in;
    uint8_t  ep_out;
    uint8_t  iface;
    uint8_t  ep0_mps;
    uint16_t max_packet;
    uint16_t vid;
    uint16_t pid;
} usb_hub_msc_t;

#define USB_HUB_MSC_MAX 4
static usb_dev_entry_t usb_devtab[USB_MAX_DEVICES];
static uint8_t usb_next_addr;
static usb_hub_msc_t usb_hub_msc_list[USB_HUB_MSC_MAX];
static uint8_t usb_hub_msc_count;
static usb_hub_msc_t usb_hub_msc;
static uint8_t usb_hub_mounted = 0xFF;   /* index into usb_hub_msc_list, 0xFF none */
static uint8_t usb_hub_dead;      /* address of a hub abandoned after a control timeout */

/* A USB network candidate: Apple's tethering interface (class FF/FD/01)
 * in one of the phone's configurations; its alternate setting 1 carries
 * the bulk pair. addr == 0: none found. */
#define USB_VID_APPLE 0x05ACu

typedef struct {
    uint8_t  addr, hub_addr, hub_port;
    uint8_t  cfg_value, iface, alt;
    uint8_t  ep_in, ep_out;      /* endpoint numbers, direction bit masked off */
    uint8_t  ep0_mps;
    uint16_t max_packet;
} usb_net_cand_t;

static usb_net_cand_t usb_net_cand;

#ifdef CONFIG_USB_NET
static uint8_t usbnet_wanted;            /* settings: hold host mode for the network link */
static int usbnet_attach(void);          /* net/usbnet.c, included after the USB host */
#endif

static void usb_devtab_clear(void)
{
    uint32_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++)
        usb_devtab[i].addr = 0;
    usb_next_addr = 1;
    usb_hub_msc.addr = 0;
    usb_net_cand.addr = 0;
    usb_hub_msc_count = 0;
    usb_hub_mounted = 0xFF;
    usb_hub_dead = 0;
}

static usb_dev_entry_t *usb_devtab_alloc(void)
{
    uint32_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++)
        if (usb_devtab[i].addr == 0)
            return &usb_devtab[i];
    return 0;
}

/* ---- MUSB hub routing registers ---- */

/* Per-endpoint hub routing at 0x80 + ep*8:
 *   +0 TXFUNCADDR, +2 TXHUBADDR, +3 TXHUBPORT
 *   +4 RXFUNCADDR, +6 RXHUBADDR, +7 RXHUBPORT */

#define SIE_TXFUNCADDR(ep)  (0x80 + (ep) * 8)
#define SIE_TXHUBADDR(ep)   (0x82 + (ep) * 8)
#define SIE_TXHUBPORT(ep)   (0x83 + (ep) * 8)
#define SIE_RXFUNCADDR(ep)  (0x84 + (ep) * 8)
#define SIE_RXHUBADDR(ep)   (0x86 + (ep) * 8)
#define SIE_RXHUBPORT(ep)   (0x87 + (ep) * 8)

static void usb_hub_route_ep(uint8_t ep, uint8_t dev_addr,
                             uint8_t hub_addr, uint8_t hub_port)
{
    usb_host_sie_wr(SIE_TXFUNCADDR(ep), dev_addr);
    usb_host_sie_wr(SIE_TXHUBADDR(ep), hub_addr);
    usb_host_sie_wr(SIE_TXHUBPORT(ep), hub_port);
    usb_host_sie_wr(SIE_RXFUNCADDR(ep), dev_addr);
    usb_host_sie_wr(SIE_RXHUBADDR(ep), hub_addr);
    usb_host_sie_wr(SIE_RXHUBPORT(ep), hub_port);
}

static void usb_hub_route_clear(uint8_t ep)
{
    usb_hub_route_ep(ep, 0, 0, 0);
}

/* ---- Device class handlers ---- */

static const char *usb_class_name(uint8_t cls)
{
    switch (cls) {
    case 0x00: return "composite";
    case 0x01: return "audio";
    case 0x03: return "HID";
    case USB_CLASS_CDC:  return "CDC/ethernet";
    case USB_CLASS_MSC:  return "MSC";
    case USB_CLASS_HUB:  return "hub";
    case USB_CLASS_BILLBOARD: return "billboard";
    case USB_CLASS_VENDOR: return "vendor";
    default: return "unknown";
    }
}

/* Parse config descriptor in usb_enum_buf, find MSC BBB interface
 * and its bulk endpoints. Returns 1 if found, 0 otherwise. */
static int usb_hub_parse_msc(uint8_t addr, uint8_t hub_addr, uint8_t hub_port,
                             uint16_t vid, uint16_t pid)
{
    usb_config_desc_t *cfg = (usb_config_desc_t *)usb_enum_buf;
    uint16_t total = cfg->wTotalLength;
    if (total > sizeof(usb_enum_buf)) total = sizeof(usb_enum_buf);

    uint8_t ep_in = 0, ep_out = 0;
    uint16_t ep_maxp = 64;
    uint8_t msc_found = 0, msc_iface = 0;
    uint32_t pos = cfg->bLength;

    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;

        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            usb_interface_desc_t *ifc = (usb_interface_desc_t *)&usb_enum_buf[pos];
            if (ifc->bInterfaceClass == USB_CLASS_MSC &&
                ifc->bInterfaceSubClass == USB_MSC_SUBCLASS_SCSI &&
                ifc->bInterfaceProtocol == USB_MSC_PROTO_BBB) {
                msc_found = 1;
                msc_iface = ifc->bInterfaceNumber;
            } else {
                msc_found = 0;
            }
        }

        if (dtype == USB_DT_ENDPOINT && dlen >= 7 && msc_found) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&usb_enum_buf[pos];
            /* BOT uses the first bulk IN and first bulk OUT; UASP-capable
             * bridges list two more bulk pipes after them */
            if ((ep->bmAttributes & 0x03) == 0x02) {
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

    if (!ep_in || !ep_out) return 0;

    modd_log_int("hub: MSC found! addr=", addr);
    modd_log_int("hub: MSC ep_in=", ep_in);
    modd_log_int("hub: MSC ep_out=", ep_out);

    if (usb_hub_msc_count < USB_HUB_MSC_MAX) {
        usb_hub_msc_t *m = &usb_hub_msc_list[usb_hub_msc_count++];
        m->addr = addr;
        m->hub_addr = hub_addr;
        m->hub_port = hub_port;
        m->ep_in = ep_in;
        m->ep_out = ep_out;
        m->iface = msc_iface;
        m->ep0_mps = uhost.ep0_mps;
        m->max_packet = ep_maxp;
        m->vid = vid;
        m->pid = pid;
    }
    return 1;
}

/* Parse one whole configuration descriptor in usb_enum_buf for Apple's
 * tethering interface. Returns 1 and fills usb_net_cand when found. */
static int usb_hub_parse_apple_net(uint8_t addr, uint8_t hub_addr, uint8_t hub_port,
                                   const usb_config_desc_t *cfg, uint16_t total)
{
    uint32_t pos = cfg->bLength;
    uint8_t in_iface = 0, iface_no = 0, ep_in = 0, ep_out = 0;
    uint16_t maxp = 64;

    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;
        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            const usb_interface_desc_t *ifc = (const usb_interface_desc_t *)&usb_enum_buf[pos];
            in_iface = ifc->bInterfaceClass == 0xFF && ifc->bInterfaceSubClass == 0xFD &&
                       ifc->bInterfaceProtocol == 0x01 && ifc->bAlternateSetting == 1;
            if (in_iface) iface_no = ifc->bInterfaceNumber;
        } else if (dtype == USB_DT_ENDPOINT && dlen >= 7 && in_iface) {
            const usb_endpoint_desc_t *ep = (const usb_endpoint_desc_t *)&usb_enum_buf[pos];
            if ((ep->bmAttributes & 0x03) == 0x02) {
                if ((ep->bEndpointAddress & USB_EP_DIR_IN) && !ep_in) {
                    ep_in = ep->bEndpointAddress & 0x0F;
                    maxp = ep->wMaxPacketSize;
                } else if (!(ep->bEndpointAddress & USB_EP_DIR_IN) && !ep_out) {
                    ep_out = ep->bEndpointAddress & 0x0F;
                }
            }
        }
        pos += dlen;
    }
    if (!ep_in || !ep_out) return 0;

    usb_net_cand.addr = addr;
    usb_net_cand.hub_addr = hub_addr;
    usb_net_cand.hub_port = hub_port;
    usb_net_cand.cfg_value = cfg->bConfigurationValue;
    usb_net_cand.iface = iface_no;
    usb_net_cand.alt = 1;
    usb_net_cand.ep_in = ep_in;
    usb_net_cand.ep_out = ep_out;
    usb_net_cand.ep0_mps = uhost.ep0_mps;
    usb_net_cand.max_packet = maxp > 64 ? 64 : maxp;
    modd_log_int("hub: apple net cfg=", cfg->bConfigurationValue);
    return 1;
}

/* Stub handlers for non-MSC device classes.
 * These log what was found; actual drivers can be added later. */

static void usb_hub_on_cdc(uint8_t addr, uint16_t vid, uint16_t pid)
{
    modd_log_int("hub: CDC/ethernet at addr=", addr);
    modd_log_int("hub:  VID=", vid);
    modd_log_int("hub:  PID=", pid);
    /* Future: CDC-ECM or RNDIS ethernet driver */
}

static void usb_hub_on_billboard(uint8_t addr, uint16_t vid, uint16_t pid)
{
    modd_log_int("hub: billboard at addr=", addr);
    (void)vid; (void)pid;
    /* USB-C alt-mode capability advertisement (DisplayPort, HDMI).
     * No action needed: alt-mode is handled by the hub's USB-C controller. */
}

static void usb_hub_on_unknown(uint8_t addr, uint8_t cls, uint16_t vid, uint16_t pid)
{
    modd_log_int("hub: unknown class device addr=", addr);
    usb_host_log_hex8("hub:  class=", cls);
    modd_log_int("hub:  VID=", vid);
    modd_log_int("hub:  PID=", pid);
}

/* ---- Hub discovery (recursive) ---- */

/* "hub: a1 p3 st=0103 ch=0010 CEP F": wPortStatus, wPortChange, then
 * flags: C connected, E enabled, P powered, O overcurrent; L/F/H speed. */
static void usb_hub_log_port_status(uint8_t hub, uint8_t port, uint32_t status)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[MODD_LOG_LINE_LEN];
    uint32_t st = status & 0xFFFFu, ch = status >> 16;
    int n = 0;
    const char *p = "hub: a";
    while (*p) line[n++] = *p++;
    if (hub >= 10) line[n++] = (char)('0' + hub / 10);
    line[n++] = (char)('0' + hub % 10);
    p = " p"; while (*p) line[n++] = *p++;
    line[n++] = (char)('0' + port);
    p = " st="; while (*p) line[n++] = *p++;
    line[n++] = hex[(st >> 12) & 0xF]; line[n++] = hex[(st >> 8) & 0xF];
    line[n++] = hex[(st >> 4) & 0xF];  line[n++] = hex[st & 0xF];
    p = " ch="; while (*p) line[n++] = *p++;
    line[n++] = hex[(ch >> 12) & 0xF]; line[n++] = hex[(ch >> 8) & 0xF];
    line[n++] = hex[(ch >> 4) & 0xF];  line[n++] = hex[ch & 0xF];
    line[n++] = ' ';
    if (st & HUB_PS_CONNECTION)  line[n++] = 'C';
    if (st & HUB_PS_ENABLE)      line[n++] = 'E';
    if (st & HUB_PS_POWER)       line[n++] = 'P';
    if (st & HUB_PS_OVERCURRENT) line[n++] = 'O';
    if (st & HUB_PS_CONNECTION) {
        line[n++] = ' ';
        line[n++] = (st & HUB_PS_LOW_SPEED) ? 'L' : (st & HUB_PS_HIGH_SPEED) ? 'H' : 'F';
    }
    line[n] = 0;
    modd_log(line);
}

/* "hub: a5 if0 cls=08/06/50 ep=81 02" */
static void usb_hub_log_iface(uint8_t addr, const usb_interface_desc_t *ifc,
                              const uint8_t *eps, uint32_t neps)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[MODD_LOG_LINE_LEN];
    int n = 0;
    uint32_t i;
    const char *p = "hub: a";
    while (*p) line[n++] = *p++;
    if (addr >= 10) line[n++] = (char)('0' + addr / 10);
    line[n++] = (char)('0' + addr % 10);
    p = " if"; while (*p) line[n++] = *p++;
    line[n++] = (char)('0' + ifc->bInterfaceNumber % 10);
    p = " cls="; while (*p) line[n++] = *p++;
    line[n++] = hex[ifc->bInterfaceClass >> 4];    line[n++] = hex[ifc->bInterfaceClass & 0xF];
    line[n++] = '/';
    line[n++] = hex[ifc->bInterfaceSubClass >> 4]; line[n++] = hex[ifc->bInterfaceSubClass & 0xF];
    line[n++] = '/';
    line[n++] = hex[ifc->bInterfaceProtocol >> 4]; line[n++] = hex[ifc->bInterfaceProtocol & 0xF];
    if (neps) {
        p = " ep="; while (*p) line[n++] = *p++;
        for (i = 0; i < neps && n < MODD_LOG_LINE_LEN - 4; i++) {
            if (i) line[n++] = ' ';
            line[n++] = hex[eps[i] >> 4]; line[n++] = hex[eps[i] & 0xF];
        }
    }
    line[n] = 0;
    modd_log(line);
}

static int usb_hub_discover_device(uint8_t addr, uint8_t parent_hub,
                                   uint8_t parent_port, uint8_t depth)
{
    usb_device_desc_t ddev;
    int r;

    if (depth > 3) {
        modd_log("hub: max depth reached");
        return -1;
    }

    usb_host_sie_wr(SIE_FADDR, addr);
    usb_host_sie_wr(SIE_INDEX, 0);

    modd_log_int("hub: discover addr=", addr);

    /* First 8 bytes with the 8-byte EP0 assumption, to learn the real
     * bMaxPacketSize0, then the full descriptor with it. */
    uhost.ep0_mps = 8;
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_DEVICE << 8), 0, (uint8_t *)&ddev, 8);
    if (r < 8) {
        modd_log_int("hub: dev desc8 fail r=", r);
        return r < 0 ? r : -1;
    }
    uhost.ep0_mps = usb_ep0_mps_valid(ddev.bMaxPacketSize0);
    uint8_t my_mps = uhost.ep0_mps;
    modd_log_int("hub: ep0 mps=", my_mps);

    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_DEVICE << 8), 0, (uint8_t *)&ddev, 18);
    if (r < 18) {
        modd_log_int("hub: dev desc fail r=", r);
        return r < 0 ? r : -1;
    }

    modd_log_int("hub: VID=", ddev.idVendor);
    modd_log_int("hub: PID=", ddev.idProduct);
    usb_host_log_hex8("hub: class=", ddev.bDeviceClass);
    if (ddev.bNumConfigurations > 1)
        modd_log_int("hub: #cfg=", ddev.bNumConfigurations);

    usb_config_desc_t *cfg = (usb_config_desc_t *)usb_enum_buf;
    uint16_t total;

    /* Alternate configurations first (they share usb_enum_buf): ethernet
     * chips like the AX88179A keep a CDC-NCM configuration behind their
     * vendor one, so log what each one offers. Configuration 0 is read
     * last and is the one parsed and selected below. */
    uint8_t ci;
    for (ci = 1; ci < ddev.bNumConfigurations && ci < 4; ci++) {
        r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                            (USB_DT_CONFIG << 8) | ci, 0, usb_enum_buf, 9);
        if (r < 9) break;
        total = cfg->wTotalLength;
        if (total > sizeof(usb_enum_buf)) total = sizeof(usb_enum_buf);
        r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                            (USB_DT_CONFIG << 8) | ci, 0, usb_enum_buf, total);
        if (r < 9) break;
        modd_log_int("hub: cfg idx=", ci);
        modd_log_int("hub:  value=", cfg->bConfigurationValue);
        modd_log_int("hub:  #iface=", cfg->bNumInterfaces);
        uint32_t cpos = cfg->bLength;
        while (cpos + 2 <= total) {
            uint8_t dlen = usb_enum_buf[cpos];
            uint8_t dtype = usb_enum_buf[cpos + 1];
            if (dlen < 2 || cpos + dlen > total) break;
            if (dtype == USB_DT_INTERFACE && dlen >= 9)
                usb_hub_log_iface(addr, (usb_interface_desc_t *)&usb_enum_buf[cpos], 0, 0);
            cpos += dlen;
        }
        if (ddev.idVendor == USB_VID_APPLE && !usb_net_cand.addr)
            usb_hub_parse_apple_net(addr, parent_hub, parent_port, cfg, total);
    }

    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_CONFIG << 8), 0, usb_enum_buf, 9);
    if (r < 0) return r;

    total = cfg->wTotalLength;
    if (total > sizeof(usb_enum_buf)) total = sizeof(usb_enum_buf);

    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_CONFIG << 8), 0, usb_enum_buf, total);
    if (r < 0) return r;

    /* Log interfaces, one line each with their endpoint addresses */
    uint32_t pos = cfg->bLength;
    uint8_t iface_class = ddev.bDeviceClass;
    usb_interface_desc_t *cur_ifc = 0;
    uint8_t eps[4];
    uint32_t neps = 0;
    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;
        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            if (cur_ifc) usb_hub_log_iface(addr, cur_ifc, eps, neps);
            cur_ifc = (usb_interface_desc_t *)&usb_enum_buf[pos];
            neps = 0;
            if (iface_class == 0) iface_class = cur_ifc->bInterfaceClass;
        }
        if (dtype == USB_DT_ENDPOINT && dlen >= 7 && neps < 4) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&usb_enum_buf[pos];
            eps[neps++] = ep->bEndpointAddress;
        }
        pos += dlen;
    }
    if (cur_ifc) usb_hub_log_iface(addr, cur_ifc, eps, neps);

    /* SET_CONFIGURATION */
    cfg = (usb_config_desc_t *)usb_enum_buf;
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_CONFIG,
                        cfg->bConfigurationValue, 0, 0, 0);
    if (r < 0) {
        modd_log_int("hub: set cfg fail r=", r);
        return r;
    }
    fm1_delay_ms(50);

    /* Record in device table */
    usb_dev_entry_t *ent = usb_devtab_alloc();
    if (ent) {
        ent->addr = addr;
        ent->hub_addr = parent_hub;
        ent->hub_port = parent_port;
        ent->dev_class = iface_class;
    }

    /* ---- Dispatch by class ---- */

    if (ddev.bDeviceClass == USB_CLASS_HUB) {
        /* Recurse into sub-hub */
        modd_log("hub: sub-hub found");
        goto hub_enumerate_ports;
    }

    /* For composite devices (bDeviceClass==0), use interface class */
    switch (iface_class) {
    case USB_CLASS_MSC:
        usb_hub_parse_msc(addr, parent_hub, parent_port,
                          ddev.idVendor, ddev.idProduct);
        break;
    case USB_CLASS_CDC:
    case USB_CLASS_VENDOR:
        usb_hub_on_cdc(addr, ddev.idVendor, ddev.idProduct);
        break;
    case USB_CLASS_BILLBOARD:
        usb_hub_on_billboard(addr, ddev.idVendor, ddev.idProduct);
        break;
    case USB_CLASS_HUB:
        goto hub_enumerate_ports;
    default:
        usb_hub_on_unknown(addr, iface_class, ddev.idVendor, ddev.idProduct);
        break;
    }
    return 0;

hub_enumerate_ports:
    ;
    usb_hub_desc_t hdesc;
    r = usb_control_msg(USB_RT_HUB_GET, HUB_REQ_GET_DESCRIPTOR,
                        (USB_DT_HUB << 8), 0, (uint8_t *)&hdesc, 7);
    if (r < 0) {
        modd_log_int("hub: hub desc fail r=", r);
        return r;
    }

    modd_log_int("hub: #ports=", hdesc.bNbrPorts);
    uint8_t nports = hdesc.bNbrPorts;
    if (nports > USB_MAX_HUB_PORTS) nports = USB_MAX_HUB_PORTS;

    /* Power all ports */
    uint8_t p;
    for (p = 1; p <= nports; p++)
        usb_control_msg(USB_RT_PORT_SET, HUB_REQ_SET_FEATURE,
                        HUB_FEAT_PORT_POWER, p, 0, 0);

    /* bPwrOn2PwrGood (2 ms units), then another 100 ms for the devices to
     * announce themselves on the ports */
    uint32_t pwr_delay = hdesc.bPwrOn2PwrGood * 2;
    if (pwr_delay < 100) pwr_delay = 100;
    fm1_delay_ms(pwr_delay + 100);

    /* Enumerate each connected port. A hub that stops answering on EP0
     * (timeout or no response, not a stall) is abandoned: every further
     * request would block for the full timeout. */
    for (p = 1; p <= nports; p++) {
        uint8_t ps_buf[4];
        r = usb_control_msg(USB_RT_PORT_GET, HUB_REQ_GET_STATUS,
                            0, p, ps_buf, 4);
        if (r == -1 || r == -3) goto hub_dead;
        if (r < 4) continue;

        uint32_t status = ps_buf[0] | (ps_buf[1] << 8) |
                          (ps_buf[2] << 16) | (ps_buf[3] << 24);
        usb_hub_log_port_status(addr, p, status);

        if (!(status & HUB_PS_CONNECTION)) continue;
        if (status & HUB_PS_LOW_SPEED) continue;

        if (status & (HUB_PS_CONNECTION << 16))   /* C_PORT_CONNECTION set */
            usb_control_msg(USB_RT_PORT_CLR, HUB_REQ_CLEAR_FEATURE,
                            HUB_FEAT_C_PORT_CONNECTION, p, 0, 0);

        /* Assign the address once: every attempt below uses it */
        uint8_t new_addr = ++usb_next_addr;
        if (new_addr >= 127) break;

        /* Reset the port and address the device. A device that is still waking up (a flash
         * drive after a VBUS dip: the 2026-11-13 log shows one timing out SET_ADDRESS 33 ms
         * after its reset) gets the whole sequence again, with a longer recovery, up to
         * HUB_ADDR_TRIES times, as desktop hosts do. Only then is the port given up. */
        int addressed = 0, port_done = 0;
        uint32_t attempt;
        for (attempt = 0; attempt < HUB_ADDR_TRIES && !addressed && !port_done; attempt++) {
            uint32_t tries;
            uint32_t post_status = 0;
            if (attempt) {
                modd_log_int("hub: SET_ADDRESS retry ", (int32_t)attempt);
                fm1_delay_ms(HUB_ADDR_RETRY_MS);
            }
            if (attempt == HUB_ADDR_TRIES - 1u && (hdesc.wHubCharacteristics & 3u) == 1u) {
                /* last try: a device that stays enabled but deaf through two resets (the
                 * 2026-11-13 log: a flash drive after a VBUS dip) is power-cycled where the hub
                 * switches port power individually, as Linux's hub_port_power_cycle does */
                modd_log_int("hub: port power cycle p=", p);
                usb_control_msg(USB_RT_PORT_CLR, HUB_REQ_CLEAR_FEATURE, HUB_FEAT_PORT_POWER, p, 0, 0);
                fm1_delay_ms(HUB_PORT_OFF_MS);
                usb_control_msg(USB_RT_PORT_SET, HUB_REQ_SET_FEATURE, HUB_FEAT_PORT_POWER, p, 0, 0);
                fm1_delay_ms(pwr_delay + 100);
            }
            r = usb_control_msg(USB_RT_PORT_SET, HUB_REQ_SET_FEATURE,
                                HUB_FEAT_PORT_RESET, p, 0, 0);
            if (r == -1 || r == -3) goto hub_dead;
            if (r < 0) { port_done = 1; break; }

            for (tries = 0; tries < 10; tries++) {
                fm1_delay_ms(50);
                r = usb_control_msg(USB_RT_PORT_GET, HUB_REQ_GET_STATUS,
                                    0, p, ps_buf, 4);
                if (r < 4) break;
                post_status = ps_buf[0] | (ps_buf[1] << 8) |
                              (ps_buf[2] << 16) | (ps_buf[3] << 24);
                if (!(post_status & HUB_PS_RESET)) break;
            }
            if (r == -1 || r == -3) goto hub_dead;
            if (r < 4 || (post_status & HUB_PS_RESET)) { port_done = 1; break; }
            usb_hub_log_port_status(addr, p, post_status);

            usb_control_msg(USB_RT_PORT_CLR, HUB_REQ_CLEAR_FEATURE,
                            HUB_FEAT_C_PORT_RESET, p, 0, 0);

            if (!(post_status & HUB_PS_ENABLE)) { port_done = 1; break; }
            /* reset recovery before the first request: the spec's 10 ms the first time,
             * HUB_ADDR_RETRY_MS on a retry */
            fm1_delay_ms(attempt ? HUB_ADDR_RETRY_MS : 20);

            /* SET_ADDRESS at default addr 0: hub repeats to the just-reset port */
            usb_host_sie_wr(SIE_FADDR, 0);
            fm1_delay_ms(10);
            r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_ADDRESS, new_addr, 0, 0, 0);
            if (r < 0) {
                modd_log_int("hub: SET_ADDRESS fail r=", r);
                usb_host_sie_wr(SIE_FADDR, addr);
                continue;                       /* retry: the port is reset again (HUB_ADDR_TRIES) */
            }
            addressed = 1;
        }
        if (!addressed) {
            usb_host_sie_wr(SIE_FADDR, addr);
            continue;
        }
        fm1_delay_ms(10);

        usb_hub_discover_device(new_addr, addr, p, depth + 1);
        usb_host_sie_wr(SIE_FADDR, addr);
        uhost.ep0_mps = my_mps;
    }

    return 0;

hub_dead:
    modd_log_int("hub: unresponsive, abandoned addr=", addr);
    usb_hub_dead = addr;
    return r;
}

/* ---- Configure MUSB routing for MSC device through hub ---- */

static int usb_hub_mount_msc_idx(uint8_t idx)
{
    if (idx >= usb_hub_msc_count) {
        modd_log("hub: no more MSC candidates");
        return -1;
    }
    usb_hub_msc = usb_hub_msc_list[idx];
    usb_hub_mounted = idx;

    modd_log_int("hub: mounting MSC addr=", usb_hub_msc.addr);
    modd_log_int("hub:  via hub addr=", usb_hub_msc.hub_addr);
    modd_log_int("hub:  port=", usb_hub_msc.hub_port);

    /* Set FADDR to the MSC device address. The FS hub is a transparent
     * repeater: no per-endpoint routing registers needed (they don't
     * exist on JieLi's SIE anyway, and writing to addresses >= 0x80
     * corrupts the CON1 access state machine by setting the DONE bit). */
    usb_host_sie_wr(SIE_FADDR, usb_hub_msc.addr);
    uhost.ep0_mps = usb_hub_msc.ep0_mps;
    uhost.addr = usb_hub_msc.addr;
    uhost.ep0_mps_msc = usb_hub_msc.ep0_mps;

    /* Configure bulk EP1 registers (same as usb_host_enumerate step 8) */
    usb_host_rxbuf = usb_enum_buf;
    fm1_usb_ep_txbuf(USB_BULK_HOST_EP, ep0buf);
    fm1_usb_ep_rxbuf(USB_BULK_HOST_EP, usb_host_rxbuf);
    usb_host_sie_wr(SIE_INDEX, USB_BULK_HOST_EP);
    usb_host_sie_wr(SIE_TXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_TXCSR1, 0);
    usb_host_sie_wr(SIE_TXCSR1, 0x48u);
    usb_host_sie_wr(SIE_TXCSR2, 0x20u);
    usb_host_sie_wr(SIE_TXTYPE, (uint32_t)(0x20u | usb_hub_msc.ep_out));
    usb_host_sie_wr(SIE_RXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_RXCSR1, 0);
    usb_host_sie_wr(SIE_RXCSR1, 0x90u);
    usb_host_sie_wr(SIE_RXCSR2, 0);
    usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x20u | usb_hub_msc.ep_in));
    fm1_usb_ep_enable(1u << USB_BULK_HOST_EP);

    /* Verify EP1 state: re-select INDEX=1 before readback */
    usb_host_sie_wr(SIE_INDEX, USB_BULK_HOST_EP);
    usb_host_log_hex8("hub: FADDR=", usb_host_sie_rd(SIE_FADDR));
    usb_host_log_hex8("hub: TXCSR1=", usb_host_sie_rd(SIE_TXCSR1));
    usb_host_log_hex8("hub: TXCSR2=", usb_host_sie_rd(SIE_TXCSR2));
    usb_host_log_hex8("hub: TXTYPE=", usb_host_sie_rd(SIE_TXTYPE));
    usb_host_log_hex8("hub: RXTYPE=", usb_host_sie_rd(SIE_RXTYPE));

    /* Populate uhost so existing MSC code works transparently */
    uhost.vid = usb_hub_msc.vid;
    uhost.pid = usb_hub_msc.pid;
    uhost.dev_class = USB_CLASS_MSC;
    uhost.ep_in = usb_hub_msc.ep_in;
    uhost.ep_out = usb_hub_msc.ep_out;
    uhost.iface = usb_hub_msc.iface;
    uhost.max_packet = usb_hub_msc.max_packet;
    uhost.state = UHOST_ENUMERATED;

    modd_log("hub: MSC routing configured");
    return 0;
}

/* ---- Main entry point ---- */

static int usb_hub_discover(void)
{
    modd_log("hub: === discovery start ===");
    usb_devtab_clear();

    /* The hub at address 1 was already addressed by usb_host_enumerate;
     * usb_hub_discover_device records it and walks its ports. */
    usb_next_addr = 1;

    int r = usb_hub_discover_device(1, 0, 0, 0);

    /* Log device table */
    modd_log("hub: === device table ===");
    uint32_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devtab[i].addr == 0) continue;
        usb_dev_entry_t *e = &usb_devtab[i];
        modd_log_int("hub: dev addr=", e->addr);
        modd_log(usb_class_name(e->dev_class));
        modd_log_int("hub:  via hub=", e->hub_addr);
    }
    modd_log("hub: === discovery done ===");

    if (r < 0) return r;

    /* Try each MSC candidate until one initializes */
    uint8_t mi;
    int disk = 0;
    for (mi = 0; mi < usb_hub_msc_count; mi++) {
        r = usb_hub_mount_msc_idx(mi);
        if (r < 0) continue;
        r = msc_init_device();
        if (r == 0) { disk = 1; break; }
        modd_log_int("hub: MSC init fail r=", r);
    }
    if (usb_hub_msc_count == 0)
        modd_log("hub: no MSC device found");

#ifdef CONFIG_USB_NET
    /* The network link comes up next to the disk; the session is a
     * success when either did. */
    if (usb_net_cand.addr && usbnet_wanted && usbnet_attach() == 0)
        return 0;
#endif
    return disk ? 0 : -1;
}

/* D_USB debug dump: uhost state, SIE registers, then the hub discovery
 * outcome (kept until the next discovery, so it survives the log ring).
 * Layout: [0] OK, [1] state, [2] last_err, [3] ep_in, [4] ep_out,
 *         [5] iface, [6] max_lun, [7-8] max_packet, [9-10] vid,
 *         [11-12] pid, [13] dev_class, [14-17] capacity, [18-21] sector_size,
 *         [22] SIE FADDR, [23] SIE POWER, [24] SIE DEVCTL, [25] SIE INTRUSB,
 *         [26] usb_line, [27] OTG mode, [28-29] CON0 bits 16-31,
 *         [30] SIE CSR0, [31] SIE TXCSR1, [32] SIE RXCSR1,
 *         [33] lun, [34] csw_status, [35] sense_key, [36] asc, [37] ascq,
 *         [38] inq_pdt, [39] inq_rmb, [40] tur_tries,
 *         [41] ctl_rt, [42] ctl_rq, [43] ctl_stage, [44] ctl_r, [45] ctl_csr0,
 *         [46] ctl_faddr, [47] n devices, [48] n MSC candidates,
 *         [49] mounted candidate (0xFF none), [50] abandoned hub addr,
 *         [51..] devices: addr, hub, port, class (4 bytes each, [47] entries),
 *         then candidates: addr, hub, port, ep_in, ep_out, vid, pid (9 bytes each) */
static uint32_t usb_debug_fill(uint8_t *b, uint32_t max)
{
    uint32_t n = 51, i, ndev = 0;
    if (max < n) return 0;
    memset(b, 0, max);
    b[0] = 0;  /* OK */
    b[1] = uhost.state;
    b[2] = (uint8_t)uhost.last_err;
    b[3] = uhost.ep_in;
    b[4] = uhost.ep_out;
    b[5] = uhost.iface;
    b[6] = uhost.max_lun;
    b[7] = (uint8_t)(uhost.max_packet & 0xFF);
    b[8] = (uint8_t)(uhost.max_packet >> 8);
    b[9] = (uint8_t)(uhost.vid & 0xFF);
    b[10] = (uint8_t)(uhost.vid >> 8);
    b[11] = (uint8_t)(uhost.pid & 0xFF);
    b[12] = (uint8_t)(uhost.pid >> 8);
    b[13] = uhost.dev_class;
    b[14] = (uint8_t)(uhost.capacity);
    b[15] = (uint8_t)(uhost.capacity >> 8);
    b[16] = (uint8_t)(uhost.capacity >> 16);
    b[17] = (uint8_t)(uhost.capacity >> 24);
    b[18] = (uint8_t)(uhost.sector_size);
    b[19] = (uint8_t)(uhost.sector_size >> 8);
    b[20] = (uint8_t)(uhost.sector_size >> 16);
    b[21] = (uint8_t)(uhost.sector_size >> 24);
    b[22] = (uint8_t)usb_host_sie_rd(SIE_FADDR);
    b[23] = (uint8_t)usb_host_sie_rd(SIE_POWER);
    b[24] = (uint8_t)usb_host_sie_rd(SIE_DEVCTL);
    b[25] = (uint8_t)usb_host_sie_rd(SIE_INTRUSB);
    b[26] = (uint8_t)fm1_usb_line();
    b[27] = (uhost.state != UHOST_DISCONNECTED) ? 1 : 0;
    uint32_t con0 = FM1_USB_CON0;
    b[28] = (uint8_t)((con0 >> 16) & 0xFF);
    b[29] = (uint8_t)((con0 >> 24) & 0xFF);
    b[30] = (uint8_t)usb_host_sie_rd(SIE_CSR0);
    b[31] = (uint8_t)usb_host_sie_rd(SIE_TXCSR1);
    b[32] = (uint8_t)usb_host_sie_rd(SIE_RXCSR1);
    b[33] = uhost.lun;
    b[34] = uhost.csw_status;
    b[35] = uhost.sense_key;
    b[36] = uhost.asc;
    b[37] = uhost.ascq;
    b[38] = uhost.inq_pdt;
    b[39] = uhost.inq_rmb;
    b[40] = uhost.tur_tries;
    b[41] = uhost.ctl_rt;
    b[42] = uhost.ctl_rq;
    b[43] = uhost.ctl_stage;
    b[44] = (uint8_t)uhost.ctl_r;
    b[45] = uhost.ctl_csr0;
    b[46] = uhost.ctl_faddr;
    b[49] = usb_hub_mounted;
    b[50] = usb_hub_dead;
    for (i = 0; i < USB_MAX_DEVICES && n + 4 <= max; i++) {
        if (usb_devtab[i].addr == 0) continue;
        b[n++] = usb_devtab[i].addr;
        b[n++] = usb_devtab[i].hub_addr;
        b[n++] = usb_devtab[i].hub_port;
        b[n++] = usb_devtab[i].dev_class;
        ndev++;
    }
    b[47] = (uint8_t)ndev;
    for (i = 0; i < usb_hub_msc_count && n + 9 <= max; i++) {
        const usb_hub_msc_t *m = &usb_hub_msc_list[i];
        b[n++] = m->addr;
        b[n++] = m->hub_addr;
        b[n++] = m->hub_port;
        b[n++] = m->ep_in;
        b[n++] = m->ep_out;
        b[n++] = (uint8_t)(m->vid & 0xFF);
        b[n++] = (uint8_t)(m->vid >> 8);
        b[n++] = (uint8_t)(m->pid & 0xFF);
        b[n++] = (uint8_t)(m->pid >> 8);
        b[48]++;
    }
    /* USB network candidate: addr, cfg value, iface, alt, ep in, ep out (zeros: none) */
    if (n + 6 <= max) {
        b[n++] = usb_net_cand.addr;
        b[n++] = usb_net_cand.cfg_value;
        b[n++] = usb_net_cand.iface;
        b[n++] = usb_net_cand.alt;
        b[n++] = usb_net_cand.addr ? (uint8_t)(0x80u | usb_net_cand.ep_in) : 0;
        b[n++] = usb_net_cand.ep_out;
    }
    return n;
}
