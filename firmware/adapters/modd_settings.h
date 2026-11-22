// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Settings persistence adapter.
 * Contract: sys_fl_load(p, n) loads up to n bytes into p (0 = ok, -1 = defaults).
 * sys_fl_save(p, n) writes n bytes from p to flash (0 = ok).
 * Names prefixed sys_fl_ to avoid collision with upstream settings_load/save
 * stubs in modd_storage.h.
 * Backend selected by Kconfig: SETTINGS_FMLAB, SETTINGS_FELUCCA, SETTINGS_NONE. */
#ifndef MODD_SETTINGS_H
#define MODD_SETTINGS_H

/* --- Backend --- */

#if defined(CONFIG_SETTINGS_FMLAB)
#include "settings/settings_fmlab.c"
#elif defined(CONFIG_SETTINGS_NONE)
static int sys_fl_load(void *p, uint32_t n) { (void)p; (void)n; return -1; }
static int sys_fl_save(const void *p, uint32_t n) { (void)p; (void)n; return 0; }
#else
#error "No settings backend selected (enable SETTINGS_FMLAB or SETTINGS_NONE)"
#endif

/* --- Settings struct --- */

typedef struct {
#ifdef CONFIG_SYS_MENU_USB_MODE
    uint8_t usb_mode;       /* 0 = MIDI (device), 1 = OTG (host) */
#endif
#ifdef CONFIG_SYS_MENU_MIDI_CH
    uint8_t midi_ch;        /* 0..15 */
#endif
#ifdef CONFIG_SYS_MENU_BLE
    uint8_t ble_on;         /* 0 = OFF, 1 = ON */
    uint8_t ble_discoverable; /* 0 = OFF, 1 = ON (advertising) */
#endif
#ifdef CONFIG_BLE
    uint8_t ble_addr[7];    /* [0..5] = saved random static address, [6] = 0xA5 if valid */
#endif
#ifdef CONFIG_SYS_MENU_NETWORK
    uint8_t wifi_on;        /* 0 = OFF, 1 = ON (user preference; module checks at connect) */
#endif
#ifdef CONFIG_USB_NET
    uint8_t usb_net_on;    /* 0 = OFF, 1 = ON (auto-detects ECM or iPhone) */
#endif
#if defined(CONFIG_UI_THEME_MODD) || defined(CONFIG_UI_THEME_FELUCCA)
    uint8_t ui_pal;         /* palette: UI_PAL_TAG + index (Settings > DISPLAY > THEME) */
    uint8_t ui_lights;      /* LED brightness: 0=OFF, 1=LOW, 2=MID, 3=HIGH, 4=FULL */
#endif
#ifdef CONFIG_UDISK
    uint8_t udisk_off;      /* the DISK setting, inverted: 0 = ON (default), 1 = OFF. A store saved
                             * before this field existed has its _end byte (0) here and reads ON. */
#endif
    uint8_t _end;           /* always present so struct is never empty */
} sys_settings_t;

static sys_settings_t sys_settings;
static volatile uint8_t sys_settings_dirty;

/* modd_lights is declared in modd.c (needs to exist even without SYS_MENU) */

static void sys_settings_defaults(void)
{
#ifdef CONFIG_SYS_MENU_USB_MODE
    sys_settings.usb_mode = 0;   /* DEVICE */
#endif
#ifdef CONFIG_SYS_MENU_MIDI_CH
    sys_settings.midi_ch = 0;    /* channel 1 */
#endif
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
#ifdef CONFIG_WIFI_DEFAULT_ON
    sys_settings.wifi_on = 1;    /* ON (CONFIG_WIFI_DEFAULT_ON) */
#else
    sys_settings.wifi_on = 0;    /* OFF */
#endif
#endif
#ifdef CONFIG_USB_NET
    sys_settings.usb_net_on = 0;
#endif
#ifdef CONFIG_SYS_MENU_BLE
    sys_settings.ble_on = 1;     /* ON */
    sys_settings.ble_discoverable = 1; /* ON */
#endif
#if defined(CONFIG_UI_THEME_MODD) || defined(CONFIG_UI_THEME_FELUCCA)
    sys_settings.ui_pal = 0;     /* below UI_PAL_TAG: reads as the default palette */
    sys_settings.ui_lights = 3;  /* HIGH */
#endif
#ifdef CONFIG_UDISK
    sys_settings.udisk_off = 0;  /* ON: the disk may be used when something needs it */
#endif
    sys_settings._end = 0;
}

static void sys_settings_init(void)
{
    sys_settings_defaults();
    sys_fl_load(&sys_settings, sizeof sys_settings);
#ifdef CONFIG_SYS_MENU_MIDI_CH
    if (sys_settings.midi_ch > 15u) sys_settings.midi_ch = 0;
#endif
#ifdef CONFIG_SYS_MENU_BLE
    if (sys_settings.ble_on > 1u) sys_settings.ble_on = 1;
    if (sys_settings.ble_discoverable > 1u) sys_settings.ble_discoverable = 1;
#endif
#ifdef CONFIG_USB_NET
    if (sys_settings.usb_net_on > 1u) sys_settings.usb_net_on = 0;
#endif
#if defined(CONFIG_UI_THEME_MODD) || defined(CONFIG_UI_THEME_FELUCCA)
    if (sys_settings.ui_lights > 4u) sys_settings.ui_lights = 3;
#endif
#ifdef CONFIG_UDISK
    if (sys_settings.udisk_off > 1u) sys_settings.udisk_off = 0;
#endif
    sys_settings_dirty = 0;
}

static void sys_settings_flush(void)
{
    if (!sys_settings_dirty)
        return;
    sys_settings_dirty = 0;
    sys_fl_save(&sys_settings, sizeof sys_settings);
}

#endif /* MODD_SETTINGS_H */
