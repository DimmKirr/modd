// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB network link: Apple's USB tethering interface as an lwIP netif.
 * Behaviour follows the public description of the interface in the Linux
 * ipheth driver (written from its request table, no code copied):
 * receive = 2-byte pad + Ethernet frame per bulk transfer, transmit = the
 * raw frame, carrier polled with a vendor request once a second.
 * The phone sits on host endpoint 2; the disk keeps endpoint 1. */

#ifdef CONFIG_USB_NET

#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#define USBNET_HOST_EP      2
#define USBNET_MTU          1500u
#define USBNET_FRAME_MAX    (USBNET_MTU + 14u)
#define USBNET_RX_PAD       2u
#define USBNET_CARRIER_MS   1000u

#define APPLE_REQ_GET_MACADDR    0x00
#define APPLE_REQ_CARRIER_CHECK  0x45
#define APPLE_CARRIER_ON         0x04

static struct netif usbnet_netif;
static uint8_t usbnet_up, usbnet_carrier;
static uint8_t usbnet_mac[6];
static uint32_t usbnet_carrier_at;
static uint8_t usbnet_dma_tx[64] __attribute__((aligned(4), section(".pool_net")));
static uint8_t usbnet_dma_rx[68] __attribute__((aligned(4), section(".pool_net")));
static uint8_t usbnet_rx_frame[USBNET_RX_PAD + USBNET_FRAME_MAX] __attribute__((section(".pool_net")));
static uint8_t usbnet_tx_frame[USBNET_FRAME_MAX] __attribute__((section(".pool_net")));
static uint32_t usbnet_rx_len;
static uint8_t usbnet_rx_armed;
static uint32_t usbnet_rx_frames, usbnet_tx_frames, usbnet_rx_drops;

/* One FADDR on the SIE: point it at the phone before every transfer. */
static void usbnet_select(void)
{
    usb_host_sie_wr(SIE_FADDR, usb_net_cand.addr);
    uhost.ep0_mps = usb_net_cand.ep0_mps;
}

static int usbnet_vendor_in(uint8_t req, uint8_t *buf, uint16_t len)
{
    usbnet_select();
    return usb_control_msg(0xC0, req, 0, usb_net_cand.iface, buf, len);
}

/* ---- lwIP glue ---- */

static err_t usbnet_linkoutput(struct netif *nif, struct pbuf *p)
{
    uint32_t len = 0;
    struct pbuf *q;
    (void)nif;
    for (q = p; q; q = q->next) {
        uint32_t chunk = q->len;
        if (len + chunk > USBNET_FRAME_MAX) chunk = USBNET_FRAME_MAX - len;
        memcpy(usbnet_tx_frame + len, q->payload, chunk);
        len += chunk;
    }
    if (len < 14u || !usbnet_carrier) return ERR_IF;
    usbnet_select();
    if (usb_bulk_out(USBNET_HOST_EP, usbnet_dma_tx, usb_net_cand.max_packet,
                     usb_net_cand.ep_out, usbnet_tx_frame, len) < 0)
        return ERR_IF;
    usbnet_tx_frames++;
    return ERR_OK;
}

static err_t usbnet_netif_init(struct netif *nif)
{
    nif->name[0] = 'u';
    nif->name[1] = 's';
    nif->output = etharp_output;
    nif->linkoutput = usbnet_linkoutput;
    nif->mtu = USBNET_MTU;
    nif->hwaddr_len = 6;
    memcpy(nif->hwaddr, usbnet_mac, 6);
    nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    return ERR_OK;
}

static void usbnet_status_cb(struct netif *nif)
{
    if (nif->ip_addr.addr) {
        modd_log("usbnet: dhcp bound");
        net_on_up(nif);
    }
}

/* ---- bring-up, called from hub discovery ---- */

static int usbnet_is_up(void) { return usbnet_up; }

static int usbnet_attach(void)
{
    int r;
#ifdef CONFIG_DOCK
    if (dock_active) {                          /* its RAM is the network's (.pool_net overlay) */
        modd_log("usbnet: a module is running, not attaching");
        return -1;
    }
#endif
    usbnet_select();
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_CONFIG, usb_net_cand.cfg_value, 0, 0, 0);
    if (r < 0) { modd_log_int("usbnet: set cfg fail ", r); return r; }
    fm1_delay_ms(50);
    /* SET_INTERFACE: recipient interface, wValue = alternate setting */
    r = usb_control_msg(0x01, 0x0B, usb_net_cand.alt, usb_net_cand.iface, 0, 0);
    if (r < 0) { modd_log_int("usbnet: set iface fail ", r); return r; }
    r = usbnet_vendor_in(APPLE_REQ_GET_MACADDR, usbnet_mac, 6);
    if (r < 6) { modd_log_int("usbnet: mac fail ", r); return r < 0 ? r : -1; }
    modd_log_hex("usbnet: mac ", usbnet_mac, 6);

    /* host EP2: bulk pair to the phone, same register recipe as EP1 for the disk */
    fm1_usb_ep_txbuf(USBNET_HOST_EP, usbnet_dma_tx);
    fm1_usb_ep_rxbuf(USBNET_HOST_EP, usbnet_dma_rx);
    usb_host_sie_wr(SIE_INDEX, USBNET_HOST_EP);
    usb_host_sie_wr(SIE_TXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_TXCSR1, 0);
    usb_host_sie_wr(SIE_TXCSR1, 0x48u);
    usb_host_sie_wr(SIE_TXCSR2, 0x20u);
    usb_host_sie_wr(SIE_TXTYPE, (uint32_t)(0x20u | usb_net_cand.ep_out));
    usb_host_sie_wr(SIE_RXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_RXCSR1, 0);
    usb_host_sie_wr(SIE_RXCSR1, 0x90u);
    usb_host_sie_wr(SIE_RXCSR2, 0);
    usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x20u | usb_net_cand.ep_in));
    fm1_usb_ep_enable((1u << USB_BULK_HOST_EP) | (1u << USBNET_HOST_EP));

    net_init();
    if (!usbnet_up) {
        ip4_addr_t z;
        ip4_addr_set_zero(&z);
        netif_add(&usbnet_netif, &z, &z, &z, 0, usbnet_netif_init, ethernet_input);
        netif_set_status_callback(&usbnet_netif, usbnet_status_cb);
        usbnet_up = 1;
    } else {
        memcpy(usbnet_netif.hwaddr, usbnet_mac, 6);
    }
    usbnet_carrier = 0;
    usbnet_carrier_at = 0;
    usbnet_rx_armed = 0;
    usbnet_rx_len = 0;
    modd_log("usbnet: attached");
    return 0;
}

static void usbnet_detach(void)
{
    if (!usbnet_up) return;
    if (usbnet_carrier) {
        dhcp_stop(&usbnet_netif);
        netif_set_link_down(&usbnet_netif);
        netif_set_down(&usbnet_netif);
    }
    usbnet_carrier = 0;
    usbnet_rx_armed = 0;
    modd_log("usbnet: detached");
}

/* ---- carrier: the phone says whether the hotspot link is up ---- */

static void usbnet_carrier_poll(uint32_t now)
{
    uint8_t b[2] = { 0, 0 };
    uint8_t on;
    int r;
    if (usbnet_carrier_at && now - usbnet_carrier_at < USBNET_CARRIER_MS) return;
    usbnet_carrier_at = now ? now : 1;
    r = usbnet_vendor_in(APPLE_REQ_CARRIER_CHECK, b, 2);
    on = (r == 1 && b[0] == APPLE_CARRIER_ON) || (r >= 2 && b[1] == APPLE_CARRIER_ON);
    if (on == usbnet_carrier) return;
    usbnet_carrier = on;
    if (on) {
        modd_log("usbnet: carrier on");
        netif_set_up(&usbnet_netif);
        netif_set_link_up(&usbnet_netif);
        dhcp_start(&usbnet_netif);
    } else {
        modd_log_int("usbnet: carrier off r=", r);
        dhcp_stop(&usbnet_netif);
        netif_set_link_down(&usbnet_netif);
    }
}

/* ---- receive: one ReqPkt outstanding, never blocks ---- */

static void usbnet_rx_deliver(void)
{
    uint32_t len = usbnet_rx_len;
    usbnet_rx_len = 0;
    if (len <= USBNET_RX_PAD + 14u) return;
    len -= USBNET_RX_PAD;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (uint16_t)len, PBUF_POOL);
    if (!p) { usbnet_rx_drops++; return; }
    pbuf_take(p, usbnet_rx_frame + USBNET_RX_PAD, (uint16_t)len);
    usbnet_rx_frames++;
    if (usbnet_netif.input(p, &usbnet_netif) != ERR_OK)
        pbuf_free(p);
}

static void usbnet_rx_poll(void)
{
    uint32_t csr, n;
    usbnet_select();
    usb_host_sie_wr(SIE_INDEX, USBNET_HOST_EP);
    if (!usbnet_rx_armed) {
        fm1_usb_ep_rxbuf(USBNET_HOST_EP, usbnet_dma_rx);
        usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x20u | usb_net_cand.ep_in));
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);           /* ReqPkt */
        usbnet_rx_armed = 1;
        return;
    }
    csr = usb_host_sie_rd(SIE_RXCSR1);
    if (csr & 0x40u) {                                /* stall: drop the frame */
        usb_host_sie_wr(SIE_RXCSR1, 0);
        usbnet_rx_armed = 0;
        usbnet_rx_len = 0;
        return;
    }
    if (csr & 0x0Cu) {                                /* error: ask again */
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        return;
    }
    if (!(csr & 0x01u)) return;                       /* nothing yet */
    n = usb_host_sie_rd(SIE_RXCOUNT1) | (usb_host_sie_rd(SIE_RXCOUNT2) << 8);
    if (n > 64u) n = 64u;
    fm1_usb_rx_sync();
    if (usbnet_rx_len + n <= sizeof usbnet_rx_frame) {
        memcpy(usbnet_rx_frame + usbnet_rx_len, usbnet_dma_rx, n);
        usbnet_rx_len += n;
    }
    usb_host_sie_wr(SIE_RXCSR1, 0x10u);
    usbnet_rx_armed = 0;
    if (n == usb_net_cand.max_packet) return;         /* the frame continues */
    usbnet_rx_deliver();
}

static void usbnet_poll(void)
{
    if (!usbnet_up || otg.mode != OTG_HOST || uhost.state == UHOST_DISCONNECTED) return;
    usbnet_carrier_poll(fm1_ms);
    if (usbnet_carrier) usbnet_rx_poll();
}

static void usbnet_init(void)
{
    usbnet_up = 0;
    usbnet_wanted = 0;
}

static void usbnet_set_wanted(uint8_t on)
{
    usbnet_wanted = on;
    modd_log(on ? "usbnet: wanted" : "usbnet: off");
}

#else /* !CONFIG_USB_NET */

static void usbnet_init(void) {}
static void usbnet_poll(void) {}
static int usbnet_is_up(void) { return 0; }

#endif /* CONFIG_USB_NET */
