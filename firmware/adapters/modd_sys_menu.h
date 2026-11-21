// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Settings menu adapter: item descriptors and tabs.
 * UI-agnostic: any renderer (Felucca, Melodee, core) can iterate this.
 * Items are static via Kconfig. See design doc for dynamic registration TODO. */
#ifndef MODD_SYS_MENU_H
#define MODD_SYS_MENU_H

/* --- Tabs: match upstream Felucca order --- */

enum {
    SMTAB_DISPLAY, SMTAB_CONTROL, SMTAB_AUDIO,
    SMTAB_MIDI, SMTAB_SYSTEM,
#ifdef CONFIG_DOCK
    SMTAB_INSTR,
#endif
#ifdef CONFIG_SYS_MENU_HARDWARE
    SMTAB_HARDWARE,
#endif
#ifdef CONFIG_SYS_MENU_NETWORK
    SMTAB_NETWORK,
#endif
#ifdef CONFIG_BLE
    SMTAB_BLE,
#endif
    SMTAB_COUNT
};

static const char *const SMTAB_NAME[SMTAB_COUNT] = {
    "DISPLAY", "CONTROL", "AUDIO", "MIDI", "SYSTEM",
#ifdef CONFIG_DOCK
    "INSTR",
#endif
#ifdef CONFIG_SYS_MENU_HARDWARE
    "HW",
#endif
#ifdef CONFIG_SYS_MENU_NETWORK
    "NET",
#endif
#ifdef CONFIG_BLE
    "BLE",
#endif
};

/* --- Item descriptor --- */

typedef struct {
    const char *name;
    uint8_t     tab;
    uint8_t     n_values;
    const char *const *values;
    uint8_t   (*get)(void);
    void      (*set)(uint8_t v);
} sys_menu_item_t;

/* --- Value name arrays --- */

static const char *const SM_ONOFF[] = {"OFF", "ON"};

#ifdef CONFIG_SYS_MENU_USB_MODE
static const char *const SM_USB_MODE[] = {"MIDI", "OTG"};

static uint8_t sm_usb_mode_get(void) { return sys_settings.usb_mode; }
static void    sm_usb_mode_set(uint8_t v)
{
    if (v > 1u) v = 1u;
    sys_settings.usb_mode = v;
    sys_settings_dirty = 1;
}
#endif

#ifdef CONFIG_SYS_MENU_MIDI_CH
static const char *const SM_MIDI_CH[] = {
    "1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"
};

static uint8_t sm_midi_ch_get(void) { return sys_settings.midi_ch; }
static void    sm_midi_ch_set(uint8_t v)
{
    if (v > 15u) v = 15u;
    sys_settings.midi_ch = v;
    sys_settings_dirty = 1;
}
#endif

#if (defined(CONFIG_SYS_MENU_HARDWARE) || defined(CONFIG_SYS_MENU_NETWORK)) && (defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE))
#ifdef CONFIG_WIFI
static void wifi_set_enabled(uint8_t on);   /* wifi/wifi_need.c */
#endif
static uint8_t sm_wifi_on_get(void) { return sys_settings.wifi_on; }
static void    sm_wifi_on_set(uint8_t v)
{
    if (v > 1u) v = 1u;
    sys_settings.wifi_on = v;
    sys_settings_dirty = 1;
#ifdef CONFIG_WIFI
    wifi_set_enabled(v);        /* the radio follows the setting now, not at the next boot */
#endif
}
#endif

#ifdef CONFIG_USB_NET
static uint8_t usbnet_wanted;        /* effective: the setting allows it and a need is held (eth_need) */
static uint8_t ecm_wanted;
static uint8_t usbnet_allowed;       /* the setting (a permission) */
static uint8_t ecm_allowed;
static void    usbnet_set_wanted(uint8_t on);
static void    ecm_set_wanted(uint8_t on);
static uint8_t sm_usb_net_get(void) { return usbnet_allowed || ecm_allowed; }
static void    sm_usb_net_set(uint8_t v)
{
    uint8_t on = v > 0u;
    usbnet_set_wanted(on);
    ecm_set_wanted(on);
    sys_settings.usb_net_on = on;
    sys_settings_dirty = 1;
}
#endif

#ifdef CONFIG_SYS_MENU_BLE
static uint8_t sm_ble_on_get(void) { return sys_settings.ble_on; }
static void    sm_ble_on_set(uint8_t v)
{
    if (v > 1u) v = 1u;
    sys_settings.ble_on = v;
    sys_settings_dirty = 1;
}

static uint8_t sm_ble_disc_get(void) { return sys_settings.ble_discoverable; }
static void    sm_ble_disc_set(uint8_t v)
{
    if (v > 1u) v = 1u;
    sys_settings.ble_discoverable = v;
    sys_settings_dirty = 1;
}

static const char *const SM_BLE_NAME[] = { CONFIG_FIRMWARE_NAME };
static uint8_t sm_ble_name_get(void) { return 0; }
static void    sm_ble_name_set(uint8_t v) { (void)v; }
#endif

/* --- LIGHTS setting (LED brightness: the modd theme's leds.c) --- */

#ifdef CONFIG_UI_THEME_MODD
static const char *const SM_LIGHTS[] = {"OFF", "LOW", "MID", "HIGH", "FULL"};

static uint8_t sm_lights_get(void) {
    return sys_settings.ui_lights < 5u ? sys_settings.ui_lights : 3u;
}
static void sm_lights_set(uint8_t v) {
    if (v > 4u) v = 4u;
    sys_settings.ui_lights = v;
    modd_lights = v;
    sys_settings_dirty = 1;
}
#endif

/* --- DEVICES tab items --- */

#if defined(CONFIG_SYS_MENU_HARDWARE) && defined(CONFIG_UDISK)
/* DISK: may the port go host for a USB disk at all. ON does not mount anything by itself: the
 * disk comes up while a page or a copy needs it (udisk/udisk.c). OFF keeps the port a device. */
static uint8_t udisk_is_enabled(void);
static void    udisk_set_enabled(uint8_t on);
static uint8_t sm_udisk_get(void) { return udisk_is_enabled(); }
static void    sm_udisk_set(uint8_t v)
{
    uint8_t on = v > 0u;
    udisk_set_enabled(on);
    sys_settings.udisk_off = !on;
    sys_settings_dirty = 1;
}
#endif

#if defined(CONFIG_SYS_MENU_HARDWARE) && defined(CONFIG_USB_AUDIO)
static uint8_t sm_usb_audio_get(void) { return 0; }
static void    sm_usb_audio_set(uint8_t v) { (void)v; }
#endif

#if defined(CONFIG_SYS_MENU_HARDWARE) && defined(CONFIG_USB_AUDIO_HOST)
static uint8_t sm_usb_audio_host_get(void) { return 0; }
static void    sm_usb_audio_host_set(uint8_t v) { (void)v; }
#endif

#ifdef CONFIG_SYS_MENU_DEMO
static uint8_t sm_demo_val;
static uint8_t sm_demo_get(void) { return sm_demo_val; }
static void    sm_demo_set(uint8_t v) { sm_demo_val = v; }
#endif

/* --- Item array (static, Kconfig-gated) --- */

static const sys_menu_item_t SYS_MENU[] = {
#ifdef CONFIG_UI_THEME_MODD
    { "LIGHTS",   SMTAB_DISPLAY, 5, SM_LIGHTS, sm_lights_get, sm_lights_set },
#endif
#ifdef CONFIG_SYS_MENU_USB_MODE
    { "USB MODE",   SMTAB_SYSTEM, 2,  SM_USB_MODE,  sm_usb_mode_get, sm_usb_mode_set },
#endif
#ifdef CONFIG_SYS_MENU_MIDI_CH
    { "MIDI CH",    SMTAB_MIDI,  16,  SM_MIDI_CH,   sm_midi_ch_get,  sm_midi_ch_set },
#endif
#if defined(CONFIG_SYS_MENU_HARDWARE) && (defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE))
    { "WIFI",         SMTAB_HARDWARE, 2, SM_ONOFF,  sm_wifi_on_get,  sm_wifi_on_set },
#endif
#if defined(CONFIG_SYS_MENU_HARDWARE) && defined(CONFIG_UDISK)
    { "DISK",         SMTAB_HARDWARE, 2, SM_ONOFF,   sm_udisk_get,    sm_udisk_set },
#endif
#if defined(CONFIG_SYS_MENU_HARDWARE) && defined(CONFIG_USB_AUDIO)
    { "AUDIO OUT",    SMTAB_HARDWARE, 2, SM_ONOFF,   sm_usb_audio_get, sm_usb_audio_set },
#endif
#if defined(CONFIG_SYS_MENU_HARDWARE) && defined(CONFIG_USB_AUDIO_HOST)
    { "AUDIO IN",     SMTAB_HARDWARE, 2, SM_ONOFF,   sm_usb_audio_host_get, sm_usb_audio_host_set },
#endif
#if defined(CONFIG_SYS_MENU_HARDWARE) && defined(CONFIG_USB_NET)
    { "ETHERNET",     SMTAB_HARDWARE, 2, SM_ONOFF,   sm_usb_net_get,  sm_usb_net_set },
#endif
#ifdef CONFIG_SYS_MENU_BLE
    { "BLUETOOTH",    SMTAB_BLE,  2,  SM_ONOFF,     sm_ble_on_get,   sm_ble_on_set },
    { "DISCOVERABLE", SMTAB_BLE,  2,  SM_ONOFF,     sm_ble_disc_get, sm_ble_disc_set },
    { "NAME",         SMTAB_BLE,  1,  SM_BLE_NAME,  sm_ble_name_get, sm_ble_name_set },
#endif
#ifdef CONFIG_SYS_MENU_DEMO
    { "DEMO A",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
    { "DEMO B",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
    { "DEMO C",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
    { "DEMO D",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
    { "DEMO E",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
    { "DEMO F",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
    { "DEMO G",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
    { "DEMO H",  SMTAB_SYSTEM, 2, SM_ONOFF, sm_demo_get, sm_demo_set },
#endif
    { 0, 0, 0, 0, 0, 0 }  /* sentinel */
};
#define SYS_MENU_COUNT \
    ((sizeof SYS_MENU / sizeof SYS_MENU[0]) - 1u)  /* exclude sentinel */

/* --- TODO: Dynamic item registration (future) ---
 *
 * Subsystems loaded at runtime (plugins, external modules) could register
 * items via sys_menu_register(item). This would require:
 *
 *   #define SYS_MENU_DYN_MAX 16
 *   static sys_menu_item_t sys_menu_dyn[SYS_MENU_DYN_MAX];
 *   static uint32_t sys_menu_dyn_count;
 *
 *   static void sys_menu_register(const sys_menu_item_t *item) {
 *       if (sys_menu_dyn_count < SYS_MENU_DYN_MAX)
 *           sys_menu_dyn[sys_menu_dyn_count++] = *item;
 *   }
 *
 * UI iteration would walk SYS_MENU[] first, then sys_menu_dyn[].
 * Dynamic items could also bring new tabs (SMTAB_DYN_BASE + n).
 * Not implemented: all items are currently static via Kconfig.
 */

#endif /* MODD_SYS_MENU_H */
