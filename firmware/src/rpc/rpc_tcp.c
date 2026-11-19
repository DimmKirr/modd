// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Thin lwIP TCP server for the RPC WebSocket transport.
 * Single-connection: one client at a time, polled from the main loop. */

#include "lwip/tcp.h"

static struct tcp_pcb *rpc_listen_pcb;
static struct tcp_pcb *rpc_conn_pcb;

static uint8_t  rpc_rx_ring[4096];
static uint16_t rpc_rx_head;
static uint16_t rpc_rx_tail;

static void (*rpc_on_disconnect)(void);

static err_t rpc_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb,
                             struct pbuf *p, err_t err)
{
    (void)arg;
    if (!p || err != ERR_OK) {
        if (tpcb == rpc_conn_pcb) {
            rpc_conn_pcb = 0;
            rpc_rx_head = rpc_rx_tail = 0;
            tcp_close(tpcb);
            if (rpc_on_disconnect) rpc_on_disconnect();
        }
        return ERR_OK;
    }
    struct pbuf *q;
    for (q = p; q; q = q->next) {
        uint16_t i;
        const uint8_t *d = (const uint8_t *)q->payload;
        for (i = 0; i < q->len; i++) {
            uint16_t next = (rpc_rx_head + 1) & (sizeof(rpc_rx_ring) - 1);
            if (next == rpc_rx_tail) break;
            rpc_rx_ring[rpc_rx_head] = d[i];
            rpc_rx_head = next;
        }
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void rpc_tcp_err_cb(void *arg, err_t err)
{
    (void)arg; (void)err;
    rpc_conn_pcb = 0;
    rpc_rx_head = rpc_rx_tail = 0;
    if (rpc_on_disconnect) rpc_on_disconnect();
}

static err_t rpc_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    if (rpc_conn_pcb) {
        modd_log("rpc: accept reject (busy)");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    modd_log("rpc: accept ok");
    rpc_conn_pcb = newpcb;
    rpc_rx_head = rpc_rx_tail = 0;
    tcp_arg(newpcb, 0);
    tcp_recv(newpcb, rpc_tcp_recv_cb);
    tcp_err(newpcb, rpc_tcp_err_cb);
    tcp_nagle_disable(newpcb);
    return ERR_OK;
}

static void rpc_tcp_listen(uint16_t port)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return;
    tcp_bind(pcb, IP_ADDR_ANY, port);
    rpc_listen_pcb = tcp_listen(pcb);
    if (!rpc_listen_pcb) { tcp_close(pcb); return; }
    tcp_accept(rpc_listen_pcb, rpc_tcp_accept_cb);
}

static uint32_t rpc_tcp_read(uint8_t *buf, uint32_t max)
{
    uint32_t n = 0;
    while (rpc_rx_tail != rpc_rx_head && n < max) {
        buf[n++] = rpc_rx_ring[rpc_rx_tail];
        rpc_rx_tail = (rpc_rx_tail + 1) & (sizeof(rpc_rx_ring) - 1);
    }
    return n;
}

static void ecm_poll(void);
static void usbnet_poll(void);
static void net_poll(void);

static uint32_t rpc_drain_t0;
static uint32_t rpc_drain_ms0;

static void rpc_tcp_drain(void)
{
    /* Advance fm1_ms from the hardware timer so lwIP timers (window
       probes, retransmits) keep running while the main loop is blocked
       inside this send-drain loop. */
    if (rpc_drain_t0) {
        uint32_t elapsed = (fm1_ticks() - rpc_drain_t0) / (FM1_TICKS_PER_US * 1000u);
        fm1_ms = rpc_drain_ms0 + elapsed;
    }
    ecm_poll();
    usbnet_poll();
    net_poll();
}

/* 10 s wall-clock timeout via the hardware timer (fm1_ticks advances even
   when the main loop is blocked inside this drain loop). */
#define RPC_SEND_TIMEOUT_TICKS (10u * 1000u * 1000u * FM1_TICKS_PER_US)

static int rpc_tcp_send(const uint8_t *data, uint32_t len)
{
    if (!rpc_conn_pcb) return -1;
    rpc_drain_t0 = fm1_ticks();
    rpc_drain_ms0 = fm1_ms;
    uint32_t sent = 0;
    uint32_t t0 = rpc_drain_t0;
    while (sent < len) {
        if (!rpc_conn_pcb) {
            modd_log_int("rpc: send disconnected at ", (int32_t)sent);
            rpc_drain_t0 = 0; return -1;
        }
        if ((uint32_t)(fm1_ticks() - t0) > RPC_SEND_TIMEOUT_TICKS) {
            modd_log_int("rpc: send timeout at ", (int32_t)sent);
            modd_log_int("rpc: sndbuf=", (int32_t)tcp_sndbuf(rpc_conn_pcb));
            rpc_drain_t0 = 0; return -1;
        }
        uint16_t snd = tcp_sndbuf(rpc_conn_pcb);
        if (snd == 0) {
            tcp_output(rpc_conn_pcb);
            rpc_tcp_drain();
            fm1_delay_ms(1);
#ifdef CONFIG_SCHED
            yield();
#endif
            continue;
        }
        t0 = fm1_ticks();
        uint16_t chunk = (uint16_t)(len - sent);
        if (chunk > snd) chunk = snd;
        err_t e = tcp_write(rpc_conn_pcb, data + sent, chunk, TCP_WRITE_FLAG_COPY);
        if (e == ERR_MEM) {
            tcp_output(rpc_conn_pcb);
            rpc_tcp_drain();
            fm1_delay_ms(1);
#ifdef CONFIG_SCHED
            yield();
#endif
            continue;
        }
        if (e != ERR_OK) {
            modd_log_int("rpc: tcp_write err at ", (int32_t)sent);
            rpc_drain_t0 = 0; return -1;
        }
        sent += chunk;
        tcp_output(rpc_conn_pcb);
    }
    rpc_drain_t0 = 0;
    return (int)len;
}

static void rpc_tcp_close(void)
{
    if (!rpc_conn_pcb) return;
    struct tcp_pcb *pcb = rpc_conn_pcb;
    rpc_conn_pcb = 0;
    rpc_rx_head = rpc_rx_tail = 0;
    tcp_arg(pcb, 0);
    tcp_recv(pcb, 0);
    tcp_err(pcb, 0);
    if (tcp_close(pcb) != ERR_OK)
        tcp_abort(pcb);
    if (rpc_on_disconnect) rpc_on_disconnect();
}
