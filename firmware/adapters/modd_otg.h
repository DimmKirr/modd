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
#define OTG_HOST_REMOVAL_MS      2000
#define OTG_HOST_COOLDOWN_MS     5000   /* wait before retrying host after enum fail */
#define OTG_SETTLE_MS              50

/* MUSB DEVCTL register VBUS bits [4:3]: 11 = above VBusValid (~4.4V).
 * When an external host (iPhone, computer) supplies 5V, this reads true.
 * A passive USB device (flash drive) cannot supply VBUS. */
#define OTG_VBUS_VALID(dc)  (((dc) & 0x18u) == 0x18u)

static int otg_vbus_present(void)
{
    uint32_t dc = usb_host_sie_rd(SIE_DEVCTL);
    return OTG_VBUS_VALID(dc);
}

typedef struct {
    uint8_t  mode;
    uint8_t  vbus_last;
    uint32_t debounce_ms;
    uint32_t idle_ms;
    uint32_t cooldown_until;
} otg_state_t;

static otg_state_t otg;

static void otg_init(void)
{
    otg.mode = OTG_DEVICE;
    otg.vbus_last = 2;
    otg.debounce_ms = 0;
    otg.idle_ms = 0;
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
#ifdef CONFIG_UDISK
        if (udisk_active)
            should_switch = 1;
#endif

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
#ifdef CONFIG_UDISK
        if (!udisk_active) {
            modd_log("OTG: udisk off, -> device");
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
