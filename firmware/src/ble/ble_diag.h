/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal BLE diagnostics stub: provides BLE_DG() used by ble_hid.c.
 * The full ble_diag.h was removed from Optimist v0.4; modd only needs the
 * recording macro, which compiles to nothing when BLE_DIAG is 0. */
#ifndef BLE_DIAG_H
#define BLE_DIAG_H

#ifndef BLE_DIAG
#define BLE_DIAG 0
#endif

#if BLE_DIAG
#define BLE_DG(...) __VA_ARGS__
#else
#define BLE_DG(...) ((void)0)
#endif

#endif
