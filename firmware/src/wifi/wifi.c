// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md.
 * No vendor code, IR or disassembly.
 *
 * Public API implementing modd_wifi.h. Orchestrates:
 *   rtos.c       -- bare-metal RTOS shim (buffer pool, deferred work, IRQ lock)
 *   hw/rf.c      -- RF analog init, VCO scan, channel set, TX power
 *   hw/sdio.c    -- SDIO bus driver, DMA, IRQ
 *   mac/mac.c    -- 802.11 MAC frame handling
 *   sta.c        -- STA state machine
 *   mac/data.c   -- TX/RX data path
 *   mac/wpa.c    -- WPA2-PSK (4-way handshake, CCMP)
 *   lwip.c       -- lwIP netif, DHCP */

/* stored credentials for the connect sequence */
static const char *wifi_cfg_ssid;
static const char *wifi_cfg_pass;
/* next automatic scan (autoscan / autojoin): the first one 2 s after the task starts, and wifi_init()
 * sets it to "now", so a scan starts at once after the radio comes back */
static uint32_t wifi_scan_next_ms = 2000u;
#ifdef CONFIG_WIFI_IN_SHARED_POOL
static uint8_t wifi_join_need;      /* a join by hand took a net_need(); leaving the network gives it back */
#endif

static void wifi_disconnect(void);
#if defined(CONFIG_DOCK) && defined(CONFIG_WIFI_IN_SHARED_POOL)
static uint8_t wifi_dock_logged;    /* "a module is running" is logged once per wait, not per poll */
#endif

static void wifi_init(void)
{
    modd_log("wifi: init start");
    /* RTOS shim: buffer pool, deferred work queue */
    wifi_rtos_init();

    /* Running-indicator rows at every init boundary (iteration 12i). The
     * first sample is "rf", after the clock tree: the SoC block has only been
     * read on hardware from that point on. */
    wifi_ind_arm();

    /* RF bring-up: clocks, analog block, VCO scan, BBP, LUT (14-group
     * sequence from docs/hardware/wifi/SPECSHEET.md) */
    wifi_rf_init();
    wifi_ind_log("rf", 0);

    /* SDIO internal bus: clock/reset, enumeration, DMA */
    wifi_sdio_init();

    /* MAC: set initial channel, enable RX */
    wifi_mac_init();
    /* Re-arm the RF die after the last MAC/PHY reset [SPECSHEET §14 row 7,
     * §4.2.1 A17-A23]: the vendor's LUT load and auto control follow its
     * final PHY init with no reset afterwards; ours ran before the resets
     * in wifi_sdio_init(), so the tail runs once more here (12m). */
    wifi_rf_arm();
    wifi_ind_log("mac", 1);
    wifi_ind_done();

    /* STA state machine: start in IDLE */
    wifi_sta_init(wifi_mac_addr);

    /* lwIP: share MAC with netif, then init stack */
    wifi_lwip_set_mac(wifi_sta.our_mac);
    wifi_lwip_init();

    wifi_cfg_ssid = 0;
    wifi_cfg_pass = 0;
    wifi_scan_next_ms = wifi_sta_now_ms();

    /* diagnostic: confirm WiFi subsystem state after init */
    modd_log_int("wifi: init mac_sys=", (int32_t)WIFI_MAC_SYS_CTRL);
    modd_log_int("wifi: init wdma_ctl=", (int32_t)WDMA_CTL);
    modd_log_int("wifi: init wdma_cfg0=", (int32_t)WDMA_CFG0);
    modd_log_int("wifi: init mac0=", (int32_t)WIFI_MAC_ADDR_DW0);
    modd_log_int("wifi: init mac1=", (int32_t)WIFI_MAC_ADDR_DW1);
    modd_log_int("wifi: init sta_st=", (int32_t)wifi_sta.state);
    modd_log("wifi: init done");
}

#ifdef CONFIG_WIFI_IN_SHARED_POOL
/* Tear the radio down: the reverse of wifi_init(). Runs from wifi_poll() (the fixed context), after
 * wifi_disconnect() has left the network (the deauth needs the radio). Order: interrupts first, so
 * no ISR runs against a half-torn-down chip; then software state; then MAC, DMA, RF in the reverse
 * order of wifi_init(). The netif stays added to lwIP (wifi_lwip_init() does not add it twice).
 * Steps that the spec sheet does not describe are marked in the callee. */
static void wifi_deinit(void)
{
    modd_log("wifi: deinit start");

    wifi_sdio_quiesce();            /* ISR source off, driver refuses RX/TX/sweeps */
    wifi_rtos_deinit();             /* deferred work and timers cancelled */
    wifi_lwip_deinit();             /* DHCP stopped, netif down (kept added) */
#ifdef CONFIG_WIFI_WPA
    wifi_wpa_reset();               /* key material wiped */
#endif
    wifi_sta_deinit();              /* station state back to WIFI_STATE_OFF, scan results gone */
    wifi_mac_deinit();              /* MAC RX/TX off, BSS table cleared */
    wifi_sdio_deinit();             /* DMA stopped, WDMA clock gate off */
    wifi_rf_deinit();               /* RF front end off, clock selects back */

    wifi_cfg_ssid = 0;
    wifi_cfg_pass = 0;
    modd_log("wifi: deinit done");
}

/* wifi_need.c calls this when wifi_active changes. It does not touch the hardware: bring-up and
 * tear-down run in wifi_poll(), one fixed context (the WiFi task, or the main loop without the
 * scheduler), whichever task took the need or toggled the setting. */
static void wifi_power(uint8_t on)
{
    (void)on;                       /* wifi_need_update() has logged the change */
}
#endif /* CONFIG_WIFI_IN_SHARED_POOL */

static int wifi_scan(void (*cb)(const char *ssid, int rssi, int channel))
{
    if (wifi_sta.state == WIFI_STATE_OFF)
        return -1;
    return wifi_sta_start_scan(cb);
}

static int wifi_connect(const char *ssid, const char *password)
{
    int idx;

    if (!ssid)
        return -1;
    modd_log("wifi: connect");
    if (wifi_sta.state != WIFI_STATE_IDLE &&
        wifi_sta.state != WIFI_STATE_SCANNING)
        return -1;

    /* look up the AP in scan results so the PMK can be derived before
     * authentication starts, instead of racing the EAPOL handshake */
    idx = wifi_sta_find_ap(ssid);
    if (idx < 0) {
        modd_log("wifi: AP not found");
        return -1;
    }

    wifi_cfg_ssid = ssid;
    wifi_cfg_pass = password;
#ifdef CONFIG_WIFI_IN_SHARED_POOL
    if (!wifi_join_need) {
        wifi_join_need = 1;
        net_need();
    }
#endif

#ifdef CONFIG_WIFI_WPA
    if (password) {
        uint32_t plen = 0, slen = 0;
        const char *p = password, *s = ssid;
        while (*p++) plen++;
        while (*s++) slen++;
        wifi_wpa_init((const uint8_t *)password, plen,
                      (const uint8_t *)ssid, slen,
                      wifi_sta.our_mac,
                      wifi_sta.results[idx].bssid);
#ifdef CONFIG_WIFI_IN_SHARED_POOL
        if (!wifi_inited || wifi_sta.state == WIFI_STATE_OFF) {
            wifi_disconnect();
            return -1;
        }
#endif
    }
#endif

    return wifi_sta_start_connect(ssid, password);
}

static void wifi_disconnect(void)
{
    modd_log("wifi: disconnect");
#ifdef CONFIG_WIFI_IN_SHARED_POOL
    if (wifi_inited) {
#endif
        wifi_lwip_stop_dhcp();
        wifi_sta_do_disconnect();
#ifdef CONFIG_WIFI_IN_SHARED_POOL
    }
#endif
    wifi_cfg_ssid = 0;
    wifi_cfg_pass = 0;
#ifdef CONFIG_WIFI_IN_SHARED_POOL
    if (wifi_join_need) {
        wifi_join_need = 0;
        net_release();
    }
#endif
}

static wifi_state_t wifi_get_state(void)
{
    return wifi_sta.state;
}

static uint32_t wifi_get_ip(void)
{
    return wifi_lwip_get_ip();
}

static uint32_t wifi_get_gw(void)
{
    return wifi_lwip_get_gw();
}

static uint32_t wifi_get_dns(void)
{
    return wifi_lwip_get_dns();
}

/* ---- event callbacks from lower layers ---- */

/* wifi_sta.c calls this when association + WPA completes */
static void wifi_on_connected(void)
{
    modd_log("wifi: associated, starting DHCP");
    toast_status("Acquiring IP");
    wifi_mac_tighten_filter();
    wifi_lwip_start_dhcp();
}

/* wifi_sta.c calls this on disconnect / deauth */
static void wifi_on_disconnected(void)
{
    modd_log("wifi: disconnected");
    toast_error("WiFi disconnected");
    wifi_lwip_stop_dhcp();
}

/* wifi_data.c calls this for each received Ethernet frame */
static void wifi_on_rx(const uint8_t *data, uint32_t len)
{
    wifi_lwip_input(data, len);
}

/* ---- main loop poll ---- */

#ifdef CONFIG_WIFI_DEBUG_AUTOSCAN
/* Debug: scan without the panel. First scan 2 s after the task starts, then
 * every 15 s while idle, so a power-cycle alone produces the scan
 * diagnostics for task debug:dump / debug:follow. */
static void wifi_autoscan_poll(void)
{
    uint32_t now = wifi_sta_now_ms();
    if (wifi_sta.state != WIFI_STATE_IDLE || (int32_t)(now - wifi_scan_next_ms) < 0)
        return;
    wifi_scan_next_ms = now + 15000u;
    modd_log("wifi: autoscan");
    wifi_scan(0);
}
#endif

#if defined(CONFIG_WIFI_AUTOJOIN) && defined(CONFIG_WIFI_SSID) && !defined(CONFIG_WIFI_DEBUG_AUTOSCAN)
/* Join without the panel. While WiFi is on and the station sits idle (not
 * joined yet, or a join just failed, or the AP went away), scan; the end of
 * a scan joins CONFIG_WIFI_SSID (wifi_sta_poll). First scan 2 s after the
 * task starts, then every 15 s. With the debug auto-scan built in, that one
 * does the scanning. */
static void wifi_autojoin_poll(void)
{
    uint32_t now = wifi_sta_now_ms();
    if (wifi_sta.state != WIFI_STATE_IDLE || CONFIG_WIFI_SSID[0] == '\0')
        return;
    /* checked before the timer, so switching WiFi on scans straight away */
#ifdef CONFIG_WIFI_IN_SHARED_POOL
    if (!wifi_active)
        return;
#elif defined(CONFIG_SYS_MENU_NETWORK)
    if (!sys_settings.wifi_on)
        return;
#endif
    if ((int32_t)(now - wifi_scan_next_ms) < 0)
        return;
    wifi_scan_next_ms = now + 15000u;
    modd_log("wifi: scan to join");
    wifi_scan(0);
}
#endif

#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE
/* WL_CTL0 release probe (iteration 12i, 2026-11-04) [observations
 * 2026-11-04T00:04]. The running vendor radio reads 0x14000 = 0 while its MAC
 * registers stay readable; modd keeps the 0xFFFF (reads 0xF103) written
 * at bring-up, and the five read-only "running" indicators never assert.
 * Host-triggered only (DAP D_WL0, `task debug:wl0`): fm1_fault() halts the
 * unit on the FAULT screen, so a probe that re-ran by itself after a
 * power-cycle could keep the device unreachable; on request it runs once,
 * from the WiFi task, with the indicator rows before and after. SoC-block
 * reads come first (always safe), the MAC read last: if WL_CTL0 is a level
 * gate after all, that read faults and the rows already in the ring plus the
 * crash record (task logs:hello) say so. NOINIT marker bit 0 is set for the
 * duration of the write so a halted attempt is visible next time. One scan
 * follows so the counters can move. */
static uint8_t wifi_wl0_probe_req;   /* set by the DAP command, consumed by the poll */
static uint8_t wifi_wl0_retry_req;   /* mode 2: retry the writes refused on a held block */

static void wifi_wl0_probe_request(uint32_t mode)
{
    if (mode == 2u) {
        wifi_wl0_retry_req = 1;
        return;
    }
    wifi_wl0_probe_req = 1;
}

/* 12k: the writes refused in iterations 12e/12h (0x14050 b23, 0x14060 b23,
 * 0x14058 vendor value, WDMA_CTL b16) were all attempted on a held block
 * (WL_CTL0 0xF103). Retry them on a released block, read each back, bracket
 * with indicator rows, then scan. A write that sticks now is an enable that
 * the hold was gating. Releases first so the result does not depend on how
 * this boot was started. */
static void wifi_wl0_retry_poll(void)
{
    if (!wifi_wl0_retry_req)
        return;
    wifi_wl0_retry_req = 0;
    modd_log("wifi: wl0 retry refused writes");
    wifi_ind_log("pre2", 1);
    *(volatile uint32_t *)0x14000u = 0u;
    fm1_delay_us(1000);
    *(volatile uint32_t *)0x14050u |= (1u << 23);
    *(volatile uint32_t *)0x14060u |= (1u << 23);
    *(volatile uint32_t *)0x14058u = 0x009F827Du;
    WDMA_CTL = 0x00010037u;
    fm1_delay_us(100);
    modd_log_int("wifi: r2 sirq=", (int32_t)(*(volatile uint32_t *)0x14050u));
    modd_log_int("wifi: r2 cfg60=", (int32_t)(*(volatile uint32_t *)0x14060u));
    modd_log_int("wifi: r2 cfg58=", (int32_t)(*(volatile uint32_t *)0x14058u));
    modd_log_int("wifi: r2 wctl=", (int32_t)WDMA_CTL);
    wifi_ind_log("pst2", 1);
    if (wifi_sta.state == WIFI_STATE_IDLE)
        wifi_scan(0);
}

static void wifi_wlctl0_probe_poll(void)
{
    volatile uint32_t *wlctl0 = (volatile uint32_t *)0x14000u;
    if (!wifi_wl0_probe_req)
        return;
    wifi_wl0_probe_req = 0;
    if (modd_dbg_nv.probe & 1u)
        modd_log("wifi: wl0 probe: previous attempt crashed");
    modd_log("wifi: wl0 probe");
    wifi_ind_log("pre0", 1);
    modd_dbg_nv.probe |= 1u;
    *wlctl0 = 0u;
    fm1_delay_us(100);
    modd_log_int("wifi: wl0 post=", (int32_t)*wlctl0);
    wifi_ind_log("soc0", 0);
    wifi_ind_log("mac0", 1);
    modd_dbg_nv.probe &= ~1u;
    modd_log("wifi: wl0 probe done");
    if (wifi_sta.state == WIFI_STATE_IDLE)
        wifi_scan(0);
}
#endif

#ifdef CONFIG_WIFI_IN_SHARED_POOL
/* Boot only: the bring-up branch of wifi_poll(), once, called from fm1_main() right after
 * net_boot_needs(), on the main stack before the scheduler and before interrupts, timer 5 and audio
 * are on (the ordering proven on hardware). Later activations (setting switched on, first need taken
 * later, re-init after a deinit) come up in wifi_poll(). No CONFIG_DOCK check: a module cannot be
 * running at boot. It does not poll. */
static void wifi_boot_init(void)
{
    if (wifi_active && !wifi_inited) {
        wifi_init();
        wifi_inited = 1;
    }
}
#endif /* CONFIG_WIFI_IN_SHARED_POOL */

#ifdef CONFIG_WIFI_IN_SHARED_POOL
/* Experimental: on-demand power cycling via wifi_need.c (wifi_active / wifi_inited state machine).
 * The deinit/reinit hardware cycle causes crash_pc=0x10804 — kept here for future fix. */
static void wifi_poll(void)
{
    if (!wifi_active && !wifi_inited) {
#ifdef CONFIG_DOCK
        wifi_dock_logged = 0;
#endif
        return;
    }
    if (wifi_active && !wifi_inited) {
#ifdef CONFIG_DOCK
        if (dock_active) {
            if (!wifi_dock_logged) {
                wifi_dock_logged = 1;
                modd_log("wifi: a module is running, not starting");
            }
            return;
        }
        wifi_dock_logged = 0;
#endif
        wifi_init();
        wifi_inited = 1;
        return;
    }
    if (!wifi_active) {
        if (wifi_sta.state > WIFI_STATE_IDLE) {
            uint8_t joining = wifi_sta.state >= WIFI_STATE_AUTH;
            wifi_disconnect();
            if (joining)
                toast_status("WiFi off");
        }
        if (wifi_inited) {
            wifi_deinit();
            wifi_inited = 0;
        }
        return;
    }
    wifi_defer_poll();
    wifi_timer_poll();
    wifi_sdio_poll_irq();
    wifi_sta_poll();
#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE
    wifi_wlctl0_probe_poll();
    wifi_wl0_retry_poll();
#endif
#ifdef CONFIG_WIFI_DEBUG_AUTOSCAN
    wifi_autoscan_poll();
#elif defined(CONFIG_WIFI_AUTOJOIN) && defined(CONFIG_WIFI_SSID)
    wifi_autojoin_poll();
#endif
}
#else
/* Stable baseline: WiFi initialised once at boot, no runtime power cycling. The setting
 * toggle only disconnects/reconnects the AP; the radio stays on. */
static void wifi_poll(void)
{
    wifi_defer_poll();
    wifi_timer_poll();
    wifi_sdio_poll_irq();
    wifi_sta_poll();
#ifdef CONFIG_SYS_MENU_NETWORK
    if (!sys_settings.wifi_on && wifi_sta.state >= WIFI_STATE_AUTH) {
        wifi_disconnect();
        toast_status("WiFi off");
    }
#endif
#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE
    wifi_wlctl0_probe_poll();
    wifi_wl0_retry_poll();
#endif
#ifdef CONFIG_WIFI_DEBUG_AUTOSCAN
    wifi_autoscan_poll();
#elif defined(CONFIG_WIFI_AUTOJOIN) && defined(CONFIG_WIFI_SSID)
    wifi_autojoin_poll();
#endif
}
#endif

#ifdef CONFIG_SCHED
static void wifi_task(void)
{
    modd_log("wifi: task poll enabled");
    for (;;) {
        wifi_poll();
        yield();
    }
}
#endif
