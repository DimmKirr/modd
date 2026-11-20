// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Who needs the network. Consumers (weather, LLM, RPC, a settings page) do not know which link
 * carries it: they call net_need() while they use the network and net_release() when done. This
 * file turns that one count into a need on a link, by policy:
 *
 *   consumers --net_need()/net_release()--> net_needs --> wifi_need()/wifi_release()  (WiFi radio)
 *                                                     \-> eth_need()/eth_release()    (USB host + CDC-ECM)
 *
 * Wired ethernet is preferred (net_link_prio): while an ethernet netif has an address, the WiFi
 * need is dropped, and it is taken again when that link goes away. A link only runs if its own
 * setting allows it (HW.WiFi, HW.USB NET) and a need is held, like the disk (udisk_need). Every
 * need pairs with one release. Without lwIP there is no link policy: only WiFi exists, and
 * net_need() is wifi_need(). */

static uint8_t net_needs;
static uint8_t eth_needs;
static uint8_t net_held_wifi;
static uint8_t net_held_eth;

#ifdef CONFIG_WIFI
static int  wifi_need(void);
static void wifi_release(void);
#endif
#ifdef CONFIG_USB_NET
/* also declared in modd_sys_menu.h (and usb_hub.c); a build without the menu sees only these */
static uint8_t usbnet_wanted, ecm_wanted, usbnet_allowed, ecm_allowed;
static uint8_t net_ethernet_up(void);            /* net.c: an ethernet netif has an address */
#endif

#ifdef CONFIG_USB_NET
static void eth_update(void)
{
    usbnet_wanted = (usbnet_allowed && eth_needs) ? 1 : 0;
    ecm_wanted    = (ecm_allowed && eth_needs) ? 1 : 0;
}
#else
static void eth_update(void) {}
#endif

static int eth_need(void)
{
    if (eth_needs < 255u) eth_needs++;
    eth_update();
    return 1;
}

static void eth_release(void)
{
    if (eth_needs) eth_needs--;
    eth_update();
}

/* Take or drop the link needs to match net_needs and the link state. Called on every change of
 * the count and when a link comes up or goes down (net.c). */
static void net_need_update(void)
{
    uint8_t want = net_needs ? 1 : 0;
    uint8_t want_eth = 0, want_wifi = 0;
#ifdef CONFIG_USB_NET
    want_eth = want;
#endif
#ifdef CONFIG_WIFI
    want_wifi = want;
#ifdef CONFIG_USB_NET
    if (want_eth && net_ethernet_up()) want_wifi = 0;
#endif
#endif
    if (want_eth && !net_held_eth) { eth_need(); net_held_eth = 1; }
    else if (!want_eth && net_held_eth) { eth_release(); net_held_eth = 0; }
#ifdef CONFIG_WIFI
    if (want_wifi && !net_held_wifi) { wifi_need(); net_held_wifi = 1; }
    else if (!want_wifi && net_held_wifi) { wifi_release(); net_held_wifi = 0; }
#endif
}

static void net_need(void)
{
    if (net_needs < 255u) net_needs++;
    net_need_update();
}

static void net_release(void)
{
    if (net_needs) net_needs--;
    net_need_update();
}

/* Needs that exist from boot: something that wants the network without a page being open.
 * Both are held for ever, on purpose (decided 2026-11-20): a build with the REST server wants to
 * be reachable, and a configured SSID means "stay on this network". The WIFI setting is the off
 * switch for both; with either one built in, the radio never powers down for lack of a consumer.
 * tests/net/net_need_test.c (d) pins this. */
static void net_boot_needs(void)
{
#ifdef CONFIG_RPC
    net_need();                                  /* the REST/WebSocket server on port 9222 */
#endif
#if defined(CONFIG_WIFI_AUTOJOIN) && defined(CONFIG_WIFI_SSID)
    if (CONFIG_WIFI_SSID[0] != '\0')
        net_need();                              /* join the configured SSID */
#endif
}
