/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE adapter: stubs when the BLE module is off. */
#ifndef MODD_BLE_H
#define MODD_BLE_H
#include <stdint.h>

#ifndef MODD_BLE_MIDI
static int ble_midi_ready(void) { return 0; }
static int ble_connected(void) { return 0; }
static void ble_midi_out(uint32_t msg) { (void)msg; }
static void ble_midi_poll(void) {}
static void ble_bond_poll(void) {}
#endif

#endif /* MODD_BLE_H */
