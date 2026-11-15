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
static uint32_t ecm_rx_stalls, ecm_rx_errors;
static uint32_t ecm_stats_next;
static uint32_t ecm_rx_diag_n;

static void ecm_select(void)
{
    usb_host_sie_wr(SIE_FADDR, usb_ecm_cand.addr);
    FM1_EP_TXFUNCADDR(ECM_HOST_EP) = usb_ecm_cand.addr;
    FM1_EP_RXFUNCADDR(ECM_HOST_EP) = usb_ecm_cand.addr;
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
        toast_clear();
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
    usb_settle_ms(50);

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

    /* Diagnostic: log the candidate the ECM parser found */
    modd_log_int("ecm: addr=", usb_ecm_cand.addr);
    modd_log_int("ecm: hub=", usb_ecm_cand.hub_addr);
    modd_log_int("ecm: port=", usb_ecm_cand.hub_port);
    modd_log_int("ecm: ep_in=", usb_ecm_cand.ep_in);
    modd_log_int("ecm: ep_out=", usb_ecm_cand.ep_out);
    modd_log_int("ecm: maxpkt=", usb_ecm_cand.max_packet);
    modd_log_int("ecm: data_if=", usb_ecm_cand.data_iface);
    modd_log_int("ecm: data_alt=", usb_ecm_cand.data_alt);

    fm1_usb_ep_txbuf(ECM_HOST_EP, ecm_dma_tx);
    fm1_usb_ep_rxbuf(ECM_HOST_EP, ecm_dma_rx);
    /* H1 diag: do NOT call usb_hub_route_ep() — comment in
     * usb_hub_mount_msc_idx says writing SIE addresses >= 0x80
     * corrupts the CON1 state machine. MSC works through hubs
     * with FADDR alone. */
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

    /* H3 diag: readback SIE state after EP setup */
    usb_host_sie_wr(SIE_INDEX, ECM_HOST_EP);
    modd_log_int("ecm: sie faddr=", (int32_t)usb_host_sie_rd(SIE_FADDR));
    modd_log_int("ecm: sie txtype=", (int32_t)usb_host_sie_rd(SIE_TXTYPE));
    modd_log_int("ecm: sie rxtype=", (int32_t)usb_host_sie_rd(SIE_RXTYPE));
    modd_log_int("ecm: sie txcsr1=", (int32_t)usb_host_sie_rd(SIE_TXCSR1));
    modd_log_int("ecm: sie rxcsr1=", (int32_t)usb_host_sie_rd(SIE_RXCSR1));
    modd_log_int("ecm: ep2 txfa=", (int32_t)FM1_EP_TXFUNCADDR(ECM_HOST_EP));
    modd_log_int("ecm: ep2 rxfa=", (int32_t)FM1_EP_RXFUNCADDR(ECM_HOST_EP));

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
    ecm_rx_diag_n = 0;
    netif_set_up(&ecm_netif);
    netif_set_link_up(&ecm_netif);
    toast_status("USB ethernet");
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
    if (ecm_wanted)
        toast_error("ETH disconnected");
    else
        toast_clear();
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
        /* H3 diag: log SIE state on first 3 arm attempts */
        if (ecm_rx_diag_n < 3) {
            modd_log_int("ecm: rx arm faddr=", (int32_t)usb_host_sie_rd(SIE_FADDR));
            modd_log_int("ecm: rx arm rxtype=", (int32_t)usb_host_sie_rd(SIE_RXTYPE));
            modd_log_int("ecm: rx arm rxcsr1=", (int32_t)usb_host_sie_rd(SIE_RXCSR1));
            ecm_rx_diag_n++;
        }
        return;
    }
    csr = usb_host_sie_rd(SIE_RXCSR1);
    if (csr & 0x40u) {
        usb_host_sie_wr(SIE_RXCSR1, 0);
        ecm_rx_armed = 0;
        ecm_rx_len = 0;
        ecm_rx_stalls++;
        return;
    }
    if (csr & 0x0Cu) {
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        ecm_rx_errors++;
        return;
    }
    if (!(csr & 0x01u)) {
        if (!(csr & 0x20u)) {
            /* NAK: MUSB cleared ReqPkt without data. Re-arm. */
            usb_host_sie_wr(SIE_RXCSR1, 0x20u);
            if (ecm_rx_diag_n < 5) {
                modd_log_int("ecm: nak rearm csr=", (int32_t)csr);
                ecm_rx_diag_n++;
            }
        }
        return;
    }
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
    if (fm1_ms >= ecm_stats_next) {
        ecm_stats_next = fm1_ms + 10000u;
        if (ecm_rx_frames | ecm_tx_frames | ecm_rx_drops |
            ecm_rx_stalls | ecm_rx_errors) {
            modd_log_int("ecm: rx ", (int32_t)ecm_rx_frames);
            modd_log_int("ecm: tx ", (int32_t)ecm_tx_frames);
            if (ecm_rx_drops)
                modd_log_int("ecm: drops ", (int32_t)ecm_rx_drops);
            if (ecm_rx_stalls)
                modd_log_int("ecm: stalls ", (int32_t)ecm_rx_stalls);
            if (ecm_rx_errors)
                modd_log_int("ecm: errors ", (int32_t)ecm_rx_errors);
        }
    }
}

static void ecm_init(void)
{
    ecm_up = 0;
#ifdef CONFIG_USB_NET_AUTO
    ecm_wanted = 1;
#else
    ecm_wanted = sys_settings.usb_net_on;
#endif
}

static void ecm_set_wanted(uint8_t on)
{
#ifdef CONFIG_USB_NET_AUTO
    if (!on) return;
#endif
    ecm_wanted = on;
    modd_log(on ? "ecm: wanted" : "ecm: off");
}

#else /* !CONFIG_USB_NET_ECM */

static void ecm_init(void) {}
static void ecm_poll(void) {}
static int ecm_is_up(void) { return 0; }

#endif /* CONFIG_USB_NET_ECM */
