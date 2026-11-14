// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* OTG dual-role state machine: auto-switch between USB device and host.
 * Requires: modd_log.h, fm1_usb.h (device HAL), fm1_usb_host.h (host HAL).
 * Device-mode globals: usb.up, usb.config, usb.frame_same, usb.detached.
 * Host-mode globals: uhost (from fm1_usb_host.h). */
#ifndef MODD_OTG_H
#define MODD_OTG_H
#include <stdint.h>

#define OTG_DEVICE  0
#define OTG_HOST    1

#define OTG_FRAME_IDLE_THRESHOLD  100   /* ~1s at 10ms/tick */
#define OTG_DEVICE_DEBOUNCE_MS    500
#define OTG_HOST_REMOVAL_MS      5000
#define OTG_HOST_COOLDOWN_MS     8000   /* wait before retrying host after enum fail */
#define OTG_SETTLE_MS              50
#define OTG_HUB_SETTLE_MS       1000   /* extra settle time after hub detection */
/* Powered hubs backfeed VBUS, making OTG think a host is connected.
 * If VBUS is present but no host activity (no resets, no config) for this
 * long, force host mode. */
#define OTG_VBUS_NOHOST_MS      3000

/* MUSB DEVCTL register VBUS bits [4:3]: 11 = above VBusValid (~4.4V).
 * When an external host (iPhone, computer) supplies 5V, this reads true.
 * A passive USB device (flash drive) cannot supply VBUS. */
#define OTG_VBUS_VALID(dc)  (((dc) & 0x18u) == 0x18u)

static int otg_vbus_present(void)
{
    uint32_t dc = usb_host_sie_rd(SIE_DEVCTL);
    return OTG_VBUS_VALID(dc);
}

/* USB network link (net/usbnet.c, included later): the user wants the
 * port held in host mode for it, like udisk_active does for the disk. */
#ifdef CONFIG_USB_NET
static void usbnet_detach(void);         /* usbnet_wanted is declared in udisk/usb_hub.c */
#define OTG_HOST_WANTED() (udisk_active || usbnet_wanted)
#else
#define OTG_HOST_WANTED() (udisk_active)
#endif

typedef struct {
    uint8_t  mode;
    uint8_t  vbus_last;
    uint32_t debounce_ms;
    uint32_t idle_ms;
    uint32_t cooldown_until;
    uint32_t vbus_nohost_ms;   /* tracks VBUS-present-but-no-host-activity */
} otg_state_t;

static otg_state_t otg;

static void otg_init(void)
{
    otg.mode = OTG_DEVICE;
    otg.vbus_last = 2;
    otg.debounce_ms = 0;
    otg.idle_ms = 0;
    otg.vbus_nohost_ms = 0;
}

static void otg_switch_to_host(void)
{
    usb_detach();
    fm1_delay_ms(OTG_SETTLE_MS);
    usb_host_init(ep0buf);
    modd_log("OTG: -> host");
    otg.mode = OTG_HOST;
    otg.debounce_ms = 0;
    otg.idle_ms = 0;
}

static void otg_switch_to_device(void)
{
#ifdef CONFIG_USB_NET
    usbnet_detach();
#endif
    usb_host_deinit();
    usb.detached = 0;
    usb.retry_ms = 0;
    usb.config = 0;
    usb.frame_same = 0;
    usb.resets = 0;
    modd_log("OTG: -> device");
    otg.mode = OTG_DEVICE;
    otg.debounce_ms = 0;
    otg.idle_ms = 0;
}

static void otg_poll(uint32_t now_ms)
{
    uint8_t vb = otg_vbus_present() ? 1 : 0;
    if (vb != otg.vbus_last) {
        otg.vbus_last = vb;
        if (vb) {
            modd_log("OTG: VBUS on");
        } else {
            modd_log("OTG: VBUS off");
        }
    }

    if (otg.mode == OTG_DEVICE) {
        int should_switch;
#ifdef CONFIG_SYS_MENU_USB_MODE
        if (sys_settings.usb_mode == 0) {
            should_switch = 0;
        } else
#endif
        if (otg_vbus_present()) {
            /* VBUS is present: normally means a host is connected.
             * But powered hubs backfeed VBUS with no host activity.
             * If we see VBUS but no resets and no config for a while,
             * force host mode to try hub enumeration. */
            if (!usb.config && usb.resets == 0) {
                if (otg.vbus_nohost_ms == 0) {
                    otg.vbus_nohost_ms = now_ms ? now_ms : 1;
                } else if (now_ms - otg.vbus_nohost_ms >= OTG_VBUS_NOHOST_MS) {
                    modd_log("OTG: VBUS but no host activity, forcing host");
                    should_switch = 1;
                    goto otg_check_switch;
                }
            } else {
                otg.vbus_nohost_ms = 0;
            }
            should_switch = 0;
        } else if (!usb.up) {
            should_switch = 0;
        } else if (usb.config) {
            should_switch = usb.frame_same >= OTG_FRAME_IDLE_THRESHOLD;
        } else if (usb.resets > 0) {
            should_switch = 0;
        } else {
            should_switch = 1;
        }
        if (usb.config && usb.frame_same < OTG_FRAME_IDLE_THRESHOLD)
            should_switch = 0;
#if defined(CONFIG_UDISK) || defined(CONFIG_USB_NET)
        if (OTG_HOST_WANTED())
            should_switch = 1;
#endif

otg_check_switch:
        if (should_switch && (otg.cooldown_until == 0 || now_ms >= otg.cooldown_until)) {
            if (otg.debounce_ms == 0) {
                otg.debounce_ms = now_ms;
                if (usb.config)
                    modd_log("OTG: frames frozen, debouncing");
                else
                    modd_log("OTG: no host, debouncing");
            } else if (now_ms - otg.debounce_ms >= OTG_DEVICE_DEBOUNCE_MS)
                otg_switch_to_host();
        } else {
            if (otg.debounce_ms != 0)
                modd_log("OTG: debounce cancelled");
            otg.debounce_ms = 0;
            otg.cooldown_until = 0;
        }
    } else if (otg.mode == OTG_HOST) {
        usb_host_poll();
#if defined(CONFIG_UDISK) || defined(CONFIG_USB_NET)
        if (!OTG_HOST_WANTED()) {
            modd_log("OTG: host not wanted, -> device");
            otg_switch_to_device();
            return;
        }
#endif
        int failed = uhost.state == UHOST_ERROR;
        int connected = usb_host_connected();
        int idle = !connected || failed;
        if (idle) {
            if (otg.idle_ms == 0) {
                otg.idle_ms = now_ms;
                if (failed)
                    modd_log("OTG: host enum failed, waiting");
                else
                    modd_log("OTG: device removed, waiting");
            } else if (now_ms - otg.idle_ms >= OTG_HOST_REMOVAL_MS) {
                if (failed) {
                    modd_log_int("OTG: cooldown ms", OTG_HOST_COOLDOWN_MS);
                    otg.cooldown_until = now_ms + OTG_HOST_COOLDOWN_MS;
                }
                otg_switch_to_device();
            }
        } else {
            if (otg.idle_ms != 0)
                modd_log("OTG: device reconnected");
            otg.idle_ms = 0;
        }
    }
}

#endif /* MODD_OTG_H */
