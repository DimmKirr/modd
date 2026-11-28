/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Wrapper for the upstream Optimist BLE scanner (CONFIG_BLE, BLE_CENTRAL).
 * Origin: w0ts/optimist firmware/src/ble/ble_scan.c @ v0.4 (7184726b)
 * The upstream file is #included unchanged (through the patched copy from
 * patches/optimist/0001-ble-scan-hid-hooks.patch); this wrapper defines the
 * hook macros that add HID service detection when MODD_BLE_HID is set.
 * Fixes: upstream's ble_ad_parse and ble_name_is_fm1 are restored (the fork
 * had removed them, leaving a latent undefined-symbol bug). */

/* --- HID hook macros (before the upstream include) --- */
#ifdef MODD_BLE_HID

enum { BLE_KIND_HID = 3 };

struct ble_ad_ext {
    struct ble_ad base;
    uint8_t hid;
    uint8_t has_appearance;
    uint16_t appearance;
};

static void ble_ad_parse_ext(const uint8_t *ad, uint32_t n, struct ble_ad_ext *ext);

#define BLE_SCAN_HID_VARS \
    struct ble_ad_ext adx; int is_hid = 0;

#define BLE_SCAN_HID_AFTER_PARSE(ad_data, ad_len) \
    ble_ad_parse_ext(ad_data, ad_len, &adx); is_hid = adx.hid;

#define BLE_SCAN_HID_IS_LISTED(ad_midi) ((ad_midi) || is_hid)

#define BLE_SCAN_HID_KIND(e, kind) \
    if (!((e)->midi && (e)->kind == BLE_KIND_HID)) (e)->kind = (kind)

#define BLE_SCAN_HID_AFTER_LISTED(t, e) \
    if (is_hid && !(e)->midi) { (e)->kind = BLE_KIND_HID; sc_listed(t, e); }

#endif /* MODD_BLE_HID */

/* --- upstream (patched copy: hooks default to no-ops when macros are not set) --- */
#include "upstream/optimist/firmware/src/ble/ble_scan.c"

/* --- HID extended parser (after upstream, so ble_ad_parse and helpers are available) --- */
#ifdef MODD_BLE_HID
static void ble_ad_parse_ext(const uint8_t *ad, uint32_t n, struct ble_ad_ext *ext)
{
    ble_zero((uint8_t *)ext, sizeof *ext);
    ble_ad_parse(ad, n, &ext->base);
    uint32_t i = 0;
    while (i < n) {
        uint32_t len = ad[i], k;
        const uint8_t *d = ad + i + 2;
        if (!len || i + 1u + len > n)
            break;
        switch (ad[i + 1]) {
        case 0x02:
        case 0x03:
            for (k = 0; k + 2u <= len - 1u; k += 2u)
                if (d[k] == 0x12u && d[k + 1u] == 0x18u)
                    ext->hid = 1;
            break;
        case 0x19:
            if (len >= 3u) {
                ext->appearance = ble_rd16(d);
                ext->has_appearance = 1;
            }
            break;
        default:
            break;
        }
        i += 1u + len;
    }
}
#endif
