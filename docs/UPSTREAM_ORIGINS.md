# Upstream origins

Rule (docs/PORTING.md, "The rule: upstream code, through an adapter, into modd"):

- "Never copy or rewrite upstream code unless duplication / forking is approved by the user."
- "The work of porting is writing the adapter, and nothing else."

Every file in `firmware/src`, `firmware/adapters` and `firmware/hal` that carries an `Origin:` line in its first 40 lines has one row here. `tests/build/upstream_origin_test.py` reads this table and fails when a file with an `Origin:` line has no row, when a row names a file that no longer exists, or when a row has a class or status the test does not know. New debt is blocked; the debt listed here stays visible until each fork is replaced by an include.

## Classes

| Class | Meaning | Check |
|-------|---------|-------|
| WRAPPER | a thin `<name>_modd.c` that `#include`s the upstream file unchanged | the file contains an `#include "upstream/` line |
| HEADER-DERIVED | an adapter or HAL header (or HAL assembly) derived from an upstream header, or a project config header for a vendored library | none |
| PATCHED | a patch in `patches/<upstream>/`: a hook (an `#ifndef` around a constant) in an upstream file, applied by the build to a copy under `build/<defconfig>/gen/patched/`; `firmware/upstream/` stays pristine | the patch has `Approved-by` and `Forwarded` header fields and applies to the commit in `vendir.lock.yml` (`task vendor:patches:check`) |
| FORK | a copy, reduction or rewrite of upstream code in `firmware/src`. The note starts with COPIED, REDUCED or REWRITTEN | status must be one of the three forms below |

Status of a PATCHED row: `approved: <who>, <date>, <reason>`, naming the upstream files by name. A patch adds a hook and the value comes from Kconfig; anything that changes behaviour needs the owner's approval again. See `patches/README.md`.

Status of a FORK:

- `approved: <who>, <date>, <reason>`
- `not approved: to be replaced by an include`
- `pre-rule (before 2026-11-23): not reviewed`: the fork is older than the rule; nobody has decided whether it stays. Not an approval. Never use it for a file added on or after 2026-11-23.

No fork has been approved by the owner so far. Do not write `approved:` without the owner's words. Files that include upstream directly from `engines.c` (the Felucca-Engines files and `eng_grain.c`) are INCLUDED and have no row, because they carry no `Origin:` line. `firmware/src/audio/engines/eng_sample_modd.c` is a wrapper without an `Origin:` line, so the test does not list it.

## Table

| path | class | upstream file | status | note |
|---|---|---|---|---|
| `firmware/src/audio/chord.c` | FORK | firmware/upstream/felucca/firmware/src/chord.c | not approved: to be replaced by an include | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/audio/core/audio.c` | FORK | keremimo/melodee firmware/src/audio.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/audio/core/felucca/stack_modd.c` | WRAPPER | hugelton/Felucca firmware/src/{params,mod,voice,slicer,fx,perform,click,audio}.c | n/a | upstream files included unchanged (CONFIG_AUDIO_FELUCCA_STACK_INCLUDED); the file also holds adapter glue |
| `firmware/src/audio/effects/felucca/fx.c` | FORK | hugelton/Felucca firmware/src/fx.c | not approved: to be replaced by an include | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/audio/effects/host/fx_unit_exgain_modd.c` | WRAPPER | w0ts/optimist firmware/src/fx/insfx/exgain.c | n/a | upstream file included unchanged |
| `firmware/src/audio/effects/host/dsp_optimist_modd.c` | WRAPPER | w0ts/optimist firmware/src/dsp/dsp.c | n/a | upstream file included unchanged; macros rename the 17 names it shares with Felucca-Engines dsp.c for the span of the include |
| `firmware/src/audio/engines/eng_adpcm_modd.c` | WRAPPER | hugelton/Felucca firmware/src/eng_adpcm.c | n/a | upstream file included unchanged |
| `firmware/src/audio/engines/eng_sample_modd.c` | WRAPPER | hugelton/Felucca firmware/src/eng_sample.c | n/a | upstream file included unchanged |
| `firmware/src/audio/engines/eng_slice_modd.c` | WRAPPER | hugelton/Felucca firmware/src/eng_slice.c | n/a | upstream file included unchanged |
| `firmware/src/audio/mod.c` | FORK | firmware/upstream/felucca/firmware/src/mod.c | not approved: to be replaced by an include | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/audio/voice.c` | FORK | isod89/sloop-fm1 firmware/src/voice.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ble/ble_att_ft.c` | FORK | w0ts/optimist firmware/src/ble/ble_att.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ble/ble_hid.c` | FORK | w0ts/optimist firmware/src/ble/ble_gattc.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (inferred from the header comment) |
| `firmware/src/ble/ble_hw_stub_sim.c` | FORK | w0ts/optimist firmware/src/ble/ble_hw_stub.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ble/ble_scan.c` | FORK | w0ts/optimist firmware/src/ble/ble_scan.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ble/ble_scan.h` | HEADER-DERIVED | w0ts/optimist firmware/src/ble/ble_scan.h | n/a | header or HAL source derived from the upstream file |
| `firmware/src/boot/boot_work.c` | FORK | keremimo/melodee firmware/src/ui_boot.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/boot/ota.c` | FORK | keremimo/melodee firmware/src/ota.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/debug/debug_dap.c` | FORK | joakimeriksson/fm1-dock firmware/src/dock.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/dock/dock.c` | FORK | joakimeriksson/fm1-dock firmware/src/dock.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/libc.c` | FORK | keremimo/melodee firmware/src/libc.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/midi/melodee/midi_routing.c` | FORK | keremimo/melodee firmware/src/midi_control.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/midi/usb.c` | FORK | keremimo/melodee firmware/src/usb.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/modd.c` | FORK | keremimo/melodee firmware/src/main.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/net/arch/cc.h` | HEADER-DERIVED | lwip-tcpip/lwip src/include/lwip/arch.h | n/a | header or HAL source derived from the upstream file |
| `firmware/src/net/lwipopts.h` | HEADER-DERIVED | lwip-tcpip/lwip contrib/examples/example_app/test_configs/opt_nosys_ipv4.h | n/a | header or HAL source derived from the upstream file |
| `firmware/src/seq/melodee/seq.c` | FORK | keremimo/melodee firmware/src/seq.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/seq/seq.c` | FORK | hugelton/Felucca firmware/src/seq.c | not approved: to be replaced by an include | REDUCED |
| `firmware/src/settings/settings_fmlab.c` | FORK | jsmith13/fm_lab firmware/src/platform.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/udisk/diskio.h` | HEADER-DERIVED | FatFs R0.15 source/diskio.h, https://elm-chan.org/fsw/ff/arc/ff15.zip (pinned in vendir.yml) | n/a | header or HAL source derived from the upstream file |
| `firmware/src/udisk/ffconf.h` | HEADER-DERIVED | FatFs R0.15 source/ffconf.h, https://elm-chan.org/fsw/ff/arc/ff15.zip (pinned in vendir.yml) | n/a | header or HAL source derived from the upstream file |
| `firmware/src/udisk/usb_host_xfer.c` | FORK | keremimo/melodee firmware/src/usb_host.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/udisk/usb_hub.c` | FORK | keremimo/melodee firmware/src/usb_host.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ui/shells/felucca/draw.c` | FORK | hugelton/Felucca firmware/src/ui_draw.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ui/shells/felucca/fui_icons.c` | FORK | hugelton/Felucca firmware/src/icons.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ui/shells/felucca/lvgl_palette.c` | FORK | firmware/upstream/felucca/build/gen/ui_palettes.h | not approved: to be replaced by an include | REWRITTEN (inferred from the header comment) |
| `firmware/src/ui/shells/felucca/lvgl_shell.c` | FORK | upstream Felucca ui_input.c + ui_layer.c + ui.c | not approved: to be replaced by an include | REWRITTEN (inferred from the header comment) |
| `firmware/src/ui/shells/felucca/ui.c` | FORK | hugelton/Felucca firmware/src/ui.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ui/shells/melodee/gfx.c` | FORK | keremimo/melodee firmware/src/gfx.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/ui/shells/melodee/lcd.c` | FORK | keremimo/melodee firmware/src/lcd.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/wifi/nonfree/nf_main.c` | FORK | charlesvestal/fm1-base device/radio.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/wifi/nonfree/nf_wifi_station.c` | FORK | charlesvestal/fm1-base device/wifi.c | pre-rule (before 2026-11-23): not reviewed | COPIED (edited copy; REDUCED not assessed) |
| `firmware/src/wifi/wifi_rf.c` | FORK | w0ts/optimist firmware/hal/fm1_ble_rf.h | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (inferred from the header comment) |
| `firmware/adapters/modd_audio.h` | HEADER-DERIVED | hugelton/Felucca firmware/src/core.h | n/a | header or HAL source derived from the upstream file |
| `firmware/adapters/modd_debug.h` | HEADER-DERIVED | joakimeriksson/fm1-dock firmware/src/dock.c | n/a | header or HAL source derived from the upstream file |
| `firmware/adapters/modd_dock.h` | HEADER-DERIVED | joakimeriksson/fm1-dock firmware/dock/hb_module.h | n/a | header or HAL source derived from the upstream file |
| `firmware/adapters/modd_engine.h` | HEADER-DERIVED | hugelton/Felucca-Engines src/felucca_engine.h | n/a | header or HAL source derived from the upstream file |
| `firmware/adapters/modd_ota.h` | HEADER-DERIVED | keremimo/melodee firmware/src/ota_hw.c | n/a | header or HAL source derived from the upstream file |
| `firmware/adapters/modd_seq.h` | HEADER-DERIVED | hugelton/Felucca firmware/src/ui.c | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_adc.h` | HEADER-DERIVED | hugelton/Felucca firmware/hal/fm1_adc.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_audio.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_audio.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_ble.h` | HEADER-DERIVED | w0ts/optimist firmware/hal/fm1_ble.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_ble_rf.h` | HEADER-DERIVED | w0ts/optimist firmware/hal/fm1_ble_rf.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_cache.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_cache.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_cc.h` | HEADER-DERIVED | hugelton/Felucca firmware/hal/fm1_cc.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_clock.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_clock.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_core1_bar.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_core1_bar.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_core1_stop.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_core1_stop.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_eq.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_eq.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_flash.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_flash.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_gpio.h` | HEADER-DERIVED | hugelton/Felucca firmware/hal/fm1_gpio.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_guard.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_guard.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_input.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_input.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_irq.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_irq.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_isr.S` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_isr.S | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_lcd_hw.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_lcd_hw.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_led_pwm.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_led_pwm.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_multicore.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_multicore.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_power.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_power.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_sys.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_sys.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_time.h` | HEADER-DERIVED | hugelton/Felucca firmware/hal/fm1_time.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_timer.h` | HEADER-DERIVED | hugelton/Felucca firmware/hal/fm1_timer.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_uart.h` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_uart.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_usb.h` | HEADER-DERIVED | hugelton/Felucca firmware/hal/fm1_usb.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_vec.S` | HEADER-DERIVED | keremimo/melodee firmware/hal/fm1_vec.S | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_wifi_rf.h` | HEADER-DERIVED | w0ts/optimist firmware/hal/fm1_ble_rf.h | n/a | header or HAL source derived from the upstream file |
| `firmware/hal/fm1_xip.h` | HEADER-DERIVED | hugelton/Felucca firmware/hal/fm1_xip.h | n/a | header or HAL source derived from the upstream file |
| `patches/felucca/0001-buffer-sizes-from-config.patch` | PATCHED | hugelton/Felucca firmware/src/fx.c, firmware/src/slicer.c | approved: Dmitry Kireev, 2026-11-23, buffer sizes from Kconfig (hook only) | `#ifndef` around `DLY_LEN`, `CHO_LEN` (fx.c) and `SL_LEN` (slicer.c); the wrapper `stack_modd.c` sets them from `CONFIG_FELUCCA_*_LEN` |
