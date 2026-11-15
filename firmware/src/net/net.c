// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Network module: the lwIP stack shared by every link (WiFi, USB).
 * Links add their own struct netif; this file owns init, the timer
 * tick and the "an interface got an address" notification. */

#ifdef CONFIG_LWIP

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/dns.h"

/* lwIP timer tick: must be extern, called by the lwIP core */
uint32_t sys_now(void) { return fm1_ms; }

static uint8_t net_inited;

#ifdef CONFIG_LWIP
static void weather_fetch_start(void);
#endif

static void net_init(void)
{
    if (net_inited) return;
    lwip_init();
    net_inited = 1;
    modd_log("net: lwip init");
}

/* Link priority: wired ethernet > WiFi > iPhone tethering. */
static uint8_t net_link_prio(struct netif *nif)
{
    if (nif->name[0] == 'e' && nif->name[1] == 'n') return 3;
    if (nif->name[0] == 'w' && nif->name[1] == 'f') return 2;
    if (nif->name[0] == 'u' && nif->name[1] == 's') return 1;
    return 0;
}

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
    modd_log_ip("net: ip ", nif->ip_addr.addr);
    modd_log_ip("net: mask ", nif->netmask.addr);
    modd_log_ip("net: gw ", nif->gw.addr);
    {
        const ip_addr_t *d = dns_getserver(0);
        if (d && ip_2_ip4(d)->addr)
            modd_log_ip("net: dns ", ip_2_ip4(d)->addr);
    }
    net_elect_default();
#ifdef CONFIG_LWIP
    weather_fetch_start();
#endif
}

static void net_on_down(struct netif *nif)
{
    (void)nif;
    net_elect_default();
}

static uint32_t net_get_ip(void)
{
    return netif_default ? netif_ip4_addr(netif_default)->addr : 0;
}

static uint32_t net_get_gw(void)
{
    return netif_default ? netif_ip4_gw(netif_default)->addr : 0;
}

static uint32_t net_get_dns(void)
{
    const ip_addr_t *d = dns_getserver(0);
    return d ? ip_2_ip4(d)->addr : 0;
}

static void net_poll(void)
{
    sys_check_timeouts();
}

#else /* !CONFIG_LWIP */

static void net_init(void) {}
static void net_poll(void) {}
static uint32_t net_get_ip(void)  { return 0; }
static uint32_t net_get_gw(void)  { return 0; }
static uint32_t net_get_dns(void) { return 0; }

#endif /* CONFIG_LWIP */
