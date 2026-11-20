// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test of the WiFi power state machine: the real firmware/src/net/net_need.c,
 * firmware/src/wifi/wifi_need.c and firmware/src/wifi/wifi_api.c, with every lower layer a stub
 * that counts its calls. wifi_api.c needs only those lower-layer names and a few register macros
 * (defined as 0 here), so the whole file is included unchanged.
 *
 * Built twice: plain, and with -DTEST_DOCK (CONFIG_DOCK, a dock_active stub): then wifi_poll()
 * must not bring the radio up while a module runs. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_WIFI 1
#define CONFIG_WIFI_WPA 1
#ifdef TEST_DOCK
#define CONFIG_DOCK 1
static const void *volatile dock_active;
#endif

#include "../adapters/modd_wifi.h"

/* ---- stubs: settings, log, toasts ---- */
static struct { uint8_t wifi_on; } sys_settings;
static char logbuf[64][64];
static int nlog;
static void modd_log(const char *s)
{
    if (nlog < 64) { strncpy(logbuf[nlog], s, 63); logbuf[nlog][63] = 0; nlog++; }
}
static void modd_log_int(const char *s, int32_t v) { (void)s; (void)v; }
static void toast_status(const char *s) { (void)s; }
static void toast_error(const char *s) { (void)s; }
static int count_log(const char *s)
{
    int n = 0;
    for (int i = 0; i < nlog; i++) if (!strcmp(logbuf[i], s)) n++;
    return n;
}

/* ---- stubs: the layers below wifi_api.c, counted ---- */
enum { C_RTOS_INIT, C_RTOS_DEINIT, C_RF_INIT, C_RF_ARM, C_RF_DEINIT, C_SDIO_INIT, C_SDIO_QUIESCE,
       C_SDIO_DEINIT, C_SDIO_POLL, C_MAC_INIT, C_MAC_DEINIT, C_MAC_TIGHTEN, C_STA_INIT, C_STA_DEINIT,
       C_STA_POLL, C_STA_DISCONNECT, C_STA_SCAN, C_STA_CONNECT, C_STA_FIND, C_LWIP_INIT, C_LWIP_DEINIT,
       C_LWIP_MAC, C_LWIP_STOP_DHCP, C_LWIP_START_DHCP, C_DEFER_POLL, C_TIMER_POLL, C_WPA_INIT,
       C_WPA_RESET, C_IND, C_LWIP_INPUT, C_N };
static int calls[C_N];
static int calls_total(void) { int n = 0; for (int i = 0; i < C_N; i++) n += calls[i]; return n; }

#define WIFI_MAC_SYS_CTRL 0
#define WDMA_CTL 0
#define WDMA_CFG0 0
#define WIFI_MAC_ADDR_DW0 0
#define WIFI_MAC_ADDR_DW1 0

static uint8_t wifi_mac_addr[6];
static struct {
    wifi_state_t state;
    uint8_t our_mac[6];
    struct { uint8_t bssid[6]; } results[4];
} wifi_sta;
static int ap_known;                            /* wifi_sta_find_ap() finds the AP */
static uint32_t now_ms;

static void wifi_rtos_init(void) { calls[C_RTOS_INIT]++; }
static void wifi_rtos_deinit(void) { calls[C_RTOS_DEINIT]++; }
static void wifi_ind_arm(void) { calls[C_IND]++; }
static void wifi_ind_log(const char *s, int n) { (void)s; (void)n; calls[C_IND]++; }
static void wifi_ind_done(void) { calls[C_IND]++; }
static void wifi_rf_init(void) { calls[C_RF_INIT]++; }
static void wifi_rf_arm(void) { calls[C_RF_ARM]++; }
static void wifi_rf_deinit(void) { calls[C_RF_DEINIT]++; }
static void wifi_sdio_init(void) { calls[C_SDIO_INIT]++; }
static void wifi_sdio_quiesce(void) { calls[C_SDIO_QUIESCE]++; }
static void wifi_sdio_deinit(void) { calls[C_SDIO_DEINIT]++; }
static void wifi_sdio_poll_irq(void) { calls[C_SDIO_POLL]++; }
static void wifi_mac_init(void) { calls[C_MAC_INIT]++; }
static void wifi_mac_deinit(void) { calls[C_MAC_DEINIT]++; }
static void wifi_mac_tighten_filter(void) { calls[C_MAC_TIGHTEN]++; }
static void wifi_sta_init(const uint8_t *mac) { (void)mac; calls[C_STA_INIT]++; wifi_sta.state = WIFI_STATE_IDLE; }
static void wifi_sta_deinit(void) { calls[C_STA_DEINIT]++; wifi_sta.state = WIFI_STATE_OFF; }
static void wifi_sta_poll(void) { calls[C_STA_POLL]++; }
static void wifi_sta_do_disconnect(void) { calls[C_STA_DISCONNECT]++; wifi_sta.state = WIFI_STATE_IDLE; }
static int wifi_sta_start_scan(void (*cb)(const char *, int, int)) { (void)cb; calls[C_STA_SCAN]++; return 0; }
static int wifi_sta_start_connect(const char *s, const char *p)
{ (void)s; (void)p; calls[C_STA_CONNECT]++; wifi_sta.state = WIFI_STATE_AUTH; return 0; }
static int wifi_sta_find_ap(const char *s) { (void)s; calls[C_STA_FIND]++; return ap_known ? 0 : -1; }
static uint32_t wifi_sta_now_ms(void) { return now_ms; }
static void wifi_lwip_set_mac(const uint8_t *m) { (void)m; calls[C_LWIP_MAC]++; }
static void wifi_lwip_init(void) { calls[C_LWIP_INIT]++; }
static void wifi_lwip_deinit(void) { calls[C_LWIP_DEINIT]++; }
static void wifi_lwip_stop_dhcp(void) { calls[C_LWIP_STOP_DHCP]++; }
static void wifi_lwip_start_dhcp(void) { calls[C_LWIP_START_DHCP]++; }
static void wifi_lwip_input(const uint8_t *d, uint32_t n) { (void)d; (void)n; calls[C_LWIP_INPUT]++; }
static uint32_t wifi_lwip_get_ip(void) { return 0; }
static uint32_t wifi_lwip_get_gw(void) { return 0; }
static uint32_t wifi_lwip_get_dns(void) { return 0; }
static void wifi_defer_poll(void) { calls[C_DEFER_POLL]++; }
static void wifi_timer_poll(void) { calls[C_TIMER_POLL]++; }
static void wifi_wpa_init(const uint8_t *p, uint32_t pl, const uint8_t *s, uint32_t sl,
                          const uint8_t *m, const uint8_t *b)
{ (void)p; (void)pl; (void)s; (void)sl; (void)m; (void)b; calls[C_WPA_INIT]++; }
static void wifi_wpa_reset(void) { calls[C_WPA_RESET]++; }

#include "net/net_need.c"
#include "wifi/wifi_need.c"
#include "wifi/wifi_api.c"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void reset(void)
{
    memset(calls, 0, sizeof calls);
    nlog = 0;
    net_needs = net_held_wifi = 0;
    wifi_needs = wifi_active = wifi_inited = 0;
    wifi_join_need = 0;
    wifi_cfg_ssid = wifi_cfg_pass = 0;
    memset(&wifi_sta, 0, sizeof wifi_sta);
    ap_known = 0;
    sys_settings.wifi_on = 1;
#ifdef TEST_DOCK
    dock_active = 0;
    wifi_dock_logged = 0;
#endif
}

static void polls(int n) { while (n--) wifi_poll(); }
static int inits(void) { return calls[C_RF_INIT]; }
static int deinits(void) { return calls[C_RF_DEINIT]; }

int main(void)
{
    /* off and never needed: wifi_poll() calls nothing below it */
    reset();
    polls(10);
    CHECK(calls_total() == 0 && nlog == 0 && !wifi_inited);

    /* need + setting on: the next poll runs init exactly once; further polls poll */
    reset();
    net_need();
    CHECK(wifi_active && !wifi_inited && calls_total() == 0);   /* need_update only records */
    wifi_poll();
    CHECK(wifi_inited && inits() == 1 && calls[C_SDIO_INIT] == 1 && calls[C_MAC_INIT] == 1 &&
          calls[C_STA_INIT] == 1 && calls[C_LWIP_INIT] == 1);
    CHECK(count_log("wifi: init start") == 1 && count_log("wifi: init done") == 1);
    CHECK(calls[C_STA_POLL] == 0);                              /* the init poll returns */
    polls(5);
    CHECK(inits() == 1 && calls[C_STA_POLL] == 5 && calls[C_SDIO_POLL] == 5 &&
          calls[C_DEFER_POLL] == 5 && calls[C_TIMER_POLL] == 5);

    /* boot: need held + setting on -> wifi_boot_init() inits once, polls and a second boot call do not */
    reset();
    net_need();
    wifi_boot_init();
    CHECK(wifi_inited && inits() == 1 && count_log("wifi: init done") == 1);
    CHECK(calls[C_STA_POLL] == 0);                              /* the boot call does not poll */
    wifi_boot_init();
    wifi_poll();
    CHECK(inits() == 1 && calls[C_STA_POLL] == 1);
    /* boot: setting off (need held) -> nothing; switched on later, wifi_poll() inits */
    reset();
    sys_settings.wifi_on = 0;
    net_need();
    wifi_boot_init();
    CHECK(!wifi_inited && calls_total() == 0);
    sys_settings.wifi_on = 1;
    wifi_set_enabled(1);
    wifi_poll();
    CHECK(wifi_inited && inits() == 1);
    /* boot: no need -> nothing; the first need later inits from wifi_poll() */
    reset();
    wifi_boot_init();
    CHECK(!wifi_inited && calls_total() == 0);
    net_need();
    CHECK(inits() == 0);
    wifi_poll();
    CHECK(wifi_inited && inits() == 1);

    /* setting off while joined: disconnect, deinit once, later polls call nothing; on again: second init */
    wifi_sta.state = WIFI_STATE_CONNECTED;
    sys_settings.wifi_on = 0;
    wifi_set_enabled(0);
    CHECK(!wifi_active && wifi_inited && deinits() == 0);       /* torn down by the next poll only */
    wifi_poll();
    CHECK(!wifi_inited && deinits() == 1 && calls[C_STA_DISCONNECT] == 1 && calls[C_LWIP_STOP_DHCP] == 1);
    CHECK(calls[C_SDIO_QUIESCE] == 1 && calls[C_RTOS_DEINIT] == 1 && calls[C_LWIP_DEINIT] == 1 &&
          calls[C_WPA_RESET] == 1 && calls[C_STA_DEINIT] == 1 && calls[C_MAC_DEINIT] == 1 &&
          calls[C_SDIO_DEINIT] == 1);
    CHECK(count_log("wifi: deinit start") == 1 && count_log("wifi: deinit done") == 1);
    {
        int before = calls_total();
        polls(10);
        CHECK(calls_total() == before && deinits() == 1);       /* down: nothing runs */
    }
    sys_settings.wifi_on = 1;
    wifi_set_enabled(1);
    wifi_poll();
    CHECK(wifi_inited && inits() == 2 && count_log("wifi: init done") == 2);

    /* the last need released while up: the same tear-down */
    net_release();
    CHECK(!wifi_active);
    wifi_poll();
    CHECK(!wifi_inited && deinits() == 2);
    polls(3);
    CHECK(deinits() == 2 && inits() == 2);

    /* scan / connect / disconnect while down: -1 and no lower layer */
    reset();
    ap_known = 1;
    {
        int r_scan = wifi_scan(0), r_conn = wifi_connect("Home", "pw");
        CHECK(r_scan == -1 && r_conn == -1);
        CHECK(calls[C_STA_SCAN] == 0 && calls[C_STA_CONNECT] == 0 && calls[C_STA_FIND] == 0 &&
              calls[C_WPA_INIT] == 0);
        CHECK(net_needs == 0 && wifi_needs == 0 && !wifi_join_need);   /* a refused connect takes no need */
        wifi_disconnect();
        CHECK(calls[C_STA_DISCONNECT] == 0 && calls[C_LWIP_STOP_DHCP] == 0);
        CHECK(calls_total() == 0);
    }

    /* manual join (felucca: no RPC, empty SSID, no boot need): the page takes a need, joins, closes */
    reset();
    net_need();                                  /* the page's own need */
    wifi_poll();                                 /* radio up */
    CHECK(wifi_inited && wifi_needs == 1);
    polls(2);
    ap_known = 1;
    CHECK(wifi_connect("Home", "pw") == 0);
    CHECK(wifi_join_need && net_needs == 2 && calls[C_STA_CONNECT] == 1 && calls[C_WPA_INIT] == 1);
    CHECK(wifi_connect("Home", "pw") == -1);     /* joining: state AUTH, refused ... */
    CHECK(net_needs == 2);                       /* ... and no second need */
    wifi_sta.state = WIFI_STATE_IDLE;            /* a retry from idle: still one need */
    CHECK(wifi_connect("Home", "pw") == 0);
    CHECK(net_needs == 2 && wifi_join_need);
    net_release();                               /* the page closes */
    CHECK(net_needs == 1 && wifi_active);
    polls(3);
    CHECK(wifi_inited && deinits() == 0);        /* the join keeps the radio up */
    wifi_disconnect();                           /* leaving the network gives the need back */
    CHECK(!wifi_join_need && net_needs == 0 && !wifi_active && wifi_inited);
    wifi_poll();
    CHECK(!wifi_inited && deinits() == 1);
    wifi_disconnect();                           /* twice: no second release */
    CHECK(net_needs == 0 && wifi_needs == 0);

    /* connect to an AP that is not there: no need taken */
    reset();
    net_need();
    wifi_poll();
    ap_known = 0;
    CHECK(wifi_connect("Nowhere", "pw") == -1 && net_needs == 1 && !wifi_join_need);

#ifdef TEST_DOCK
    /* a module is running: the radio stays down, one log line, and comes up by itself afterwards */
    reset();
    dock_active = (const void *)&fails;
    net_need();
    polls(10);
    CHECK(wifi_active && !wifi_inited && inits() == 0 && calls_total() == 0);
    CHECK(count_log("wifi: a module is running, not starting") == 1);
    CHECK(count_log("wifi: init start") == 0);
    dock_active = 0;
    wifi_poll();
    CHECK(wifi_inited && inits() == 1 && count_log("wifi: init done") == 1);
    CHECK(count_log("wifi: a module is running, not starting") == 1);
    /* off and down while the module runs: nothing, and no log */
    reset();
    dock_active = (const void *)&fails;
    polls(5);
    CHECK(nlog == 0 && calls_total() == 0);
    /* the wait ends without the radio (need released): the line comes again at the next wait */
    net_need(); polls(3); net_release(); polls(2); net_need(); polls(3);
    CHECK(count_log("wifi: a module is running, not starting") == 2);
#else
    /* without CONFIG_DOCK nothing in wifi_poll() looks at a module */
    CHECK(count_log("wifi: a module is running, not starting") == 0);
#endif

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
