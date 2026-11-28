// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Host test of the WPA2-PSK 4-way handshake in firmware/src/wifi/mac/wpa.c,
 * with this file playing the access point: it derives its own PTK from the
 * SNonce of a message 2 and builds message 3 the way an AP does.
 *
 * Covers what broke on hardware on 2026-11-15 ("wpa msg3 mic FAIL", no IP):
 *   - message 1 arriving more than once before message 3
 *   - bytes after the EAPOL frame in the received buffer (an FCS, padding) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CONFIG_WIFI_WPA 1

static uint32_t fake_ticks;
static uint32_t fm1_ticks(void) { return fake_ticks += 0x9E3779B9u; }
static void modd_log(const char *m) { (void)m; }
static void modd_log_int(const char *p, int32_t v) { (void)p; (void)v; }
static uint8_t wifi_sta_wpa_pending;

#include "wifi/mac/wpa.c"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const uint8_t STA[6] = { 0xD8, 0xC8, 0x0C, 0x11, 0x22, 0x33 };
static const uint8_t AP[6]  = { 0x02, 0x46, 0x4D, 0x31, 0x00, 0x01 };

/* ---- the access point's side, written from the standard, not shared with the firmware ---- */

/* PRF-384(PMK, "Pairwise key expansion", min(AA,SPA) || max(AA,SPA) || min(nonces) || max(nonces)) */
static void ap_derive_ptk(const uint8_t pmk[32], const uint8_t *anonce, const uint8_t *snonce,
                          uint8_t ptk[48])
{
    static const char label[] = "Pairwise key expansion";
    uint8_t in[22 + 1 + 76 + 1], digest[20];
    const uint8_t *lo_mac = memcmp(AP, STA, 6) < 0 ? AP : STA;
    const uint8_t *hi_mac = lo_mac == AP ? STA : AP;
    const uint8_t *lo_n = memcmp(anonce, snonce, 32) < 0 ? anonce : snonce;
    const uint8_t *hi_n = lo_n == anonce ? snonce : anonce;
    uint32_t got = 0;
    uint8_t counter = 0;

    memcpy(in, label, 22);
    in[22] = 0;
    memcpy(in + 23, lo_mac, 6);
    memcpy(in + 29, hi_mac, 6);
    memcpy(in + 35, lo_n, 32);
    memcpy(in + 67, hi_n, 32);
    while (got < 48) {
        uint32_t n = 48 - got < 20 ? 48 - got : 20;
        in[99] = counter++;
        wifi_hmac_sha1(pmk, 32, in, sizeof in, digest);
        memcpy(ptk + got, digest, n);
        got += n;
    }
}

static void ap_mic(const uint8_t kck[16], uint8_t *eapol, uint32_t len)
{
    uint8_t digest[20];
    memset(eapol + 81, 0, 16);
    wifi_hmac_sha1(kck, 16, eapol, len, digest);
    memcpy(eapol + 81, digest, 16);
}

static int ap_mic_ok(const uint8_t kck[16], const uint8_t *eapol, uint32_t len)
{
    uint8_t copy[512], digest[20];
    memcpy(copy, eapol, len);
    memset(copy + 81, 0, 16);
    wifi_hmac_sha1(kck, 16, copy, len, digest);
    return memcmp(digest, eapol + 81, 16) == 0;
}

/* EAPOL-Key frame: 4-byte 802.1X header + 95-byte key descriptor + key data */
static uint32_t ap_frame(uint8_t *f, uint16_t key_info, uint8_t replay, const uint8_t *anonce,
                         uint32_t key_data_len)
{
    uint32_t body = 95 + key_data_len, i;
    memset(f, 0, 4 + body);
    f[0] = 2; f[1] = 3; f[2] = (uint8_t)(body >> 8); f[3] = (uint8_t)body;
    f[4] = 2;
    f[5] = (uint8_t)(key_info >> 8); f[6] = (uint8_t)key_info;
    f[8] = 16;
    f[16] = replay;
    memcpy(f + 17, anonce, 32);
    f[97] = (uint8_t)(key_data_len >> 8); f[98] = (uint8_t)key_data_len;
    for (i = 0; i < key_data_len; i++) f[99 + i] = (uint8_t)(0xA5 ^ i);   /* stands in for the wrapped GTK */
    return 4 + body;
}

#define KI_MSG1 0x008A   /* HMAC-SHA1/AES, pairwise, ACK */
#define KI_MSG3 0x13CA   /* + install, MIC, secure, encrypted key data */

static void start(void)
{
    wifi_wpa_init((const uint8_t *)"password", 8, (const uint8_t *)"IEEE", 4, STA, AP);
    wifi_sta_wpa_pending = 0;
}

static void fill(uint8_t *n, uint8_t seed) { for (int i = 0; i < 32; i++) n[i] = (uint8_t)(seed + 7 * i); }

/* IEEE 802.11i-2004 H.4.3: passphrase "password", SSID "IEEE" */
static void test_pmk_vector(void)
{
    static const uint8_t want[32] = {
        0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef, 0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a, 0x5f, 0x90,
        0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2, 0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e };
    start();
    CHECK(memcmp(wifi_wpa.pmk, want, 32) == 0);
}

static void test_handshake(int msg1_copies, uint32_t tail)
{
    uint8_t anonce[32], rx[512], tx[256], snonce1[32], ptk_ap[48];
    uint32_t n, tx_len = 0;
    int i;

    start();
    fill(anonce, 0x11);

    /* message 1, possibly several times: the AP derives its PTK from the FIRST reply */
    for (i = 0; i < msg1_copies; i++) {
        n = ap_frame(rx, KI_MSG1, (uint8_t)(1 + i), anonce, 0);
        CHECK(wifi_wpa_rx_eapol(rx, n, tx, sizeof tx, &tx_len) == 0);
        CHECK(tx_len == 121);
        CHECK(tx[16] == 1 + i);                          /* replay counter echoed */
        if (i == 0) {
            memcpy(snonce1, tx + 17, 32);
            ap_derive_ptk(wifi_wpa.pmk, anonce, snonce1, ptk_ap);
        }
        CHECK(memcmp(tx + 17, snonce1, 32) == 0);         /* same SNonce on every copy */
        CHECK(ap_mic_ok(ptk_ap, tx, tx_len));             /* so every message 2 verifies at the AP */
    }
    CHECK(wifi_wpa.tk_valid == 0);

    /* message 3, followed by `tail` bytes that are not part of the EAPOL frame */
    n = ap_frame(rx, KI_MSG3, 9, anonce, 56);
    ap_mic(ptk_ap, rx, n);
    memset(rx + n, 0xC3, tail);
    tx_len = 0;
    CHECK(wifi_wpa_rx_eapol(rx, n + tail, tx, sizeof tx, &tx_len) == 0);
    CHECK(wifi_wpa.tk_valid == 1);
    CHECK(wifi_sta_wpa_pending == 1);
    CHECK(tx_len == 99);                                  /* message 4 */
    CHECK(tx[16] == 9);
    CHECK(ap_mic_ok(ptk_ap, tx, tx_len));
    CHECK(memcmp(wifi_wpa.ptk + 32, ptk_ap + 32, 16) == 0);   /* both ends hold the same TK */
}

static void test_rejects(void)
{
    uint8_t anonce[32], other[32], rx[512], tx[256], snonce1[32], ptk_ap[48];
    uint32_t n, tx_len = 0;

    /* a message 3 whose MIC is wrong installs nothing */
    start();
    fill(anonce, 0x21);
    n = ap_frame(rx, KI_MSG1, 1, anonce, 0);
    CHECK(wifi_wpa_rx_eapol(rx, n, tx, sizeof tx, &tx_len) == 0);
    memcpy(snonce1, tx + 17, 32);
    ap_derive_ptk(wifi_wpa.pmk, anonce, snonce1, ptk_ap);
    n = ap_frame(rx, KI_MSG3, 2, anonce, 56);
    ap_mic(ptk_ap, rx, n);
    rx[81] ^= 1;
    CHECK(wifi_wpa_rx_eapol(rx, n, tx, sizeof tx, &tx_len) != 0);
    CHECK(wifi_wpa.tk_valid == 0 && tx_len == 0);

    /* a frame that says it is longer than what arrived is not parsed */
    n = ap_frame(rx, KI_MSG3, 3, anonce, 56);
    ap_mic(ptk_ap, rx, n);
    CHECK(wifi_wpa_rx_eapol(rx, n - 8, tx, sizeof tx, &tx_len) != 0);
    CHECK(wifi_wpa.tk_valid == 0);

    /* the good one still completes afterwards: the failures above changed no keys */
    n = ap_frame(rx, KI_MSG3, 4, anonce, 56);
    ap_mic(ptk_ap, rx, n);
    CHECK(wifi_wpa_rx_eapol(rx, n, tx, sizeof tx, &tx_len) == 0);
    CHECK(wifi_wpa.tk_valid == 1);

    /* a new ANonce is a new handshake: a new SNonce, mid-handshake or after one */
    start();
    fill(anonce, 0x31);
    fill(other, 0x77);
    n = ap_frame(rx, KI_MSG1, 1, anonce, 0);
    CHECK(wifi_wpa_rx_eapol(rx, n, tx, sizeof tx, &tx_len) == 0);
    memcpy(snonce1, tx + 17, 32);
    n = ap_frame(rx, KI_MSG1, 2, other, 0);
    CHECK(wifi_wpa_rx_eapol(rx, n, tx, sizeof tx, &tx_len) == 0);
    CHECK(memcmp(tx + 17, snonce1, 32) != 0);
    ap_derive_ptk(wifi_wpa.pmk, other, tx + 17, ptk_ap);
    CHECK(ap_mic_ok(ptk_ap, tx, tx_len));
}

int main(void)
{
    test_pmk_vector();
    test_handshake(1, 0);     /* the plain case, as the emulator's AP does it */
    test_handshake(2, 0);     /* 2026-11-15 log, 72 s: message 1 twice, 4 ms apart */
    test_handshake(5, 0);     /* same log, 176 s: five times in 9 ms */
    test_handshake(1, 4);     /* a 4-byte FCS left after the frame */
    test_handshake(5, 4);
    test_rejects();
    if (fails) {
        printf("%d check(s) failed\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
