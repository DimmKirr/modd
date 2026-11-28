/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 *
 * LED map for the FM-1 panel: key IDs, white-key table and column/row
 * data shared by the boot init (direct GPIO) and the app (fm1_led[]).
 * See docs/hardware/LEDS.md for the confirmed map. */
#ifndef FM1_LED_MAP_H
#define FM1_LED_MAP_H

#define FM1_NOTE_KEY_FIRST  14u
#define FM1_NOTE_KEY_LAST   40u
#define FM1_NOTE_KEY_COUNT  27u
#define FM1_WHITE_KEY_COUNT 16u

/* White keys (natural notes, no sharps): LED IDs from F3 to G5.
 * Used by ota_work (cumulative progress) and the boot init (error codes). */
static const uint8_t FM1_WHITE_KEYS[FM1_WHITE_KEY_COUNT] = {
    14, 16, 18, 20, 21, 23, 25, 26, 28, 30, 32, 33, 35, 37, 38, 40
};

/* Hardware table for direct GPIO LED drive (the boot init, before any ISR).
 * [i][0] = 595 column, [i][1] = shw_led_lines / fm1__led_lines rowmask.
 * Rowmask: 0x10 PH9 (PA8/row4, keys 14-22), 0x08 PH6 (PA7/row3, keys 23-33),
 *          0x04 PA10 (PA6/row2, keys 34-40). */
static const uint8_t FM1_WHITE_KEY_HW[FM1_WHITE_KEY_COUNT][2] = {
    {3, 0x10}, {5, 0x10}, {7, 0x10}, {8, 0x10}, {9, 0x10},   /* F3 G3 A3 B3 C4 */
    {0, 0x08}, {2, 0x08}, {3, 0x08}, {5, 0x08}, {7, 0x08},   /* D4 E4 F4 G4 A4 */
    {9, 0x08}, {10, 0x08},                                     /* B4 C5 */
    {1, 0x04}, {3, 0x04}, {4, 0x04}, {5, 0x04},               /* D5 E5 F5 G5 */
};

#endif
