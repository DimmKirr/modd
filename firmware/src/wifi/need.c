// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

#ifdef CONFIG_WIFI_IN_SHARED_POOL
/* When the WiFi radio runs: the WiFi setting allows it (sys_settings.wifi_on, a permission) and
 * somebody needs a network right now (wifi_needs, taken by net.c through net_need() while a
 * consumer uses the network). Both together are wifi_active. It is the same pattern as
 * udisk_need() / udisk_release() (udisk/udisk.c): every wifi_need() pairs with one wifi_release().
 *
 * wifi_power() only records the change. The radio comes up in one of two contexts:
 *  - boot: a need held at boot with the setting on is brought up by wifi_boot_init() (wifi_api.c),
 *    called from fm1_main() right after net_boot_needs(): main stack, interrupts off, before the
 *    scheduler (the ordering proven on hardware);
 *  - later (setting switched on, first need taken later, re-init after a deinit): wifi_poll()
 *    (wifi_api.c), in the WiFi task (or the main loop without the scheduler).
 * Tear-down is always wifi_poll(): no wifi_active and a radio -> leave the network, wifi_deinit()
 * (interrupts masked, DMA, MAC and RF off). With the radio down and no need, wifi_poll() returns
 * at once. */

static uint8_t wifi_needs;
static uint8_t wifi_active;
static uint8_t wifi_inited;

static void wifi_power(uint8_t on);              /* wifi_api.c: no hardware access, wifi_poll() acts */

static uint8_t wifi_is_active(void) { return wifi_active; }

static void wifi_need_update(void)
{
    uint8_t on = (sys_settings.wifi_on && wifi_needs) ? 1 : 0;
    if (on == wifi_active) return;
    wifi_active = on;
    modd_log(on ? "wifi: activate" : "wifi: deactivate");
    modd_log_int("wifi: needs=", (int32_t)wifi_needs);
    modd_log_int("wifi: setting=", (int32_t)sys_settings.wifi_on);
    wifi_power(on);
}

/* Take / give back a need. Returns 0 when the WiFi setting is off: the need is still counted, so
 * every wifi_need() pairs with one wifi_release(), and the radio comes up if the setting is turned
 * on meanwhile. */
static int wifi_need(void)
{
    if (wifi_needs < 255u) wifi_needs++;
    wifi_need_update();
    return sys_settings.wifi_on;
}

static void wifi_release(void)
{
    if (wifi_needs) wifi_needs--;
    wifi_need_update();
}

static void wifi_set_enabled(uint8_t on)         /* the WIFI setting (modd_sys_menu.h) */
{
    (void)on;                                    /* sys_settings.wifi_on is already set */
    wifi_need_update();
}

#else /* !CONFIG_WIFI_IN_SHARED_POOL ------------------------------------------
 * Non-pool: radio lifecycle is static (init at boot if wifi_on, never deinit).
 * Zero BSS; callers compile via inline stubs. */

static uint8_t wifi_is_active(void) { return sys_settings.wifi_on; }
static int  wifi_need(void)         { return sys_settings.wifi_on; }
static void wifi_release(void)      {}
static void wifi_set_enabled(uint8_t on) { (void)on; }

#endif /* CONFIG_WIFI_IN_SHARED_POOL */
