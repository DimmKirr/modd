// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test for the pure part of CDC-ECM (firmware/adapters/modd_cdc_ecm.h):
 * the configuration descriptor parser and the notification reassembly. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "modd_cdc_ecm.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- descriptor building blocks ---- */

static uint8_t buf[256];
static uint32_t len;

static void put(const uint8_t *d, uint32_t n) { memcpy(buf + len, d, n); len += n; }
#define PUT(...) do { const uint8_t d_[] = { __VA_ARGS__ }; put(d_, sizeof d_); } while (0)

static void cfg_begin(uint8_t value)
{
    len = 0;
    PUT(9, 0x02, 0, 0, 2, value, 0, 0xA0, 50);
}
static void cfg_end(void) { buf[2] = (uint8_t)len; buf[3] = (uint8_t)(len >> 8); }

static void iface(uint8_t num, uint8_t alt, uint8_t neps, uint8_t cls, uint8_t sub)
{
    PUT(9, 0x04, num, alt, neps, cls, sub, 0, 0);
}
static void endpoint(uint8_t addr, uint8_t attr, uint16_t maxp, uint8_t interval)
{
    PUT(7, 0x05, addr, attr, (uint8_t)maxp, (uint8_t)(maxp >> 8), interval);
}
static void cdc_header(void) { PUT(5, 0x24, 0x00, 0x10, 0x01); }
static void cdc_union(uint8_t ctrl, uint8_t data) { PUT(5, 0x24, 0x06, ctrl, data); }
static void cdc_ecm_func(uint8_t imac)
{
    PUT(13, 0x24, 0x0F, imac, 0, 0, 0, 0, 0xEA, 0x05, 0, 0, 0);
}

/* The ECM configuration of an ASIX AX88179A at full speed, rebuilt from
 * what the device reported on the bench (2026-11-15): configuration 3,
 * control interface 0 with an interrupt IN ep 1 (16 bytes, bInterval 160),
 * data interface 1 with the bulk pair on alternate 1 (IN ep 2, OUT ep 3). */
static void mk_ax88179a(uint16_t bulk_maxp, uint16_t notify_maxp)
{
    cfg_begin(3);
    iface(0, 0, 1, 0x02, 0x06);
    cdc_header();
    cdc_union(0, 1);
    cdc_ecm_func(7);
    endpoint(0x81, 0x03, notify_maxp, 160);
    iface(1, 0, 0, 0x0A, 0x00);
    iface(1, 1, 2, 0x0A, 0x00);
    endpoint(0x82, 0x02, bulk_maxp, 0);
    endpoint(0x03, 0x02, bulk_maxp, 0);
    cfg_end();
}

static void test_parse(void)
{
    cdc_ecm_desc_t d;

    mk_ax88179a(64, 16);
    memset(&d, 0xEE, sizeof d);
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 1);
    CHECK(d.cfg_value == 3);
    CHECK(d.ctrl_iface == 0 && d.data_iface == 1 && d.data_alt == 1);
    CHECK(d.ep_in == 2 && d.ep_out == 3 && d.max_packet == 64);
    CHECK(d.mac_str_idx == 7);
    CHECK(d.notify_ep == 1 && d.notify_interval == 160 && d.notify_maxp == 16);

    /* a high-speed descriptor: both packet sizes come back capped */
    mk_ax88179a(512, 0);
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 1);
    CHECK(d.max_packet == 64 && d.notify_maxp == 64);

    /* no interrupt IN on the control interface: still a candidate */
    cfg_begin(2);
    iface(0, 0, 0, 0x02, 0x06);
    cdc_union(0, 1);
    cdc_ecm_func(3);
    iface(1, 0, 0, 0x0A, 0x00);
    iface(1, 1, 2, 0x0A, 0x00);
    endpoint(0x02, 0x02, 64, 0);
    endpoint(0x81, 0x02, 64, 0);
    cfg_end();
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 1);
    CHECK(d.notify_ep == 0 && d.ep_in == 1 && d.ep_out == 2 && d.cfg_value == 2);

    /* the adapter's other two configurations are not ECM */
    cfg_begin(1);                        /* vendor specific */
    iface(0, 0, 3, 0xFF, 0xFF);
    endpoint(0x81, 0x03, 8, 11);
    endpoint(0x82, 0x02, 64, 0);
    endpoint(0x03, 0x02, 64, 0);
    cfg_end();
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 0);

    cfg_begin(2);                        /* CDC-NCM: subclass 0x0D */
    iface(0, 0, 1, 0x02, 0x0D);
    cdc_union(0, 1);
    endpoint(0x81, 0x03, 16, 160);
    iface(1, 1, 2, 0x0A, 0x00);
    endpoint(0x82, 0x02, 64, 0);
    endpoint(0x03, 0x02, 64, 0);
    cfg_end();
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 0);

    /* no union descriptor: the data interface is unknown */
    cfg_begin(3);
    iface(0, 0, 1, 0x02, 0x06);
    cdc_ecm_func(7);
    endpoint(0x81, 0x03, 16, 160);
    iface(1, 1, 2, 0x0A, 0x00);
    endpoint(0x82, 0x02, 64, 0);
    endpoint(0x03, 0x02, 64, 0);
    cfg_end();
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 0);

    /* data interface with alternate 0 only: no bulk pair */
    cfg_begin(3);
    iface(0, 0, 1, 0x02, 0x06);
    cdc_union(0, 1);
    endpoint(0x81, 0x03, 16, 160);
    iface(1, 0, 0, 0x0A, 0x00);
    cfg_end();
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 0);

    /* an interrupt IN that belongs to another interface is not the notify pipe */
    cfg_begin(3);
    iface(0, 0, 0, 0x02, 0x06);
    cdc_union(0, 1);
    iface(2, 0, 1, 0x03, 0x00);
    endpoint(0x84, 0x03, 8, 10);
    iface(1, 1, 2, 0x0A, 0x00);
    endpoint(0x82, 0x02, 64, 0);
    endpoint(0x03, 0x02, 64, 0);
    cfg_end();
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 1);
    CHECK(d.notify_ep == 0);

    /* truncated reads never run past `total` and never match by accident */
    mk_ax88179a(64, 16);
    {
        uint32_t full = len, t;
        for (t = 0; t < full; t++) {
            static uint8_t cut[256];
            memset(cut, 0xFF, sizeof cut);
            memcpy(cut, buf, t);
            (void)cdc_ecm_parse_config(cut, t, &d);
        }
        CHECK(cdc_ecm_parse_config(buf, full - 1, &d) == 0);  /* OUT endpoint cut off */
        CHECK(cdc_ecm_parse_config(buf, 8, &d) == 0);
    }

    /* a zero-length descriptor stops the walk instead of looping */
    mk_ax88179a(64, 16);
    buf[9] = 0;
    CHECK(cdc_ecm_parse_config(buf, len, &d) == 0);
}

static void test_notify(void)
{
    /* as received from the AX88179A on the bench */
    static const uint8_t conn_off[8] = { 0xA1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
    static const uint8_t conn_on[8]  = { 0xA1, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00 };
    /* speed change: 8 data bytes, up then down, 100 Mbit/s = 0x05F5E100 */
    static const uint8_t speed[16] = { 0xA1, 0x2A, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
                                       0x00, 0xE1, 0xF5, 0x05, 0x00, 0xE1, 0xF5, 0x05 };
    cdc_notify_t st;

    /* 16-byte endpoint: the 8-byte notification is a short packet */
    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, conn_on, 8, 16) == 1);
    CHECK(cdc_notify_valid(&st));
    CHECK(cdc_notify_code(&st) == CDC_NOTIFY_NETWORK_CONNECTION);
    CHECK(cdc_notify_value(&st) == 1);

    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, conn_off, 8, 16) == 1);
    CHECK(cdc_notify_value(&st) == 0);

    /* 8-byte endpoint: a full packet, but the header says no data follows */
    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, conn_on, 8, 8) == 1);
    CHECK(cdc_notify_value(&st) == 1);

    /* speed change in one packet */
    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, speed, 16, 16) == 1);
    CHECK(cdc_notify_valid(&st) && cdc_notify_code(&st) == CDC_NOTIFY_SPEED_CHANGE);
    CHECK(cdc_notify_speed_down(&st) == 100000000u);

    /* the same split 8 + 8 on an 8-byte endpoint */
    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, speed, 8, 8) == 0);
    CHECK(cdc_notify_feed(&st, speed + 8, 8, 8) == 1);
    CHECK(cdc_notify_code(&st) == CDC_NOTIFY_SPEED_CHANGE);
    CHECK(cdc_notify_speed_down(&st) == 100000000u);

    /* a speed change whose data never came: complete, but no rate */
    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, speed, 8, 16) == 1);
    CHECK(cdc_notify_speed_down(&st) == 0);

    /* runts and empty packets are dropped */
    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, conn_on, 3, 16) == 0);
    CHECK(st.len == 0);
    CHECK(cdc_notify_feed(&st, conn_on, 0, 16) == 0);
    CHECK(st.len == 0);
    CHECK(!cdc_notify_valid(&st));

    /* a header split across packets of a tiny endpoint */
    cdc_notify_reset(&st);
    CHECK(cdc_notify_feed(&st, conn_on, 4, 4) == 0);
    CHECK(cdc_notify_feed(&st, conn_on + 4, 4, 4) == 1);
    CHECK(cdc_notify_valid(&st) && cdc_notify_value(&st) == 1);

    /* not a class notification from an interface */
    {
        uint8_t other[8];
        memcpy(other, conn_on, 8);
        other[0] = 0x21;
        cdc_notify_reset(&st);
        CHECK(cdc_notify_feed(&st, other, 8, 16) == 1);
        CHECK(!cdc_notify_valid(&st));
    }

    /* more data announced than the buffer holds: capped, no overrun */
    {
        uint8_t big[64];
        memset(big, 0x55, sizeof big);
        memcpy(big, speed, 8);
        big[6] = 56;
        cdc_notify_reset(&st);
        CHECK(cdc_notify_feed(&st, big, 64, 64) == 1);
        CHECK(st.len == CDC_NOTIFY_MAX);
        CHECK(cdc_notify_code(&st) == CDC_NOTIFY_SPEED_CHANGE);
    }
}

int main(void)
{
    test_parse();
    test_notify();
    if (fails) {
        printf("%d check(s) failed\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
