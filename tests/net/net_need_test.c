// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test of the network needs: firmware/src/net/net_need.c (net_need -> wifi_need / eth_need by
 * link policy) and firmware/src/wifi/wifi_need.c (the radio runs on setting + need). The radio
 * and the link state are stubs. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_WIFI 1
#define CONFIG_WIFI_IN_SHARED_POOL 1
#define CONFIG_USB_NET 1
#define CONFIG_WIFI_AUTOJOIN 1
#ifndef TEST_NO_RPC
#define CONFIG_RPC 1
#endif
static char test_ssid[8];                       /* CONFIG_WIFI_SSID may be "" */
#define CONFIG_WIFI_SSID test_ssid

static struct { uint8_t wifi_on; } sys_settings;
static void modd_log(const char *s) { (void)s; }
static void modd_log_int(const char *s, int32_t v) { (void)s; (void)v; }

static int powered_on, powered_off, wifi_inits;
static uint8_t wifi_inited;                     /* stands for the radio: here the stub brings it up at once;
                                                 * in the firmware wifi_poll() does, and tears it down again
                                                 * (tests/wifi/wifi_power_test.c) */
static uint8_t ethernet_is_up;
static uint8_t usbnet_wanted, ecm_wanted, usbnet_allowed, ecm_allowed;

static uint8_t net_ethernet_up(void) { return ethernet_is_up; }
static void wifi_power(uint8_t on)
{
    if (on) {
        powered_on++;
        if (!wifi_inited) { wifi_inits++; wifi_inited = 1; }   /* first activation */
    } else powered_off++;
}

#include "wifi/wifi_need.c"
#include "net/net_need.c"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void reset(void)
{
    wifi_needs = wifi_active = eth_needs = net_needs = 0;
    net_held_wifi = net_held_eth = 0;
    powered_on = powered_off = 0;
    wifi_inits = 0;
    wifi_inited = 0;
    ethernet_is_up = 0;
    usbnet_wanted = ecm_wanted = 0;
    usbnet_allowed = ecm_allowed = 0;
    sys_settings.wifi_on = 1;
}

int main(void)
{
    /* nothing needs the network: nothing runs */
    reset();
    CHECK(!wifi_is_active() && !usbnet_wanted);

    /* a need turns the radio on; the last release turns it off; counted */
    reset();
    net_need();
    CHECK(wifi_is_active() && powered_on == 1);
    net_need();
    CHECK(wifi_needs == 1 && powered_on == 1);          /* one need on the link, however many consumers */
    net_release();
    CHECK(wifi_is_active());
    net_release();
    CHECK(!wifi_is_active() && powered_off == 1);
    net_release();                                      /* an extra release does not go below 0 */
    CHECK(net_needs == 0 && wifi_needs == 0);

    /* the WIFI setting is a permission: off = counted, not running; on later = runs */
    reset();
    sys_settings.wifi_on = 0;
    net_need();
    CHECK(wifi_needs == 1 && !wifi_is_active() && powered_on == 0);
    sys_settings.wifi_on = 1;
    wifi_set_enabled(1);
    CHECK(wifi_is_active() && powered_on == 1);
    sys_settings.wifi_on = 0;
    wifi_set_enabled(0);
    CHECK(!wifi_is_active() && powered_off == 1);
    net_release();
    CHECK(wifi_needs == 0);

    /* the USB link is wanted only with its setting and a need */
    reset();
    usbnet_allowed = ecm_allowed = 1;
    CHECK(!usbnet_wanted);
    net_need();
    CHECK(usbnet_wanted && ecm_wanted && eth_needs == 1);
    net_release();
    CHECK(!usbnet_wanted && !ecm_wanted && eth_needs == 0);
    reset();
    net_need();                                         /* USB NET setting off: no host mode */
    CHECK(!usbnet_wanted && !ecm_wanted);
    net_release();

    /* ethernet up: WiFi is dropped; ethernet gone: WiFi is taken again */
    reset();
    usbnet_allowed = ecm_allowed = 1;
    net_need();
    CHECK(wifi_is_active());
    ethernet_is_up = 1;
    net_need_update();
    CHECK(!wifi_is_active() && eth_needs == 1);
    ethernet_is_up = 0;
    net_need_update();
    CHECK(wifi_is_active());
    net_release();
    CHECK(!wifi_is_active() && eth_needs == 0 && net_held_wifi == 0 && net_held_eth == 0);

    /* (a) the setting toggled off and on while a need is held: power off, then on; init once */
    reset();
    net_need();
    CHECK(wifi_is_active() && wifi_inits == 1);
    sys_settings.wifi_on = 0;
    wifi_set_enabled(0);
    CHECK(!wifi_is_active() && powered_off == 1 && wifi_needs == 1);
    sys_settings.wifi_on = 1;
    wifi_set_enabled(1);
    CHECK(wifi_is_active() && powered_on == 2 && wifi_inits == 1);
    net_release();
    CHECK(!wifi_is_active() && wifi_needs == 0);

    /* (b) ethernet up while the WiFi setting is off, then down: the WiFi need is counted but the
     * radio never starts; ethernet gone and setting still off: still no radio */
    reset();
    usbnet_allowed = ecm_allowed = 1;
    sys_settings.wifi_on = 0;
    net_need();
    CHECK(usbnet_wanted && !wifi_is_active() && wifi_inits == 0 && net_held_wifi == 1);
    ethernet_is_up = 1;
    net_need_update();
    CHECK(net_held_wifi == 0 && wifi_needs == 0 && !wifi_is_active());
    ethernet_is_up = 0;
    net_need_update();
    CHECK(net_held_wifi == 1 && wifi_needs == 1 && !wifi_is_active() && wifi_inits == 0);
    net_release();
    CHECK(wifi_needs == 0 && eth_needs == 0 && net_held_wifi == 0);

    /* (c) an ethernet flap with one consumer: WiFi need dropped, retaken, dropped */
    reset();
    usbnet_allowed = ecm_allowed = 1;
    net_need();
    CHECK(wifi_needs == 1);
    ethernet_is_up = 1;
    net_need_update();
    CHECK(wifi_needs == 0 && !wifi_is_active());
    ethernet_is_up = 0;
    net_need_update();
    CHECK(wifi_needs == 1 && wifi_is_active());
    ethernet_is_up = 1;
    net_need_update();
    CHECK(wifi_needs == 0 && !wifi_is_active() && wifi_inits == 1 && eth_needs == 1);
    net_release();
    CHECK(wifi_needs == 0 && eth_needs == 0 && net_needs == 0);

    /* (d) boot needs: RPC (if built) and auto-join only with a non-empty SSID */
    reset();
    test_ssid[0] = '\0';
    net_boot_needs();
#ifdef CONFIG_RPC
    CHECK(net_needs == 1);
#else
    CHECK(net_needs == 0);
#endif
    reset();
    strcpy(test_ssid, "Home");
    net_boot_needs();
#ifdef CONFIG_RPC
    CHECK(net_needs == 2);
#else
    CHECK(net_needs == 1);
#endif

    /* (e) unbalanced releases at count 0 do not underflow */
    reset();
    net_release();
    wifi_release();
    eth_release();
    CHECK(net_needs == 0 && wifi_needs == 0 && eth_needs == 0 && !wifi_is_active());
    net_need();
    CHECK(net_needs == 1 && wifi_needs == 1);

    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
