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

#define CDC_SET_ETH_PACKET_FILTER  0x43
#define ECM_PACKET_FILTER  0x000Du   /* directed + broadcast + all multicast */

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

/* Parse a USB string descriptor (UTF-16LE) with 12 hex-encoded MAC digits. */
static int ecm_parse_mac_string(const uint8_t *desc, uint8_t len, uint8_t *mac)
{
    uint8_t i, hi, lo, c1, c2;
    if (len < 26 || desc[1] != 0x03) return -1;
    for (i = 0; i < 6; i++) {
        c1 = desc[2 + i * 4];
        c2 = desc[2 + i * 4 + 2];
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

    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_CONFIG,
                        usb_ecm_cand.cfg_value, 0, 0, 0);
    if (r < 0) { modd_log_int("ecm: set cfg fail ", r); return r; }
    fm1_delay_ms(50);

    r = usb_control_msg(0x01, 0x0B, usb_ecm_cand.data_alt,
                        usb_ecm_cand.data_iface, 0, 0);
    if (r < 0) { modd_log_int("ecm: set iface fail ", r); return r; }

    if (usb_ecm_cand.mac_str_idx) {
        r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                            (0x03 << 8) | usb_ecm_cand.mac_str_idx,
                            0x0409, str_buf, sizeof str_buf);
        if (r >= 26 && ecm_parse_mac_string(str_buf, (uint8_t)r, ecm_mac) == 6) {
            modd_log_hex("ecm: mac ", ecm_mac, 6);
        } else {
            modd_log_int("ecm: mac string fail r=", r);
            ecm_mac[0] = 0x02; ecm_mac[1] = 0x00; ecm_mac[2] = 0xEC;
            ecm_mac[3] = (uint8_t)(fm1_ms >> 16);
            ecm_mac[4] = (uint8_t)(fm1_ms >> 8);
            ecm_mac[5] = (uint8_t)fm1_ms;
        }
    }

    r = usb_control_msg(0x21, CDC_SET_ETH_PACKET_FILTER, ECM_PACKET_FILTER,
                        usb_ecm_cand.ctrl_iface, 0, 0);
    if (r < 0) modd_log_int("ecm: set filter fail ", r);

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
    net_on_down(&ecm_netif);
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
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        ecm_rx_armed = 1;
        return;
    }
    csr = usb_host_sie_rd(SIE_RXCSR1);
    if (csr & 0x40u) {
        usb_host_sie_wr(SIE_RXCSR1, 0);
        ecm_rx_armed = 0;
        ecm_rx_len = 0;
        return;
    }
    if (csr & 0x0Cu) {
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        return;
    }
    if (!(csr & 0x01u)) return;
    n = usb_host_sie_rd(SIE_RXCOUNT1) | (usb_host_sie_rd(SIE_RXCOUNT2) << 8);
    if (n > 64u) n = 64u;
    fm1_usb_rx_sync();
    if (ecm_rx_len + n <= sizeof ecm_rx_frame) {
        memcpy(ecm_rx_frame + ecm_rx_len, ecm_dma_rx, n);
        ecm_rx_len += n;
    }
    usb_host_sie_wr(SIE_RXCSR1, 0x10u);
    ecm_rx_armed = 0;
    if (n == usb_ecm_cand.max_packet) return;
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
#ifdef CONFIG_USB_NET_AUTO
    ecm_wanted = 1;
#endif
}

static void ecm_set_wanted(uint8_t on)
{
    ecm_wanted = on;
    modd_log(on ? "ecm: wanted" : "ecm: off");
}

#else /* !CONFIG_USB_NET_ECM */

static void ecm_init(void) {}
static void ecm_poll(void) {}
static int ecm_is_up(void) { return 0; }

#endif /* CONFIG_USB_NET_ECM */
