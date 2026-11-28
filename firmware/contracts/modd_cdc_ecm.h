// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* CDC-ECM, the pure part: finding an ECM function in a configuration
 * descriptor, and reassembling the notifications of its interrupt IN.
 * Bytes in, facts out: no hardware, no globals, host-tested in
 * tests/usb/cdc_ecm_test.c.  The driver is usb/net/usbnet_ecm.c, the
 * candidate bookkeeping usb/host/usb_hub.c. */
#pragma once
#include <stdint.h>

/* ---- configuration descriptor ---- */

#define CDC_ECM_DT_INTERFACE     0x04
#define CDC_ECM_DT_ENDPOINT      0x05
#define CDC_ECM_DT_CS_INTERFACE  0x24
#define CDC_ECM_SUBTYPE_UNION    0x06
#define CDC_ECM_SUBTYPE_ECM      0x0F
#define CDC_ECM_MAX_PACKET       64u     /* full speed */

typedef struct {
    uint8_t  cfg_value;          /* bConfigurationValue to select */
    uint8_t  ctrl_iface;         /* communications interface (class 02/06) */
    uint8_t  data_iface;         /* data interface (class 0A), from the union descriptor */
    uint8_t  data_alt;           /* its alternate setting that has the bulk pair */
    uint8_t  ep_in, ep_out;      /* bulk pair, endpoint numbers without the direction bit */
    uint16_t max_packet;         /* bulk IN packet size, capped at CDC_ECM_MAX_PACKET */
    uint8_t  mac_str_idx;        /* iMACAddress string index, 0 if none */
    uint8_t  notify_ep;          /* interrupt IN of the control interface, 0 if none */
    uint8_t  notify_interval;    /* its bInterval */
    uint16_t notify_maxp;        /* its packet size, 1..CDC_ECM_MAX_PACKET */
} cdc_ecm_desc_t;

/* Look for a CDC-ECM function in one configuration descriptor (`total`
 * bytes, starting at the configuration descriptor itself).  Returns 1 and
 * fills *out when the control interface, its union descriptor and a data
 * interface alternate setting with a bulk IN and a bulk OUT are all there. */
static int cdc_ecm_parse_config(const uint8_t *cfg, uint32_t total, cdc_ecm_desc_t *out)
{
    uint32_t pos;
    uint8_t ctrl_iface = 0xFF, data_iface = 0xFF;
    uint8_t mac_str = 0, in_ecm_ctrl = 0, in_data_alt = 0;
    uint8_t ep_in = 0, ep_out = 0, data_alt = 0;
    uint8_t notify_ep = 0, notify_interval = 0;
    uint16_t maxp = CDC_ECM_MAX_PACKET, notify_maxp = 8;

    if (total < 9u || cfg[0] < 2u) return 0;

    /* pass 1: the control interface, its functional descriptors and its
     * interrupt IN (NETWORK_CONNECTION, CONNECTION_SPEED_CHANGE) */
    for (pos = cfg[0]; pos + 2u <= total; pos += cfg[pos]) {
        uint8_t dlen = cfg[pos], dtype = cfg[pos + 1];
        if (dlen < 2u || pos + dlen > total) break;

        if (dtype == CDC_ECM_DT_INTERFACE && dlen >= 9u) {
            in_ecm_ctrl = (cfg[pos + 5] == 0x02 && cfg[pos + 6] == 0x06 &&
                           cfg[pos + 3] == 0);
            if (in_ecm_ctrl)
                ctrl_iface = cfg[pos + 2];
        }
        if (!in_ecm_ctrl) continue;

        if (dtype == CDC_ECM_DT_ENDPOINT && dlen >= 7u && !notify_ep &&
            (cfg[pos + 3] & 0x03u) == 0x03u && (cfg[pos + 2] & 0x80u)) {
            notify_ep = cfg[pos + 2] & 0x0Fu;
            notify_maxp = (uint16_t)(cfg[pos + 4] | (cfg[pos + 5] << 8));
            notify_interval = cfg[pos + 6];
        }
        if (dtype == CDC_ECM_DT_CS_INTERFACE && dlen >= 13u &&
            cfg[pos + 2] == CDC_ECM_SUBTYPE_ECM)
            mac_str = cfg[pos + 3];
        if (dtype == CDC_ECM_DT_CS_INTERFACE && dlen >= 5u &&
            cfg[pos + 2] == CDC_ECM_SUBTYPE_UNION)
            data_iface = cfg[pos + 4];
    }
    if (ctrl_iface == 0xFF || data_iface == 0xFF) return 0;

    /* pass 2: the data interface's alternate setting with the bulk pair
     * (alternate 0 has no endpoints) */
    for (pos = cfg[0]; pos + 2u <= total; pos += cfg[pos]) {
        uint8_t dlen = cfg[pos], dtype = cfg[pos + 1];
        if (dlen < 2u || pos + dlen > total) break;

        if (dtype == CDC_ECM_DT_INTERFACE && dlen >= 9u) {
            in_data_alt = (cfg[pos + 2] == data_iface && cfg[pos + 3] >= 1u &&
                           cfg[pos + 5] == 0x0A);
            if (in_data_alt)
                data_alt = cfg[pos + 3];
        }
        if (dtype == CDC_ECM_DT_ENDPOINT && dlen >= 7u && in_data_alt &&
            (cfg[pos + 3] & 0x03u) == 0x02u) {
            if ((cfg[pos + 2] & 0x80u) && !ep_in) {
                ep_in = cfg[pos + 2] & 0x0Fu;
                maxp = (uint16_t)(cfg[pos + 4] | (cfg[pos + 5] << 8));
            } else if (!(cfg[pos + 2] & 0x80u) && !ep_out) {
                ep_out = cfg[pos + 2] & 0x0Fu;
            }
        }
    }
    if (!ep_in || !ep_out) return 0;

    out->cfg_value = cfg[5];
    out->ctrl_iface = ctrl_iface;
    out->data_iface = data_iface;
    out->data_alt = data_alt;
    out->ep_in = ep_in;
    out->ep_out = ep_out;
    out->max_packet = maxp > CDC_ECM_MAX_PACKET ? CDC_ECM_MAX_PACKET : maxp;
    out->mac_str_idx = mac_str;
    out->notify_ep = notify_ep;
    out->notify_interval = notify_interval;
    out->notify_maxp = (notify_maxp == 0 || notify_maxp > CDC_ECM_MAX_PACKET)
                       ? CDC_ECM_MAX_PACKET : notify_maxp;
    return 1;
}

/* ---- notifications on the interrupt IN ---- */

#define CDC_NOTIFY_NETWORK_CONNECTION  0x00
#define CDC_NOTIFY_SPEED_CHANGE        0x2A
#define CDC_NOTIFY_HEADER              8u    /* bmRequestType, bNotification, wValue, wIndex, wLength */
#define CDC_NOTIFY_MAX                 16u   /* header + the 8 data bytes of a speed change */

typedef struct {
    uint8_t buf[CDC_NOTIFY_MAX];
    uint8_t len;
} cdc_notify_t;

static void cdc_notify_reset(cdc_notify_t *st) { st->len = 0; }

/* Add one packet of `n` bytes from an endpoint whose packet size is
 * `maxp`.  Returns 1 when st holds a notification to read (then call
 * cdc_notify_reset()), 0 while more packets are expected.  The header
 * says how much data follows; a short packet ends the notification early,
 * and one that ends before the header is complete is dropped. */
static int cdc_notify_feed(cdc_notify_t *st, const uint8_t *p, uint32_t n, uint32_t maxp)
{
    uint32_t i, need;
    for (i = 0; i < n && st->len < CDC_NOTIFY_MAX; i++)
        st->buf[st->len++] = p[i];
    if (st->len < CDC_NOTIFY_HEADER) {
        if (n != maxp) st->len = 0;
        return 0;
    }
    need = CDC_NOTIFY_HEADER + ((uint32_t)st->buf[6] | ((uint32_t)st->buf[7] << 8));
    if (need > CDC_NOTIFY_MAX) need = CDC_NOTIFY_MAX;
    return !(st->len < need && n == maxp);
}

/* A class notification from an interface: bmRequestType 0xA1. */
static int cdc_notify_valid(const cdc_notify_t *st)
{
    return st->len >= CDC_NOTIFY_HEADER && st->buf[0] == 0xA1u;
}

static uint8_t cdc_notify_code(const cdc_notify_t *st) { return st->buf[1]; }

/* wValue: 1 = connected, 0 = disconnected for NETWORK_CONNECTION */
static uint16_t cdc_notify_value(const cdc_notify_t *st)
{
    return (uint16_t)(st->buf[2] | (st->buf[3] << 8));
}

/* Downstream bit rate of a CONNECTION_SPEED_CHANGE (its data is the
 * upstream then the downstream rate, little-endian); 0 if the data is short. */
static uint32_t cdc_notify_speed_down(const cdc_notify_t *st)
{
    if (st->len < CDC_NOTIFY_MAX) return 0;
    return (uint32_t)st->buf[12] | ((uint32_t)st->buf[13] << 8) |
           ((uint32_t)st->buf[14] << 16) | ((uint32_t)st->buf[15] << 24);
}
