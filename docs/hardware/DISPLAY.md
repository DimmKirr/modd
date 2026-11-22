# FM-1 Display

## Panel

| Spec | Value |
|------|-------|
| Type | 1.54" square IPS TFT |
| Controller | ST7789V (Sitronix) |
| Resolution | 240 x 240 pixels |
| Active area | 27.72 x 27.72 mm |
| Pixel density | 219 PPI |
| Display mode | Transmissive, normally black |
| Viewing angle | ~80 degrees all directions (IPS) |
| Contrast ratio | ~800:1 typical |
| Brightness | ~300 cd/m2 typical |

## Color

| Spec | Value |
|------|-------|
| Color depth | RGB565 (16-bit) |
| Total colors | 65,536 (5R x 6G x 5B = 32 x 64 x 32) |
| Byte order on wire | Big-endian (firmware byte-swaps) |
| GRAM depth | 18-bit internal (only 16 used via COLMOD 0x55) |

RGB565 bit layout (MSB first):

```
bit  15 14 13 12 11  10  9  8  7  6  5  4  3  2  1  0
      R  R  R  R  R   G  G  G  G  G  G  B  B  B  B  B
```

Green gets 6 bits because human vision is most sensitive to green.

## Interface

| Signal | SoC pin | GPIO | Function |
|--------|---------|------|----------|
| CLK | PC9 | Port C bit 9 | SPI1 clock |
| MOSI | PC10 | Port C bit 10 | SPI1 data out |
| CS | PC7 | Port C bit 7 | Chip select (active low) |
| D/C | PC8 | Port C bit 8 | Data/command select |
| BL | PA2 | Port A bit 2 | Backlight (active low) |
| RST | - | via FPC | Hardware reset |

SPI1 clock: `lsb_clk / (BAUD + 1)`. Default BAUD=4 gives ~12 MHz.
ST7789V max SPI write: ~15 MHz (66 ns cycle per datasheet).

## Backlight

PA2 is shared with the 74HC595 shift register /OE (keyboard matrix column driver). This means:

- **PA2 low**: backlight ON, key matrix enabled (normal operation)
- **PA2 high**: backlight OFF, key matrix disabled (no input scanning)

Firmware cannot turn the backlight off independently of the key matrix. The only "dim" path is the ST7789V sleep sequence (DISPOFF + SLPIN), which blanks the panel while the backlight LED stays on.

ST7789V has built-in brightness registers (WRDISBV 0x51, WRCTRLD 0x53) for 0-255 PWM levels, but these only work if the module routes the controller's CABC output to the backlight LEDs. The FM-1's module does not wire this: it uses a separate BL pin.

**Conclusion**: no software brightness control. Binary on/off only, gated by input scanning.

## Init sequence

From `firmware/src/ui/shells/melodee/lcd.c`:

| Step | Command | Arg | Purpose |
|------|---------|-----|---------|
| 1 | SWRESET (0x01) | - | Software reset |
| 2 | wait | 150 ms | Reset settle |
| 3 | SLPOUT (0x11) | - | Exit sleep |
| 4 | wait | 120 ms | Wake settle |
| 5 | COLMOD (0x3A) | 0x55 | RGB565 mode |
| 6 | MADCTL (0x36) | 0x00 | Top-left origin, RGB order |
| 7 | INVON (0x21) | - | Inversion on (required for IPS) |
| 8 | NORON (0x13) | - | Normal display mode |
| 9 | clear to black | - | Fill 240x240 with 0x0000 |
| 10 | DISPON (0x29) | - | Display on |

## Refresh rate

At 12 MHz SPI, a full 240x240 RGB565 frame = 240 x 240 x 2 bytes = 115,200 bytes.

- Theoretical max: 12,000,000 / (115,200 x 8) = ~13 full frames/s
- Practical: higher, because firmware uses partial updates (dirty rectangles via `cv_blit`), not full-frame redraws. The canvas system blits header (240x30), cards (57x48 each), panel (240x124), and footer (240x28) independently.
- The ST7789V's internal GRAM allows tearing-free partial updates.

## Power states

| State | Commands | Backlight | GRAM | Input |
|-------|----------|-----------|------|-------|
| Normal | DISPON | ON | Active | Active |
| Screen off | DISPOFF, SLPIN | ON (PA2 shared) | Retained | Active |
| Wake | SLPOUT (120ms), DISPON | ON | Redrawn | Active |

## Sources

- ST7789V datasheet (Sitronix)
- `firmware/hal/fm1_lcd_hw.h` (pin mapping, SPI registers)
- `firmware/src/ui/shells/melodee/lcd.c` (protocol driver)
- `docs/hardware/board/fm1_Connectors.kicad_sch` (FPC connector)
- `docs/hardware/board/fm1_SoC.kicad_sch` (SPI1 pin mapping)
