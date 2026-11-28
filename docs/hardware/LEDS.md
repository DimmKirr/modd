# FM-1 LED Map

Confirmed by hardware testing via `GET /api/led/{id}` (port 9222), 2026-11-27.

## Buttons (ID 0-13)

| ID | Button | Physical row | Column |
|----|--------|-------------|--------|
| 0 | OCT- | PA8 (row 4) | 0 |
| 1 | OCT+ | PA8 (row 4) | 1 |
| 2 | FX | PA5 (row 1) | 6 |
| 3 | SEL | PA5 (row 1) | 4 |
| 4 | ENV | PA5 (row 1) | 2 |
| 5 | LFO | PA5 (row 1) | 0 |
| 6 | EDIT | PA6 (row 2) | 9 |
| 7 | GLO | PA6 (row 2) | 8 |
| 8 | HOME | PA5 (row 1) | 7 |
| 9 | SAVE | PA5 (row 1) | 5 |
| 10 | ARP | PA5 (row 1) | 3 |
| 11 | SEQ | PA5 (row 1) | 1 |
| 12 | PLAY/STOP (red) | PA6 (row 2) | 10 |
| 13 | REC | PA6 (row 2) | 7 |

PLAY has a second green LED (`LED_PLAY_GREEN` in `firmware/src/ui/shells/modd/leds.c`)
on a separate pin, not in the key matrix and not addressable via `/api/led/{id}`.

## Keyboard Keys (ID 14-40)

27 chromatic keys, F3 to G5.  `fm1_input.h` line 97: "bit n = note key n
(0 = F3 .. 26 = G5)"; LED ID = note_key + 14.

### Chromatic layout

| LED ID | Note | Octave | Physical row | Column |
|--------|------|--------|-------------|--------|
| 14 | F | 3 | PA8 (row 4) | 3 |
| 15 | F# | 3 | PA8 (row 4) | 2 |
| 16 | G | 3 | PA8 (row 4) | 5 |
| 17 | G# | 3 | PA8 (row 4) | 4 |
| 18 | A | 3 | PA8 (row 4) | 7 |
| 19 | A# | 3 | PA8 (row 4) | 6 |
| 20 | B | 3 | PA8 (row 4) | 8 |
| 21 | C | 4 | PA8 (row 4) | 9 |
| 22 | C# | 4 | PA8 (row 4) | 10 |
| 23 | D | 4 | PA7 (row 3) | 0 |
| 24 | D# | 4 | PA7 (row 3) | 1 |
| 25 | E | 4 | PA7 (row 3) | 2 |
| 26 | F | 4 | PA7 (row 3) | 3 |
| 27 | F# | 4 | PA7 (row 3) | 4 |
| 28 | G | 4 | PA7 (row 3) | 5 |
| 29 | G# | 4 | PA7 (row 3) | 6 |
| 30 | A | 4 | PA7 (row 3) | 7 |
| 31 | A# | 4 | PA7 (row 3) | 8 |
| 32 | B | 4 | PA7 (row 3) | 9 |
| 33 | C | 5 | PA7 (row 3) | 10 |
| 34 | C# | 5 | PA6 (row 2) | 0 |
| 35 | D | 5 | PA6 (row 2) | 1 |
| 36 | D# | 5 | PA6 (row 2) | 2 |
| 37 | E | 5 | PA6 (row 2) | 3 |
| 38 | F | 5 | PA6 (row 2) | 4 |
| 39 | F# | 5 | PA6 (row 2) | 6 |
| 40 | G | 5 | PA6 (row 2) | 5 |

Note: in `FM1_KEYMAP` row 2, column 5 holds ID 40 (G5) and column 6 holds
ID 39 (F#5) — reversed from the ascending ID order.

### White keys only (natural notes, no sharps/flats)

16 keys for diagnostic signalling:

| Index | LED ID | Note | Row | Column | Rowmask |
|-------|--------|------|-----|--------|---------|
| 0 | 14 | F3 | 4 | 3 | 0x10 |
| 1 | 16 | G3 | 4 | 5 | 0x10 |
| 2 | 18 | A3 | 4 | 7 | 0x10 |
| 3 | 20 | B3 | 4 | 8 | 0x10 |
| 4 | 21 | C4 | 4 | 9 | 0x10 |
| 5 | 23 | D4 | 3 | 0 | 0x08 |
| 6 | 25 | E4 | 3 | 2 | 0x08 |
| 7 | 26 | F4 | 3 | 3 | 0x08 |
| 8 | 28 | G4 | 3 | 5 | 0x08 |
| 9 | 30 | A4 | 3 | 7 | 0x08 |
| 10 | 32 | B4 | 3 | 9 | 0x08 |
| 11 | 33 | C5 | 3 | 10 | 0x08 |
| 12 | 35 | D5 | 2 | 1 | 0x04 |
| 13 | 37 | E5 | 2 | 3 | 0x04 |
| 14 | 38 | F5 | 2 | 4 | 0x04 |
| 15 | 40 | G5 | 2 | 5 | 0x04 |

Rowmask values for `shw_led_lines` / `fm1__led_lines`:
- 0x04 (bit 2): PA10 — drives PA6 row keys (IDs 34-40, row 2 in KEYMAP)
- 0x08 (bit 3): PH6 — drives PA7 row keys (IDs 23-33, row 3 in KEYMAP)
- 0x10 (bit 4): PH9 — drives PA8 row keys (IDs 14-22, row 4 in KEYMAP)

## Animations

With `CONFIG_LED_ANIM_FELUCCA` the panel LEDs play Felucca's two animations (pictures: upstream `fm1_led_anim.h`; frame driver: `firmware/ports/felucca/leds/`).

- Power-on sweep (`CONFIG_LED_ANIM_BOOT`): a light runs over the keys from left to right, then the buttons swell and settle. About 0.7 s. A key or button press ends it.
- Idle animation (`CONFIG_LED_ANIM_IDLE`, after `CONFIG_LED_ANIM_IDLE_MS`, default 60000 ms without input): a soft light drifts over the keys and back, the buttons breathe. It runs only while the transport is stopped. Any key, button or knob ends it.

Each LED has a level of 0..64 lit frames out of 64, drawn by a per-LED sigma-delta, one step per scan frame (1.1 ms). The glow (dim plane 0 at duty 1/32) shows while the lights level is not OFF. While an animation runs, the shell's `ui_leds()` does not write the LEDs.

## API

```
GET http://<fm1-ip>:9222/api/led/{id}
```

- `id = -1`: clear LED override, return to normal shell control
- `id = 0..13`: light one button LED
- `id = 14..40`: light one keyboard key LED

The handler (`api_led_test` in `firmware/src/rpc/api_ctl.c`) sets `api_led_override`
so the shell's `ui_leds()` does not touch the LEDs while testing.

## Hardware

- 11 columns via two 74HC595 shift registers (active low, directly from the 595 chain)
- 4 LED rows driven by GPIO (active high): PH6, PH9, PA9, PA10
- Column select: `fm1__sr_bits` (data PA4, clock PA3, latch PA1)
- LED blanking: 40 us per column (`FM1_LED_US`)
- PWM mode: MCPWM1/MCPWM3 for per-row brightness (`fm1_led_pwm.h`)
