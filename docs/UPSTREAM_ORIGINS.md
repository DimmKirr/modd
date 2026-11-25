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
| `firmware/src/audio/core/felucca/stack_modd.c` | WRAPPER | hugelton/Felucca firmware/src/{params,mod,voice,slicer,fx,perform,click}.c | n/a | upstream files included unchanged (CONFIG_AUDIO_FELUCCA_STACK_INCLUDED); provides mix_block and felucca_tracks_init for audio.c |
| `firmware/src/core/felucca/core_modd.c` | WRAPPER | hugelton/Felucca firmware/src/{params,mod,voice,slicer,fx,perform,click,song_chain,seq,chord,motion,midi_control,midi_clock}.c | n/a | upstream files included unchanged (CONFIG_CORE_FELUCCA); extends stack_modd.c with the sequencer block |
| `firmware/src/audio/effects/host/fx_unit_exgain_modd.c` | WRAPPER | w0ts/optimist firmware/src/fx/insfx/exgain.c | n/a | upstream file included unchanged |
| `firmware/src/audio/effects/host/dsp_optimist_modd.c` | WRAPPER | w0ts/optimist firmware/src/dsp/dsp.c | n/a | upstream file included unchanged; macros rename the 17 names it shares with Felucca-Engines dsp.c for the span of the include |
| `firmware/src/audio/engines/eng_adpcm_modd.c` | WRAPPER | hugelton/Felucca firmware/src/eng_adpcm.c | n/a | upstream file included unchanged |
| `firmware/src/audio/engines/fm6/fm6_felucca.c` | WRAPPER | hugelton/Felucca firmware/src/eng_fm6.c (+ fm6_core.c, felucca_fm6.h) | n/a | upstream files included unchanged (CONFIG_ENGINE_FM6_FELUCCA) |
| `firmware/src/storage/felucca/storage.c` | WRAPPER | hugelton/Felucca firmware/src/{storage,storage_hw,project,upreset,category,settings_persist,panel}.c | n/a | upstream files included unchanged (CONFIG_STORAGE_FELUCCA); adapter stubs for UI callbacks |
| `firmware/src/audio/engines/eng_sample_modd.c` | WRAPPER | hugelton/Felucca firmware/src/eng_sample.c | n/a | upstream file included unchanged |
| `firmware/src/audio/engines/eng_slice_modd.c` | WRAPPER | hugelton/Felucca firmware/src/eng_slice.c | n/a | upstream file included unchanged |
| `firmware/src/audio/voice.c` | FORK | isod89/sloop-fm1 firmware/src/voice.c | pre-rule (before 2026-11-23): not reviewed | REDUCED (19% of upstream; stripped envelope, glide, unison; minimal round-robin allocator) |
| `firmware/src/ble/ble_att_ft.c` | WRAPPER | w0ts/optimist firmware/src/ble/ble_att.c | 2026-11-24 | PATCHED (`patches/optimist/0002-ble-att-ft-hooks.patch`): wrapper pre-defines ATT_HANDLES_TAIL, ATT_EXTRA_DECLS/DB/STATE, ATT_RESET_HOOK, ATT_LEN_HOOK, ATT_128UUID, ATT_WRITE_HOOK, ATT_EVENT_HOOK for FT GATT service; BLE_DG/BLE_DIAG diagnostics |
| `firmware/src/ble/ble_hid.c` | FORK | w0ts/optimist firmware/src/ble/ble_gattc.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (inferred from the header comment) |
| `firmware/src/ble/ble_hw_stub_sim.c` | FORK | w0ts/optimist firmware/src/ble/ble_hw_stub.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (42-line passive stub became 288-line connection simulation engine) |
| `firmware/src/ble/ble_scan.c` | WRAPPER | w0ts/optimist firmware/src/ble/ble_scan.c | 2026-11-24 | PATCHED (`patches/optimist/0001-ble-scan-hid-hooks.patch`): wrapper pre-defines BLE_SCAN_HID_* hook macros for HID service detection (MODD_BLE_HID); upstream ble_ad_parse and ble_name_is_fm1 restored |
| `firmware/src/ble/ble_scan.h` | HEADER-DERIVED | w0ts/optimist firmware/src/ble/ble_scan.h | n/a | header or HAL source derived from the upstream file |
| `firmware/src/boot/boot_work.c` | FORK | keremimo/melodee firmware/src/ui_boot.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (same purpose, different drawing API: boot_text_1x, not Melodee gfx) |
| `firmware/src/boot/ota.c` | WRAPPER | keremimo/melodee firmware/src/ota.c | 2026-11-24 | PATCHED (`patches/melodee/0002-ota-build-hooks.patch`): wrapper pre-defines OTA_AREA, OTA_LOADER_TAG, OTA_USB_ID, OTA_BOOT_INSTALLER=0, OTA_DEC_DEFINED |
| `firmware/src/debug/debug_dap.c` | FORK | joakimeriksson/fm1-dock firmware/src/dock.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (debug DAP SysEx protocol; derived from dock SysEx parsing but different purpose) |
| `firmware/src/libc.c` | WRAPPER | keremimo/melodee firmware/src/libc.c | 2026-11-24 | INCLUDED: upstream `#include`d unchanged, modd extras (memmove, strlen, strncmp, strchr) appended after |
| `firmware/src/midi/melodee/midi_routing.c` | FORK | keremimo/melodee firmware/src/midi_control.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (stub with no-op bodies; becomes wrapper when Melodee MIDI routing is ported) |
| `firmware/src/midi/usb.c` | WRAPPER | keremimo/melodee firmware/src/usb.c | 2026-11-24 | PATCHED (`patches/melodee/0003-usb-midi-build-hooks.patch`): wrapper pre-defines USB_HAL_HEADER, USB_STRINGS_DEFINED and the USB_HOOK_* callbacks (event log, SysEx handshake, settings reset, debug DAP) |
| `firmware/src/modd.c` | FORK | keremimo/melodee firmware/src/main.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (modd unity-build root; shares structure but diverged completely) |
| `firmware/src/net/arch/cc.h` | HEADER-DERIVED | lwip-tcpip/lwip src/include/lwip/arch.h | n/a | header or HAL source derived from the upstream file |
| `firmware/src/net/lwipopts.h` | HEADER-DERIVED | lwip-tcpip/lwip contrib/examples/example_app/test_configs/opt_nosys_ipv4.h | n/a | header or HAL source derived from the upstream file |
| `firmware/src/seq/seq.c` | FORK | hugelton/Felucca firmware/src/seq.c | pre-rule (before 2026-11-23): not reviewed | REDUCED (modd-written step sequencer with parameter locks; kept transport, polymeter, live recording; left out song chain, motion, chords, MIDI clock) |
| `firmware/src/settings/settings_fmlab.c` | FORK | jsmith13/fm_lab firmware/src/platform.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (settings persistence; derived from platform.c structure but different data layout) |
| `firmware/src/udisk/diskio.h` | HEADER-DERIVED | FatFs R0.15 source/diskio.h, https://elm-chan.org/fsw/ff/arc/ff15.zip (pinned in vendir.yml) | n/a | header or HAL source derived from the upstream file |
| `firmware/src/udisk/ffconf.h` | HEADER-DERIVED | FatFs R0.15 source/ffconf.h, https://elm-chan.org/fsw/ff/arc/ff15.zip (pinned in vendir.yml) | n/a | header or HAL source derived from the upstream file |
| `firmware/src/udisk/usb_host_xfer.c` | FORK | keremimo/melodee firmware/src/usb_host.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (reimplemented SIE EP0/bulk transfers with finer polling; same registers, different code) |
| `firmware/src/udisk/usb_hub.c` | FORK | keremimo/melodee firmware/src/usb_host.c | pre-rule (before 2026-11-23): not reviewed | REWRITTEN (1.75× upstream; adds hub class, device table, ECM and Apple-net not in Melodee) |
| `firmware/src/ui/shells/felucca/lvgl_palette.c` | WRAPPER | firmware/upstream/felucca/build/gen/ui_palettes.h | n/a | macros and static_assert from upstream #include; colour array in modd ui_pal_t layout (C initializer requires literals) |
| `firmware/src/ui/shells/melodee/gfx.c` | WRAPPER | keremimo/melodee firmware/src/gfx.c | 2026-11-24 | INCLUDED: wrapper pre-defines `MELODEE_CANVAS_ROWS 124u`, upstream `#include`d unchanged |
| `firmware/src/ui/shells/melodee/lcd.c` | WRAPPER | keremimo/melodee firmware/src/lcd.c | 2026-11-24 | PATCHED (`patches/melodee/0001-lcd-debug-framebuffer-hooks.patch`): upstream `#include`d with debug framebuffer hooks |
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
| `patches/melodee/0001-lcd-debug-framebuffer-hooks.patch` | PATCHED | keremimo/melodee firmware/src/lcd.c | approved: Dmitry Kireev, 2026-11-24, debug framebuffer hooks (hook only) | `#ifdef CONFIG_DEBUG_FRAMEBUFFER` / `lcdfb_fill` / `lcdfb_blit` hooks in `lcd_fill` and `lcd_blit` |
| `patches/melodee/0002-ota-build-hooks.patch` | PATCHED | keremimo/melodee firmware/src/ota.c | approved: Dmitry Kireev, 2026-11-24, OTA staging area, loader tag, USB id, boot-installer toggle (hook only) | `#ifndef` around `OTA_AREA`, `OTA_LOADER_TAG`, `OTA_USB_ID`, `OTA_DEC_DEFINED`; `OTA_BOOT_INSTALLER` gates the m-bootloader installer path |
| `patches/melodee/0003-usb-midi-build-hooks.patch` | PATCHED | keremimo/melodee firmware/src/usb.c | approved: Dmitry Kireev, 2026-11-24, USB strings, HAL header, event and SysEx hooks (hook only) | `USB_HAL_HEADER`, `EP0BUF_DEFINED`, `USB_STRINGS_DEFINED`, `USB_HOOK_*` callbacks for events, MIDI input and SysEx dispatch |
| `patches/optimist/0001-ble-scan-hid-hooks.patch` | PATCHED | w0ts/optimist firmware/src/ble/ble_scan.c | approved: Dmitry Kireev, 2026-11-24, HID service detection hooks in ble_scan_add (hook only) | `BLE_SCAN_HID_VARS`, `BLE_SCAN_HID_AFTER_PARSE`, `BLE_SCAN_HID_IS_LISTED`, `BLE_SCAN_HID_KIND`, `BLE_SCAN_HID_AFTER_LISTED` hooks in `ble_scan_add`; defaults are identity / no-op |
| `patches/optimist/0002-ble-att-ft-hooks.patch` | PATCHED | w0ts/optimist firmware/src/ble/ble_att.c | approved: Dmitry Kireev, 2026-11-24, FT GATT service and BLE diagnostics hooks (hook only) | `ATT_HANDLES_TAIL`, `ATT_EXTRA_DECLS`, `ATT_EXTRA_DB`, `ATT_EXTRA_STATE`, `ATT_RESET_HOOK`, `ATT_LEN_HOOK`, `ATT_128UUID`, `ATT_WRITE_HOOK`, `ATT_EVENT_HOOK` hooks + `#ifdef BLE_DG`/`BLE_DIAG` blocks; defaults are upstream's behaviour |
