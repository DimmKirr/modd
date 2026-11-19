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

/* Host EP1 carries the bulk pair and is shared with MSC: ECM uses it when
 * DISK is off, MSC when DISK is on.  Host EP2 carries the CDC notification
 * interrupt IN.  FADDR is global: it addresses every host endpoint, so both
 * pipes reach the adapter without the per-EP FUNCADDR registers (SIE
 * addresses >= 0x80, unreachable: CON1 corrupts the DONE bit and the
 * memory-mapped path at 0x16000 is dead on AC791N). */
#define ECM_HOST_EP         USB_BULK_HOST_EP
#define ECM_NOTIFY_HOST_EP  2
#define ECM_MTU             1500u
#define ECM_FRAME_MAX       (ECM_MTU + 14u)

#define CDC_SET_ETH_PACKET_FILTER  0x43
#define ECM_PACKET_FILTER  0x000Du   /* promiscuous + directed + broadcast */

#define ECM_NOTIFY_POLL_MS   8u
#define ECM_NOTIFY_ERR_MAX   8u      /* give up on the notify pipe after this many */
#define ECM_CARRIER_UNKNOWN  0xFFu   /* no NETWORK_CONNECTION seen yet */

static struct netif ecm_netif;
static uint8_t ecm_netif_added;      /* netif_add() done, kept across detach */
static uint8_t ecm_up;               /* attached: cleared by ecm_detach() */
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

static uint8_t ecm_dma_notify[68] __attribute__((aligned(4), section(".pool_net")));
static uint8_t ecm_notify_on;        /* notify pipe is being polled */
static uint8_t ecm_notify_armed;
static cdc_notify_t ecm_notify;      /* the notification being received */
static uint8_t ecm_carrier = ECM_CARRIER_UNKNOWN;
static uint32_t ecm_notify_n, ecm_notify_errs, ecm_notify_next;
static uint32_t ecm_speed_kbps;

/* Stall diagnostics, reported with the 10 s stats when out of the ordinary */
static uint32_t ecm_poll_last_ms, ecm_gap_max_ms;   /* longest pause between polls */
static uint32_t ecm_tx_wait_max_ms, ecm_tx_fails;   /* slowest and failed TX frames */
static uint32_t ecm_rx_last_ms;                     /* when the last frame came in */

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
    /* an unplugged adapter would hold each frame for the transfer timeout */
    if (otg.mode != OTG_HOST || uhost.state == UHOST_DISCONNECTED) return ERR_IF;
    ecm_select();
    {
        uint32_t t0 = fm1_ms, dt;
        int r = usb_bulk_out(ECM_HOST_EP, ecm_dma_tx, usb_ecm_cand.max_packet,
                             usb_ecm_cand.ep_out, ecm_tx_frame, len);
        dt = fm1_ms - t0;
        if (dt > ecm_tx_wait_max_ms) ecm_tx_wait_max_ms = dt;
        if (r < 0) {
            ecm_tx_fails++;
            return ERR_IF;
        }
    }
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

/* ---- CDC notifications: carrier and speed from the interrupt IN ---- */

static void ecm_set_carrier(uint8_t on)
{
    if (on == ecm_carrier) return;
    ecm_carrier = on;
    modd_log(on ? "ecm: carrier on" : "ecm: carrier off");
    if (on) {
        /* attach raises the link before any notification arrives, so the
         * first carrier-on finds it up: restart DHCP by hand in that case */
        if (netif_is_link_up(&ecm_netif))
            dhcp_network_changed_link_up(&ecm_netif);
        else
            netif_set_link_up(&ecm_netif);
        if (ecm_netif.ip_addr.addr)
            net_elect_default();
    } else {
        netif_set_link_down(&ecm_netif);
        net_on_down(&ecm_netif);
    }
}

static void ecm_notify_dispatch(void)
{
    if (!cdc_notify_valid(&ecm_notify)) return;
    ecm_notify_n++;
    if (cdc_notify_code(&ecm_notify) == CDC_NOTIFY_NETWORK_CONNECTION) {
        ecm_set_carrier(cdc_notify_value(&ecm_notify) ? 1 : 0);
    } else if (cdc_notify_code(&ecm_notify) == CDC_NOTIFY_SPEED_CHANGE) {
        uint32_t kbps = cdc_notify_speed_down(&ecm_notify) / 1000u;
        if (kbps && kbps != ecm_speed_kbps) {
            ecm_speed_kbps = kbps;
            modd_log_int("ecm: speed kbps=", (int32_t)kbps);
        }
    }
}

static void ecm_notify_setup(void)
{
    uint32_t interval = usb_ecm_cand.notify_interval;
    ecm_notify_on = 0;
    ecm_notify_armed = 0;
    cdc_notify_reset(&ecm_notify);
    ecm_notify_n = 0;
    ecm_notify_errs = 0;
    ecm_notify_next = 0;
    ecm_speed_kbps = 0;
    ecm_carrier = ECM_CARRIER_UNKNOWN;

    if (!usb_ecm_cand.notify_ep) {
        modd_log("ecm: no notify endpoint");
        return;
    }
    /* A mounted disk moves FADDR to its own address for every transfer.
     * A request left pending on host EP2 would then send IN tokens for
     * notify_ep to the disk and could take its bulk data. */
    if (usb_hub_mounted != 0xFF) {
        modd_log("ecm: disk mounted, notify off");
        return;
    }
    if (!interval) interval = 1;

    fm1_usb_ep_rxbuf(ECM_NOTIFY_HOST_EP, ecm_dma_notify);
    usb_host_sie_wr(SIE_INDEX, ECM_NOTIFY_HOST_EP);
    usb_host_sie_wr(SIE_RXMAXP, 0xFFu);
    usb_host_sie_wr(SIE_RXCSR1, 0);
    usb_host_sie_wr(SIE_RXCSR1, 0x90u);
    usb_host_sie_wr(SIE_RXCSR2, 0);
    usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x30u | usb_ecm_cand.notify_ep));
    usb_host_sie_wr(SIE_RXINTERVAL, interval);
    fm1_usb_ep_enable(1u << ECM_NOTIFY_HOST_EP);
    ecm_notify_on = 1;

}

/* One ReqPkt outstanding on host EP2, never blocks. */
static void ecm_notify_poll(void)
{
    uint32_t csr, n;
    if (!ecm_notify_on || fm1_ms < ecm_notify_next) return;
    ecm_notify_next = fm1_ms + ECM_NOTIFY_POLL_MS;

    ecm_select();
    usb_host_sie_wr(SIE_INDEX, ECM_NOTIFY_HOST_EP);
    if (!ecm_notify_armed) {
        fm1_usb_ep_rxbuf(ECM_NOTIFY_HOST_EP, ecm_dma_notify);
        usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x30u | usb_ecm_cand.notify_ep));
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        ecm_notify_armed = 1;
        return;
    }
    csr = usb_host_sie_rd(SIE_RXCSR1);
    if (csr & 0x4Cu) {
        usb_host_sie_wr(SIE_RXCSR1, 0);
        ecm_notify_armed = 0;
        cdc_notify_reset(&ecm_notify);
        if (ecm_notify_errs < 3)
            modd_log_int("ecm: notify err csr=", (int32_t)csr);
        if (++ecm_notify_errs >= ECM_NOTIFY_ERR_MAX) {
            ecm_notify_on = 0;
            modd_log("ecm: notify off (errors)");
        }
        return;
    }
    if (!(csr & 0x01u)) {
        if (!(csr & 0x20u))
            usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        return;
    }
    n = usb_host_sie_rd(SIE_RXCOUNT1) | (usb_host_sie_rd(SIE_RXCOUNT2) << 8);
    if (n > 64u) n = 64u;
    fm1_usb_rx_sync();
    if (cdc_notify_feed(&ecm_notify, ecm_dma_notify, n, usb_ecm_cand.notify_maxp)) {
        ecm_notify_dispatch();
        cdc_notify_reset(&ecm_notify);
    }
    usb_host_sie_wr(SIE_RXCSR1, 0x10u);
    ecm_notify_armed = 0;
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
    usb_settle_ms(100);

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
    /* No usb_hub_route_ep(): SIE addresses >= 0x80 corrupt the CON1
     * state machine, and FADDR alone reaches devices behind the hub. */
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
    fm1_usb_ep_enable(1u << ECM_HOST_EP);

    ecm_notify_setup();

    net_init();
    if (!ecm_netif_added) {
        ip4_addr_t z;
        ip4_addr_set_zero(&z);
        netif_add(&ecm_netif, &z, &z, &z, 0, ecm_netif_init, ethernet_input);
        netif_set_status_callback(&ecm_netif, ecm_status_cb);
        ecm_netif_added = 1;
    } else {
        memcpy(ecm_netif.hwaddr, ecm_mac, 6);
    }
    ecm_up = 1;
    ecm_rx_armed = 0;
    ecm_rx_len = 0;
    ecm_poll_last_ms = 0;
    ecm_gap_max_ms = 0;
    ecm_tx_wait_max_ms = 0;
    ecm_tx_fails = 0;
    ecm_rx_last_ms = fm1_ms;
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
    /* dhcp_stop() sends a DHCPRELEASE when bound: it goes out if the
     * adapter is still there, ecm_linkoutput() drops it if not */
    dhcp_stop(&ecm_netif);
    netif_set_link_down(&ecm_netif);
    netif_set_down(&ecm_netif);
    ecm_up = 0;
    net_on_down(&ecm_netif);
    ecm_rx_armed = 0;
    ecm_notify_on = 0;
    ecm_notify_armed = 0;
    ecm_carrier = ECM_CARRIER_UNKNOWN;
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
    ecm_rx_last_ms = fm1_ms;
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
        ecm_rx_stalls++;
        return;
    }
    if (csr & 0x0Cu) {
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        ecm_rx_errors++;
        return;
    }
    if (!(csr & 0x01u)) {
        /* ReqPkt is normally still set: a NAK leaves it there.  It only
         * drops without data when the device goes away. */
        if (!(csr & 0x20u))
            usb_host_sie_wr(SIE_RXCSR1, 0x20u);
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
    if (n == usb_ecm_cand.max_packet) {
        /* More packets coming for this frame — re-arm immediately so
         * the IN is pending while we wait for the next piece. */
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        ecm_rx_armed = 1;
        return;
    }
    /* Frame complete.  Re-arm the IN before delivering to lwIP so the
     * next frame's transfer is already in flight during IP processing
     * (and during any synchronous TCP output the callback triggers). */
    usb_host_sie_wr(SIE_RXCSR1, 0x20u);
    ecm_rx_armed = 1;
    ecm_rx_deliver();
}

static void ecm_poll(void)
{
    if (!ecm_up || otg.mode != OTG_HOST) return;
    if (uhost.state == UHOST_DISCONNECTED) {
        ecm_detach();   /* not 5 s later, when OTG gives the port back */
        return;
    }
    if (ecm_poll_last_ms && fm1_ms - ecm_poll_last_ms > ecm_gap_max_ms)
        ecm_gap_max_ms = fm1_ms - ecm_poll_last_ms;
    ecm_poll_last_ms = fm1_ms ? fm1_ms : 1u;
    ecm_rx_poll();
    ecm_notify_poll();
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
            if (ecm_notify_on || ecm_notify_n)
                modd_log_int("ecm: notifications ", (int32_t)ecm_notify_n);
            if (ecm_tx_fails)
                modd_log_int("ecm: tx fails ", (int32_t)ecm_tx_fails);
            if (ecm_tx_wait_max_ms >= 20u)
                modd_log_int("ecm: tx wait max ms=", (int32_t)ecm_tx_wait_max_ms);
            if (ecm_gap_max_ms >= 100u)
                modd_log_int("ecm: loop gap max ms=", (int32_t)ecm_gap_max_ms);
            if (ecm_rx_frames && fm1_ms - ecm_rx_last_ms >= 5000u)
                modd_log_int("ecm: rx idle ms=", (int32_t)(fm1_ms - ecm_rx_last_ms));
            ecm_tx_wait_max_ms = 0;
            ecm_gap_max_ms = 0;
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
