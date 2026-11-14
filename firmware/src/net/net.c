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

/* Called from a link's netif status callback once it has an IP address.
 * The default route follows the most recent link to come up. */
static void net_on_up(struct netif *nif)
{
    uint32_t ip = nif->ip_addr.addr;
    modd_log_hex("net: up ip=", (const uint8_t *)&ip, 4);
    modd_log_hex("net: gw=", (const uint8_t *)&nif->gw.addr, 4);
    netif_set_default(nif);
#ifdef CONFIG_LWIP
    weather_fetch_start();
#endif
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
