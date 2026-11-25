/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 the Optimist contributors (w0ts/optimist)
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: w0ts/optimist firmware/src/ble/ble_att.c @ v0.4 (7184726b)
 * Wrapper for the upstream Optimist ATT server with File Transfer GATT extension.
 * The upstream file is #included unchanged (through the patched copy from
 * patches/optimist/0002-ble-att-ft-hooks.patch); this wrapper defines the
 * hook macros that add the FT service handles and BLE diagnostics. */
#include "ble_diag.h"
#include "ble_ft.h"

/* --- FT GATT handle enum extension --- */
#define ATT_HANDLES_TAIL \
    H_FT, H_FT_LIST_D, H_FT_LIST, H_FT_CTL_D, H_FT_CTL, \
    H_FT_RX_D, H_FT_RX, H_FT_TX_D, H_FT_TX, H_FT_TX_CCC, \
    H_LAST = H_FT_TX_CCC

/* --- FT characteristic declarations: properties + value handle + 128-bit UUID --- */
#define ATT_EXTRA_DECLS \
static const uint8_t V_FT_LIST_D[] = {0x02, H_FT_LIST, 0, \
                                      0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A, \
                                      0x5E,0x4D,0x2C,0x1B,0x02,0xFA,0xB3,0xE4}; \
static const uint8_t V_FT_CTL_D[]  = {0x08, H_FT_CTL, 0, \
                                      0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A, \
                                      0x5E,0x4D,0x2C,0x1B,0x03,0xFA,0xB3,0xE4}; \
static const uint8_t V_FT_RX_D[]   = {0x04, H_FT_RX, 0, \
                                      0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A, \
                                      0x5E,0x4D,0x2C,0x1B,0x04,0xFA,0xB3,0xE4}; \
static const uint8_t V_FT_TX_D[]   = {0x10, H_FT_TX, 0, \
                                      0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A, \
                                      0x5E,0x4D,0x2C,0x1B,0x05,0xFA,0xB3,0xE4};

/* --- FT ATT database entries --- */
#define ATT_EXTRA_DB \
    {0x2800, A_R | A_SVC, 16, BLE_UUID_FT_SVC}, \
    {0x2803, A_R, 19, V_FT_LIST_D}, \
    {0, A_R | A_128, 0, 0}, \
    {0x2803, A_R, 19, V_FT_CTL_D}, \
    {0, A_W | A_128, 0, 0}, \
    {0x2803, A_R, 19, V_FT_RX_D}, \
    {0, A_W | A_128, 0, 0}, \
    {0x2803, A_R, 19, V_FT_TX_D}, \
    {0, A_128, 0, 0}, \
    {0x2902, A_R | A_W, 2, 0},

/* --- FT state in the batt struct --- */
#define ATT_EXTRA_STATE \
    uint8_t ft_list_buf[FT_LIST_MAX]; \
    uint8_t ft_list_len;

/* --- FT init in ble_att_reset --- */
#define ATT_RESET_HOOK() \
    ble_ft_init(); \
    batt.ft_list_len = ble_ft_list(batt.ft_list_buf, FT_LIST_MAX);

/* --- FT attribute length dispatch --- */
#define ATT_LEN_HOOK(h, v) \
    if ((h) == H_FT_LIST) { \
        *(v) = batt.ft_list_buf; \
        return batt.ft_list_len; \
    } \
    if ((h) == H_FT_TX_CCC) { \
        batt.dyn[0] = ble_ft_ccc_get(); \
        batt.dyn[1] = 0; \
        *(v) = batt.dyn; \
        return 2; \
    }

/* --- 128-bit UUID lookup (MIDI + FT handles) --- */
static const uint8_t *att_128uuid_ft(uint16_t h);
#define ATT_128UUID(h) att_128uuid_ft(h)

/* --- FT write dispatch --- */
#define ATT_WRITE_HOOK(h, v, n) \
    if ((h) == H_FT_CTL) { \
        ble_ft_on_ctl((v), (n)); \
        return 0; \
    } \
    if ((h) == H_FT_RX) { \
        ble_ft_on_data((v), (n)); \
        return 0; \
    } \
    if ((h) == H_FT_TX_CCC) { \
        if ((n) != 2u) \
            return ATT_ERR_VAL_LEN; \
        ble_ft_ccc_set((v)[0]); \
        return 0; \
    }

/* --- FT notification check --- */
static void att_ft_notify(void);
#define ATT_EVENT_HOOK() att_ft_notify();

/* --- upstream (patched copy: hooks default to no-ops when macros are not set) --- */
#include "upstream/optimist/firmware/src/ble/ble_att.c"

/* --- post-include definitions (need upstream's types and functions) --- */

static const uint8_t *att_128uuid_ft(uint16_t h)
{
    if (h == H_MIDI_IO)     return ATT_UUID_MIDI_IO;
    if (h == H_FT_LIST)     return BLE_UUID_FT_LIST;
    if (h == H_FT_CTL)      return BLE_UUID_FT_CTL;
    if (h == H_FT_RX)       return BLE_UUID_FT_RX;
    if (h == H_FT_TX)       return BLE_UUID_FT_TX;
    return 0;
}

static void att_ft_notify(void)
{
    extern uint8_t bft_pending_rsp;
    if (!(ble_ft_ccc_get() & 1u))
        return;
    if (!ble_ll_connected())
        return;
}
