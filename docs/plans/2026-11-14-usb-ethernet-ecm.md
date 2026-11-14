# USB Ethernet (CDC-ECM) + Multi-Interface Layer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** When a standard USB Ethernet adapter (e.g. ASIX AX88179A) is connected via the dock hub, the FM-1 uses it as an lwIP network interface (`en0`) alongside WiFi (`wf0`) and iPhone tethering (`us0`), with priority-based default route election.

**Architecture:** A new CDC-ECM link driver (`usbnet_ecm.c`) mirrors the existing `usbnet.c` (ipheth) pattern: lwIP netif, poll-based bulk rx/tx, DHCP. The hub enumeration detects ECM-capable devices (class 02/06), switches to the ECM configuration if needed (ASIX AX88179A keeps ECM at config 3), parses the ECM functional descriptor for the MAC address string index, and hands off to the ECM attach. `net.c` gains a priority-based `net_elect_default()` that picks the highest-priority up netif (`en` > `wf` > `us`) on any link state change.

**Tech Stack:** C (bare-metal, JieLi AC791N), lwIP 2.2.0, USB host (MUSB SIE), Kconfiglib

---

### Task 1: Kconfig symbols for CDC-ECM

**Files:**
- Modify: `kconf/net.kconfig`

**Step 1: Add CONFIG_USB_NET_ECM symbol**

Add after the `USB_NET_AUTO` block, before `endmenu`:

```kconfig
config USB_NET_ECM
    bool "USB CDC-ECM ethernet adapter"
    depends on USB_HOST
    select LWIP
    default n
    help
      CDC-ECM host driver for standard USB Ethernet adapters (e.g.
      ASIX AX88179A). Devices that expose a CDC-ECM configuration
      are switched to it automatically. Shares the OTG port and
      .pool_net RAM with USB_NET (iPhone tethering).
```

**Step 2: Verify kconfig parses**

Run: `task config:generate:local`
Expected: no errors

**Step 3: Commit**

```bash
git add kconf/net.kconfig
git commit -m "kconfig: add CONFIG_USB_NET_ECM for CDC-ECM ethernet adapters"
```

---

### Task 2: CDC-ECM link driver (`usbnet_ecm.c`)

This is the core driver. It follows the same structure as `usbnet.c` but implements the CDC-ECM protocol instead of Apple ipheth.

**Files:**
- Create: `firmware/src/net/usbnet_ecm.c`

**Step 1: Write the CDC-ECM driver**

The driver needs:
- lwIP netif named `"en"` (Ethernet)
- `ecm_attach()`: called from hub discovery after ECM candidate is found
  - `SET_CONFIGURATION(cfg_value)` to switch to ECM config
  - `SET_INTERFACE(data_iface, alt=1)` to activate bulk endpoints
  - Parse MAC from string descriptor (ECM functional descriptor's `iMACAddress`)
  - `SET_ETHERNET_PACKET_FILTER` (class request 0x43, bmRequestType 0x21)
  - Configure host EP2 (same SIE register recipe as ipheth)
  - Register lwIP netif, start DHCP
- `ecm_detach()`: tear down DHCP and netif link
- `ecm_poll()`: poll bulk rx, deliver frames to lwIP
- `ecm_init()` / stubs when `CONFIG_USB_NET_ECM` is not defined
- DMA and frame buffers in `.pool_net` section (shared overlay with dock)
- No rx pad (unlike ipheth which has 2-byte pad): ECM is raw Ethernet frames

Key differences from ipheth (`usbnet.c`):
1. No `APPLE_REQ_*` vendor requests
2. MAC comes from USB string descriptor (hex UTF-16), not vendor request
3. No carrier polling: link is always up once attached (ECM spec says use interrupt endpoint for link notifications, but we skip that and assume link-up, as the adapter is physically wired)
4. No 2-byte rx pad: frames are raw Ethernet
5. Packet filter: `SET_ETHERNET_PACKET_FILTER` class request

```c
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB network link: CDC-ECM (Ethernet Control Model) as an lwIP netif.
 * Standard USB class protocol: raw Ethernet frames on the bulk pair,
 * MAC from the ECM functional descriptor's string index, one class
 * request (SET_ETHERNET_PACKET_FILTER). Works with any USB Ethernet
 * adapter that exposes a CDC-ECM configuration (e.g. ASIX AX88179A
 * at config 3). */

#ifdef CONFIG_USB_NET_ECM

#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#define ECM_HOST_EP         2
#define ECM_MTU             1500u
#define ECM_FRAME_MAX       (ECM_MTU + 14u)

/* CDC class request: SET_ETHERNET_PACKET_FILTER */
#define CDC_SET_ETH_PACKET_FILTER  0x43
/* Filter bits: directed + broadcast + all multicast */
#define ECM_PACKET_FILTER  0x000Du

static struct netif ecm_netif;
static uint8_t ecm_up;
static uint8_t ecm_mac[6];
static uint8_t ecm_dma_tx[64] __attribute__((aligned(4), section(".pool_net")));
static uint8_t ecm_dma_rx[68] __attribute__((aligned(4), section(".pool_net")));
static uint8_t ecm_rx_frame[ECM_FRAME_MAX] __attribute__((section(".pool_net")));
static uint8_t ecm_tx_frame[ECM_FRAME_MAX] __attribute__((section(".pool_net")));
static uint32_t ecm_rx_len;
static uint8_t ecm_rx_armed;
static uint32_t ecm_rx_frames, ecm_tx_frames, ecm_rx_drops;

static void ecm_select(void)
{
    usb_host_sie_wr(SIE_FADDR, usb_ecm_cand.addr);
    uhost.ep0_mps = usb_ecm_cand.ep0_mps;
}

/* Parse a USB string descriptor (UTF-16LE) containing hex-encoded MAC
 * address (12 hex digits = 6 bytes). Returns 6 on success, <0 on error. */
static int ecm_parse_mac_string(const uint8_t *desc, uint8_t len, uint8_t *mac)
{
    uint8_t i, hi, lo;
    /* String descriptor: bLength, bDescriptorType(3), then UTF-16LE chars.
     * 12 hex digits = 12 UTF-16 chars = 24 bytes of char data + 2 header = 26 */
    if (len < 26 || desc[1] != 0x03) return -1;
    for (i = 0; i < 6; i++) {
        uint8_t c1 = desc[2 + i * 4];      /* low byte of UTF-16 char */
        uint8_t c2 = desc[2 + i * 4 + 2];
        hi = (c1 >= 'a') ? (c1 - 'a' + 10) : (c1 >= 'A') ? (c1 - 'A' + 10) : (c1 - '0');
        lo = (c2 >= 'a') ? (c2 - 'a' + 10) : (c2 >= 'A') ? (c2 - 'A' + 10) : (c2 - '0');
        if (hi > 15 || lo > 15) return -2;
        mac[i] = (hi << 4) | lo;
    }
    return 6;
}

/* ---- lwIP glue ---- */

static err_t ecm_linkoutput(struct netif *nif, struct pbuf *p)
{
    uint32_t len = 0;
    struct pbuf *q;
    (void)nif;
    for (q = p; q; q = q->next) {
        uint32_t chunk = q->len;
        if (len + chunk > ECM_FRAME_MAX) chunk = ECM_FRAME_MAX - len;
        memcpy(ecm_tx_frame + len, q->payload, chunk);
        len += chunk;
    }
    if (len < 14u || !ecm_up) return ERR_IF;
    ecm_select();
    if (usb_bulk_out(ECM_HOST_EP, ecm_dma_tx, usb_ecm_cand.max_packet,
                     usb_ecm_cand.ep_out, ecm_tx_frame, len) < 0)
        return ERR_IF;
    ecm_tx_frames++;
    return ERR_OK;
}

static err_t ecm_netif_init(struct netif *nif)
{
    nif->name[0] = 'e';
    nif->name[1] = 'n';
    nif->output = etharp_output;
    nif->linkoutput = ecm_linkoutput;
    nif->mtu = ECM_MTU;
    nif->hwaddr_len = 6;
    memcpy(nif->hwaddr, ecm_mac, 6);
    nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    return ERR_OK;
}

static void ecm_status_cb(struct netif *nif)
{
    if (nif->ip_addr.addr) {
        modd_log("ecm: dhcp bound");
        net_on_up(nif);
    }
}

/* ---- bring-up, called from hub discovery ---- */

static int ecm_is_up(void) { return ecm_up; }

static int ecm_attach(void)
{
    int r;
    uint8_t str_buf[64];
#ifdef CONFIG_DOCK
    if (dock_active) {
        modd_log("ecm: a module is running, not attaching");
        return -1;
    }
#endif
    ecm_select();

    /* Switch to ECM configuration */
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_CONFIG,
                        usb_ecm_cand.cfg_value, 0, 0, 0);
    if (r < 0) { modd_log_int("ecm: set cfg fail ", r); return r; }
    fm1_delay_ms(50);

    /* Activate data interface alternate setting 1 (bulk endpoints) */
    r = usb_control_msg(0x01, 0x0B, usb_ecm_cand.data_alt,
                        usb_ecm_cand.data_iface, 0, 0);
    if (r < 0) { modd_log_int("ecm: set iface fail ", r); return r; }

    /* Get MAC from string descriptor */
    if (usb_ecm_cand.mac_str_idx) {
        r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                            (0x03 << 8) | usb_ecm_cand.mac_str_idx,
                            0x0409, str_buf, sizeof str_buf);
        if (r >= 26 && ecm_parse_mac_string(str_buf, (uint8_t)r, ecm_mac) == 6) {
            modd_log_hex("ecm: mac ", ecm_mac, 6);
        } else {
            modd_log_int("ecm: mac string fail r=", r);
            /* fallback: use a locally-administered random-ish MAC */
            ecm_mac[0] = 0x02; ecm_mac[1] = 0x00; ecm_mac[2] = 0xEC;
            ecm_mac[3] = (uint8_t)(fm1_ms >> 16);
            ecm_mac[4] = (uint8_t)(fm1_ms >> 8);
            ecm_mac[5] = (uint8_t)fm1_ms;
        }
    }

    /* SET_ETHERNET_PACKET_FILTER: directed + broadcast + all multicast */
    r = usb_control_msg(0x21, CDC_SET_ETH_PACKET_FILTER, ECM_PACKET_FILTER,
                        usb_ecm_cand.ctrl_iface, 0, 0);
    if (r < 0) modd_log_int("ecm: set filter fail ", r);
    /* non-fatal: some devices ignore this */

    /* Configure host EP2: bulk pair */
    fm1_usb_ep_txbuf(ECM_HOST_EP, ecm_dma_tx);
    fm1_usb_ep_rxbuf(ECM_HOST_EP, ecm_dma_rx);
    usb_host_sie_wr(SIE_INDEX, ECM_HOST_EP);
    usb_host_sie_wr(SIE_TXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_TXCSR1, 0);
    usb_host_sie_wr(SIE_TXCSR1, 0x48u);
    usb_host_sie_wr(SIE_TXCSR2, 0x20u);
    usb_host_sie_wr(SIE_TXTYPE, (uint32_t)(0x20u | usb_ecm_cand.ep_out));
    usb_host_sie_wr(SIE_RXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_RXCSR1, 0);
    usb_host_sie_wr(SIE_RXCSR1, 0x90u);
    usb_host_sie_wr(SIE_RXCSR2, 0);
    usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x20u | usb_ecm_cand.ep_in));
    fm1_usb_ep_enable((1u << USB_BULK_HOST_EP) | (1u << ECM_HOST_EP));

    net_init();
    if (!ecm_up) {
        ip4_addr_t z;
        ip4_addr_set_zero(&z);
        netif_add(&ecm_netif, &z, &z, &z, 0, ecm_netif_init, ethernet_input);
        netif_set_status_callback(&ecm_netif, ecm_status_cb);
    } else {
        memcpy(ecm_netif.hwaddr, ecm_mac, 6);
    }
    ecm_up = 1;
    ecm_rx_armed = 0;
    ecm_rx_len = 0;

    /* ECM link is up as soon as we attach (wired connection) */
    netif_set_up(&ecm_netif);
    netif_set_link_up(&ecm_netif);
    dhcp_start(&ecm_netif);

    modd_log("ecm: attached");
    return 0;
}

static void ecm_detach(void)
{
    if (!ecm_up) return;
    dhcp_stop(&ecm_netif);
    netif_set_link_down(&ecm_netif);
    netif_set_down(&ecm_netif);
    ecm_rx_armed = 0;
    modd_log("ecm: detached");
}

/* ---- receive: one ReqPkt outstanding, never blocks ---- */

static void ecm_rx_deliver(void)
{
    uint32_t len = ecm_rx_len;
    ecm_rx_len = 0;
    if (len < 14u) return;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (uint16_t)len, PBUF_POOL);
    if (!p) { ecm_rx_drops++; return; }
    pbuf_take(p, ecm_rx_frame, (uint16_t)len);
    ecm_rx_frames++;
    if (ecm_netif.input(p, &ecm_netif) != ERR_OK)
        pbuf_free(p);
}

static void ecm_rx_poll(void)
{
    uint32_t csr, n;
    ecm_select();
    usb_host_sie_wr(SIE_INDEX, ECM_HOST_EP);
    if (!ecm_rx_armed) {
        fm1_usb_ep_rxbuf(ECM_HOST_EP, ecm_dma_rx);
        usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x20u | usb_ecm_cand.ep_in));
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);           /* ReqPkt */
        ecm_rx_armed = 1;
        return;
    }
    csr = usb_host_sie_rd(SIE_RXCSR1);
    if (csr & 0x40u) {                                /* stall */
        usb_host_sie_wr(SIE_RXCSR1, 0);
        ecm_rx_armed = 0;
        ecm_rx_len = 0;
        return;
    }
    if (csr & 0x0Cu) {                                /* error */
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        return;
    }
    if (!(csr & 0x01u)) return;                       /* nothing yet */
    n = usb_host_sie_rd(SIE_RXCOUNT1) | (usb_host_sie_rd(SIE_RXCOUNT2) << 8);
    if (n > 64u) n = 64u;
    fm1_usb_rx_sync();
    if (ecm_rx_len + n <= sizeof ecm_rx_frame) {
        memcpy(ecm_rx_frame + ecm_rx_len, ecm_dma_rx, n);
        ecm_rx_len += n;
    }
    usb_host_sie_wr(SIE_RXCSR1, 0x10u);
    ecm_rx_armed = 0;
    if (n == usb_ecm_cand.max_packet) return;         /* frame continues */
    ecm_rx_deliver();
}

static void ecm_poll(void)
{
    if (!ecm_up || otg.mode != OTG_HOST || uhost.state == UHOST_DISCONNECTED) return;
    ecm_rx_poll();
}

static void ecm_init(void)
{
    ecm_up = 0;
}

#else /* !CONFIG_USB_NET_ECM */

static void ecm_init(void) {}
static void ecm_poll(void) {}
static int ecm_is_up(void) { return 0; }

#endif /* CONFIG_USB_NET_ECM */
```

**Step 2: Commit**

```bash
git add firmware/src/net/usbnet_ecm.c
git commit -m "net: add CDC-ECM host driver for standard USB Ethernet adapters"
```

---

### Task 3: Hub enumeration: ECM candidate detection

**Files:**
- Modify: `firmware/src/udisk/usb_hub.c`

The hub already iterates all configurations of multi-config devices (lines 441-464). We need to:

1. Add `usb_ecm_cand_t` struct (extends `usb_net_cand_t` pattern with ECM-specific fields)
2. Add `usb_hub_parse_ecm()` to find CDC-ECM interfaces in a config descriptor
3. Replace the `usb_hub_on_cdc()` stub to attempt ECM attach
4. Wire `ecm_attach()` into the hub discovery completion (alongside the existing ipheth path)

**Step 1: Add ECM candidate struct and forward declarations**

After `usb_net_cand_t` (line ~107), add:

```c
#ifdef CONFIG_USB_NET_ECM
/* CDC-ECM candidate: a standard Ethernet adapter in one of the device's
 * configurations. cfg_value selects the ECM config, data_iface/data_alt
 * the data interface's alternate setting with bulk endpoints. */
typedef struct {
    uint8_t  addr, hub_addr, hub_port;
    uint8_t  cfg_value;
    uint8_t  ctrl_iface;        /* CDC control interface */
    uint8_t  data_iface;        /* CDC data interface */
    uint8_t  data_alt;          /* alternate setting with bulk endpoints */
    uint8_t  ep_in, ep_out;
    uint8_t  ep0_mps;
    uint16_t max_packet;
    uint8_t  mac_str_idx;       /* iMACAddress from ECM functional descriptor */
} usb_ecm_cand_t;

static usb_ecm_cand_t usb_ecm_cand;
static uint8_t ecm_wanted;
static int ecm_attach(void);
static void ecm_detach(void);
#endif
```

**Step 2: Add ECM config descriptor parser**

After `usb_hub_parse_apple_net()` (line ~296), add:

```c
#ifdef CONFIG_USB_NET_ECM
/* CDC functional descriptor subtypes (USB CDC 1.2, Table 13) */
#define CDC_CS_INTERFACE     0x24
#define CDC_SUBTYPE_HEADER   0x00
#define CDC_SUBTYPE_UNION    0x06
#define CDC_SUBTYPE_ECM      0x0F

/* Parse a configuration descriptor for a CDC-ECM interface.
 * Returns 1 and fills usb_ecm_cand if found. */
static int usb_hub_parse_ecm(uint8_t addr, uint8_t hub_addr, uint8_t hub_port,
                             const usb_config_desc_t *cfg, uint16_t total)
{
    uint32_t pos = cfg->bLength;
    uint8_t ctrl_iface = 0xFF, data_iface = 0xFF;
    uint8_t mac_str = 0;
    uint8_t in_ecm_ctrl = 0;
    uint8_t ep_in = 0, ep_out = 0, data_alt = 0;
    uint16_t maxp = 64;

    /* First pass: find the ECM control interface (class 02/06/00)
     * and its ECM functional descriptor with iMACAddress. */
    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;

        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            const usb_interface_desc_t *ifc =
                (const usb_interface_desc_t *)&usb_enum_buf[pos];
            /* CDC-ECM control interface: class 02, subclass 06 */
            in_ecm_ctrl = (ifc->bInterfaceClass == 0x02 &&
                          ifc->bInterfaceSubClass == 0x06 &&
                          ifc->bAlternateSetting == 0);
            if (in_ecm_ctrl)
                ctrl_iface = ifc->bInterfaceNumber;
        }

        /* ECM functional descriptor: CS_INTERFACE, subtype 0x0F */
        if (dtype == CDC_CS_INTERFACE && dlen >= 13 && in_ecm_ctrl) {
            uint8_t subtype = usb_enum_buf[pos + 2];
            if (subtype == CDC_SUBTYPE_ECM)
                mac_str = usb_enum_buf[pos + 3];  /* iMACAddress */
        }

        /* Union functional descriptor: find the data interface */
        if (dtype == CDC_CS_INTERFACE && dlen >= 5 && in_ecm_ctrl) {
            uint8_t subtype = usb_enum_buf[pos + 2];
            if (subtype == CDC_SUBTYPE_UNION)
                data_iface = usb_enum_buf[pos + 4]; /* bSubordinateInterface0 */
        }

        pos += dlen;
    }

    if (ctrl_iface == 0xFF || data_iface == 0xFF) return 0;

    /* Second pass: find data interface alt setting 1 with bulk endpoints */
    pos = cfg->bLength;
    uint8_t in_data_alt1 = 0;
    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;

        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            const usb_interface_desc_t *ifc =
                (const usb_interface_desc_t *)&usb_enum_buf[pos];
            in_data_alt1 = (ifc->bInterfaceNumber == data_iface &&
                           ifc->bAlternateSetting >= 1 &&
                           ifc->bInterfaceClass == 0x0A);
            if (in_data_alt1)
                data_alt = ifc->bAlternateSetting;
        }

        if (dtype == USB_DT_ENDPOINT && dlen >= 7 && in_data_alt1) {
            const usb_endpoint_desc_t *ep =
                (const usb_endpoint_desc_t *)&usb_enum_buf[pos];
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

    usb_ecm_cand.addr = addr;
    usb_ecm_cand.hub_addr = hub_addr;
    usb_ecm_cand.hub_port = hub_port;
    usb_ecm_cand.cfg_value = cfg->bConfigurationValue;
    usb_ecm_cand.ctrl_iface = ctrl_iface;
    usb_ecm_cand.data_iface = data_iface;
    usb_ecm_cand.data_alt = data_alt;
    usb_ecm_cand.ep_in = ep_in;
    usb_ecm_cand.ep_out = ep_out;
    usb_ecm_cand.ep0_mps = uhost.ep0_mps;
    usb_ecm_cand.max_packet = maxp > 64 ? 64 : maxp;
    usb_ecm_cand.mac_str_idx = mac_str;
    modd_log_int("hub: ECM found cfg=", cfg->bConfigurationValue);
    modd_log_int("hub:  ctrl_if=", ctrl_iface);
    modd_log_int("hub:  data_if=", data_iface);
    return 1;
}
#endif
```

**Step 3: Wire ECM parsing into the alternate-config loop**

In `usb_hub_discover_device()`, inside the `for (ci = 1; ...)` loop (line ~462), after the Apple net parsing, add:

```c
#ifdef CONFIG_USB_NET_ECM
        if (!usb_ecm_cand.addr && ddev.idVendor != USB_VID_APPLE)
            usb_hub_parse_ecm(addr, parent_hub, parent_port, cfg, total);
#endif
```

**Step 4: Wire ECM into the CDC stub handler**

Replace `usb_hub_on_cdc()` body:

```c
static void usb_hub_on_cdc(uint8_t addr, uint16_t vid, uint16_t pid)
{
    modd_log_int("hub: CDC/ethernet at addr=", addr);
    modd_log_int("hub:  VID=", vid);
    modd_log_int("hub:  PID=", pid);
#ifdef CONFIG_USB_NET_ECM
    /* If we haven't found an ECM candidate from alternate configs yet,
     * try parsing config 0 (the one already loaded in usb_enum_buf). */
    if (!usb_ecm_cand.addr && vid != USB_VID_APPLE) {
        usb_config_desc_t *cfg = (usb_config_desc_t *)usb_enum_buf;
        uint16_t total = cfg->wTotalLength;
        if (total > sizeof(usb_enum_buf)) total = sizeof(usb_enum_buf);
        usb_hub_parse_ecm(addr, 0, 0, cfg, total);
    }
#endif
}
```

**Step 5: Wire ECM attach into hub discovery completion**

In `usb_hub_discover()`, after the existing `CONFIG_USB_NET` block (line ~787), add:

```c
#ifdef CONFIG_USB_NET_ECM
    if (usb_ecm_cand.addr && ecm_wanted && ecm_attach() == 0)
        return 0;
#endif
```

**Step 6: Clear ECM candidate in `usb_devtab_clear()`**

In `usb_devtab_clear()`, add:

```c
#ifdef CONFIG_USB_NET_ECM
    usb_ecm_cand.addr = 0;
#endif
```

**Step 7: Commit**

```bash
git add firmware/src/udisk/usb_hub.c
git commit -m "usb_hub: detect CDC-ECM interfaces and parse ECM functional descriptors"
```

---

### Task 4: Wire ECM into modd.c, OTG, sys menu, dock

**Files:**
- Modify: `firmware/src/modd.c` (include + poll/init calls)
- Modify: `firmware/adapters/modd_otg.h` (OTG_HOST_WANTED, detach)
- Modify: `firmware/adapters/modd_sys_menu.h` (settings toggle)
- Modify: `firmware/src/dock/dock.c` (dock_net_busy)

**Step 1: Include in modd.c**

After the `usbnet.c` include (line ~728), add:

```c
#include "net/usbnet_ecm.c"      /* CDC-ECM ethernet (CONFIG_USB_NET_ECM), stubs otherwise */
```

Find the two `usbnet_poll()` calls (lines ~1088 and ~1567) and add after each:

```c
        ecm_poll();
```

Find `usbnet_init()` (line ~1321) and add after it:

```c
    ecm_init();
```

**Step 2: OTG adapter**

In `modd_otg.h`, expand `OTG_HOST_WANTED()`:

```c
#if defined(CONFIG_USB_NET) && defined(CONFIG_USB_NET_ECM)
#define OTG_HOST_WANTED() (udisk_active || usbnet_wanted || ecm_wanted)
#elif defined(CONFIG_USB_NET)
#define OTG_HOST_WANTED() (udisk_active || usbnet_wanted)
#elif defined(CONFIG_USB_NET_ECM)
#define OTG_HOST_WANTED() (udisk_active || ecm_wanted)
#else
#define OTG_HOST_WANTED() (udisk_active)
#endif
```

In `otg_switch_to_device()`, add ECM detach:

```c
#ifdef CONFIG_USB_NET_ECM
    ecm_detach();
#endif
```

Update the forward declarations at the top of the `CONFIG_USB_NET` block:

```c
#ifdef CONFIG_USB_NET_ECM
static void ecm_detach(void);
#endif
```

**Step 3: Sys menu toggle**

In `modd_sys_menu.h`, after the USB NET toggle (line ~98), add:

```c
#ifdef CONFIG_USB_NET_ECM
static uint8_t ecm_wanted;
static void    ecm_set_wanted(uint8_t on);
static uint8_t sm_ecm_get(void) { return ecm_wanted; }
static void    sm_ecm_set(uint8_t v) { ecm_set_wanted(v > 0u); }
#endif
```

And in `SYS_MENU[]`, after the USB NET entry:

```c
#if defined(CONFIG_SYS_MENU_NETWORK) && defined(CONFIG_USB_NET_ECM)
    { "USB ETH",      SMTAB_NETWORK, 2, SM_ONOFF,   sm_ecm_get,      sm_ecm_set },
#endif
```

Add a `ecm_set_wanted()` function in `usbnet_ecm.c` (inside the `#ifdef CONFIG_USB_NET_ECM` block, after `ecm_init`):

```c
static void ecm_set_wanted(uint8_t on)
{
    ecm_wanted = on;
    modd_log(on ? "ecm: wanted" : "ecm: off");
}
```

**Step 4: Dock guard**

In `firmware/src/dock/dock.c`, expand `dock_net_busy()`:

```c
#if defined(CONFIG_USB_NET) && defined(CONFIG_USB_NET_ECM)
static int dock_net_busy(void) { return usbnet_is_up() || ecm_is_up(); }
#elif defined(CONFIG_USB_NET)
static int dock_net_busy(void) { return usbnet_is_up(); }
#elif defined(CONFIG_USB_NET_ECM)
static int dock_net_busy(void) { return ecm_is_up(); }
#endif
```

**Step 5: Commit**

```bash
git add firmware/src/modd.c firmware/adapters/modd_otg.h \
        firmware/adapters/modd_sys_menu.h firmware/src/dock/dock.c
git commit -m "wire CDC-ECM driver into modd.c, OTG, sys menu, and dock guard"
```

---

### Task 5: Multi-interface default route election in `net.c`

**Files:**
- Modify: `firmware/src/net/net.c`

**Step 1: Add priority-based route election**

Replace the current `net_on_up()` with a priority-aware version. The priority order is: `en` (ECM, wired) > `wf` (WiFi) > `us` (iPhone tethering).

```c
/* Link priority: wired ethernet > WiFi > iPhone tethering.
 * The name[0..1] characters identify the interface. */
static uint8_t net_link_prio(struct netif *nif)
{
    if (nif->name[0] == 'e' && nif->name[1] == 'n') return 3; /* ECM */
    if (nif->name[0] == 'w' && nif->name[1] == 'f') return 2; /* WiFi */
    if (nif->name[0] == 'u' && nif->name[1] == 's') return 1; /* iPhone */
    return 0;
}

/* Pick the highest-priority netif that is up and has an IP address. */
static void net_elect_default(void)
{
    struct netif *best = 0;
    uint8_t best_prio = 0;
    struct netif *nif;
    NETIF_FOREACH(nif) {
        if (!netif_is_link_up(nif) || !nif->ip_addr.addr) continue;
        uint8_t p = net_link_prio(nif);
        if (p > best_prio) { best = nif; best_prio = p; }
    }
    if (best != netif_default) {
        netif_set_default(best);
        if (best)
            modd_log("net: default route changed");
    }
}

static void net_on_up(struct netif *nif)
{
    uint32_t ip = nif->ip_addr.addr;
    modd_log_hex("net: up ip=", (const uint8_t *)&ip, 4);
    modd_log_hex("net: gw=", (const uint8_t *)&nif->gw.addr, 4);
    net_elect_default();
#ifdef CONFIG_LWIP
    weather_fetch_start();
#endif
}

/* Called when any link goes down so we can fail over. */
static void net_on_down(struct netif *nif)
{
    (void)nif;
    net_elect_default();
}
```

**Step 2: Wire `net_on_down` from link-down paths**

In `usbnet.c`, in `usbnet_carrier_poll()` where carrier goes off (line ~186, after `netif_set_link_down`):

```c
        net_on_down(&usbnet_netif);
```

In `usbnet.c`, in `usbnet_detach()` (line ~159, after `netif_set_down`):

```c
        net_on_down(&usbnet_netif);
```

In `usbnet_ecm.c`, in `ecm_detach()` (after `netif_set_down`):

```c
    net_on_down(&ecm_netif);
```

In `wifi_lwip.c`, in `wifi_lwip_stop_dhcp()` (line ~148, after `netif_set_down`):

```c
    net_on_down(&wifi_netif);
```

**Step 3: Commit**

```bash
git add firmware/src/net/net.c firmware/src/net/usbnet.c \
        firmware/src/net/usbnet_ecm.c firmware/src/wifi/wifi_lwip.c
git commit -m "net: priority-based default route election across all link types"
```

---

### Task 6: Enable in defconfigs

**Files:**
- Modify: `configs/modd_defconfig`
- Modify: `configs/modd_dev_network_defconfig`

**Step 1: Enable ECM in both defconfigs**

In `modd_defconfig`, after `CONFIG_USB_NET=y`:

```
CONFIG_USB_NET_ECM=y
```

In `modd_dev_network_defconfig`, after `CONFIG_USB_NET_AUTO=y`:

```
CONFIG_USB_NET_ECM=y
```

**Step 2: Verify build**

Run: `task build`
Expected: successful build producing `.fwsc`

**Step 3: Commit**

```bash
git add configs/modd_defconfig configs/modd_dev_network_defconfig
git commit -m "defconfig: enable CDC-ECM ethernet in modd and dev network profiles"
```

---

### Task 7: Build verification and integration test

**Step 1: Full build with ECM enabled**

Run: `task build`
Expected: clean compile, `.fwsc` output

**Step 2: Run existing tests**

Run: `task test:unit`
Expected: all pass (ECM doesn't break existing genconfig/defconfig tests)

**Step 3: Squash any compile errors**

Fix any issues found: missing forward declarations, section attribute conflicts, Kconfig dependency problems.

**Step 4: Final commit**

```bash
git add -A
git commit -m "fix: resolve any build issues from CDC-ECM integration"
```

---

## Testing Notes

**Hardware test (requires dock hub + ASIX AX88179A):**
1. Enable `CONFIG_USB_NET_ECM=y` and `CONFIG_USB_NET_AUTO=y` (or toggle USB ETH in Settings)
2. Connect ASIX adapter through the dock hub
3. Expected logs: `hub: ECM found cfg=3`, `ecm: mac XX:XX:XX:XX:XX:XX`, `ecm: attached`, `ecm: dhcp bound`, `net: default route changed`
4. Weather page should populate via the wired link

**Multi-interface test:**
1. Have both WiFi and ASIX connected
2. ECM (`en`) should become default (priority 3 > WiFi's 2)
3. Disconnect ASIX: WiFi should take over as default

**Emulator:** No emulator support yet (item 3 in net-followups). Test with hardware only.
