# How modd compares to other FM-1 firmwares

> Back to [README](../README.md)

modd brings networking, storage, testing, and cross-firmware module mixing
together in one configurable build. The
default build includes Melodee's audio core with FMLab's FM6 and four
Felucca-Engines (audio integration is in progress), and runs modd's WiFi stack.

## Infrastructure and connectivity

modd is the only FM-1 firmware with general-purpose networking, a USB file
browser, and a USB hub that handles both MIDI and mass storage. The WiFi driver's
RF and hardware layers were written
[clean-room](hardware/wifi/CLEAN_ROOM_METHODOLOGY.md) from register specs
and protocol documentation; MAC, WPA, and lwIP integration use public standards
and permissive-licensed code (see the
[layer table](hardware/wifi/CLEAN_ROOM_METHODOLOGY.md#layers-and-their-sources)).

|  | [modd](https://github.com/dimmkirr/modd) | [Felucca](https://github.com/hugelton/Felucca) | [Melodee](https://github.com/keremimo/melodee) | [SLOOP](https://github.com/isod89/sloop-fm1) | [JIANT](https://github.com/juanjiant-bit/JIANT) | [Optimist](https://github.com/w0ts/optimist) | [fm1-base](https://github.com/charlesvestal/fm1-base) | [fm1-dock](https://github.com/joakimeriksson/fm1-dock) |
|--|:----:|:-------:|:-------:|:-----:|:-----:|:--------:|:--------:|:--------:|
| WiFi (general networking) | [**Yes**](../firmware/src/wifi/), [clean-room](hardware/wifi/CLEAN_ROOM_METHODOLOGY.md) | No | No | No | No | [Yes](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/wifi/wifi_sta.c#L1), [WPA2 radio only](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/wifi/wifi_ip.h#L2) | [Yes](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/device/wifi.c#L3), [link stubs](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/ble/wifi/wifi_shim.c#L7) | No |
| USB tethering (iPhone) | [**Yes**](../firmware/src/usb/net/usbnet.c) | No | No | No | No | No | No | No |
| USB MIDI host | [**Yes**](../firmware/src/usb/host/usb_host_enum.c) | [Yes](https://github.com/hugelton/felucca-bsp/blob/5942e68b89ab9ccd23f2c541d712ca38fe635f11/chip/usb_host.c) | [Yes](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/usb_host.c) | No | No | No | [Yes](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/bsp/chip/usb_host.c) | No |
| USB Audio (UAC) | [**Yes**](../firmware/ports/melodee/usb/usb.c), [duplex](../firmware/ports/melodee/usb/usb.c) | [Yes](https://github.com/hugelton/Felucca/blob/v1.8.2/firmware/bsp/chip/usb.c#L17), [record + optional playback](https://github.com/hugelton/Felucca/blob/v1.8.2/firmware/bsp/chip/usb.c#L27) | [Yes](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/usb.c#L14), [duplex, optional](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/usb.c#L28) | [Yes](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/usb.c#L11), [record only](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/usb.c#L11) | [Yes](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/usb.c#L16), [record only](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/usb.c#L16) | [Yes](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/felucca.c#L50), [experimental](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/felucca.c#L50) | No | [Yes](https://github.com/joakimeriksson/fm1-dock/blob/v0.9.0/firmware/src/usb.c#L11), [record only](https://github.com/joakimeriksson/fm1-dock/blob/v0.9.0/firmware/src/usb.c#L11) |
| USB disk | [**Yes**](../firmware/src/udisk/) | No | No | No | No | [Yes](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/storage/sd/sd_save.c#L274), [SD card](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/storage/sd/sd_save.c#L274) | No | No |
| File/preset browser | [**Yes**](../firmware/src/udisk/) | No | [Yes](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/browse.c#L3), [flash presets](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/browse.c#L3) | No | No | [Yes](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/ui/optimist/op_sdbrowse.c#L1), [SD card](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/ui/optimist/op_sdbrowse.c#L1) | No | No |
| Theming | [**Yes**](../firmware/src/ui/ui_layout.h), [vtable](../firmware/src/ui/ui_layout.h) | [Yes](https://github.com/hugelton/Felucca/blob/v1.8.2/firmware/src/gfx.c#L23), [palette](https://github.com/hugelton/Felucca/blob/v1.8.2/firmware/src/gfx.c#L23) | [Yes](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/gfx.c#L30), [palette](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/gfx.c#L30) | [Yes](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/gfx.c#L45), [palette + user themes](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/gfx.c#L65) | [Yes](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/gfx.c#L27), [palettes + skins](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/gfx.c#L123) | [Yes](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/display/gfx.c#L52), [palette](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/display/gfx.c#L52) | No | [Yes](https://github.com/joakimeriksson/fm1-dock/blob/v0.9.0/firmware/src/gfx.c#L33), [palette](https://github.com/joakimeriksson/fm1-dock/blob/v0.9.0/firmware/src/gfx.c#L33) |
| Modular components | [**Yes**](../Kconfig), [Kconfig](../Kconfig) | No | No | No | No | [Yes](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/config/profiles/synths.config), [config profiles](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/config/profiles/synths.config) | No | No |
| USB hub support | [**Yes**](../firmware/src/usb/host/usb_hub.c) | [Yes](https://github.com/hugelton/felucca-bsp/blob/5942e68b89ab9ccd23f2c541d712ca38fe635f11/chip/usb_host.c#L145), [MIDI only](https://github.com/hugelton/felucca-bsp/blob/5942e68b89ab9ccd23f2c541d712ca38fe635f11/chip/usb_host.c#L174) | [Yes](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/usb_host.c#L35), [multi-level, MIDI only](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/usb_host.c#L155) | No | No | No | [Yes](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/bsp/chip/usb_host.c#L182) | No |
| Dock modules | [Experimental](../firmware/src/dock/), [needs WiFi off](../kconf/dock.kconfig) | No | No | No | No | No | No | [**Yes**](https://github.com/joakimeriksson/fm1-dock/blob/v0.9.0/firmware/src/dock.c), [23 modules](https://github.com/joakimeriksson/fm1-dock/tree/v0.9.0/modules) |

> Comparison verified 2026-11-17 against vendored commits. Corrections welcome via PR.

<details><summary>Upstream feature details</summary>

- **Optimist** has a full WPA2 STA radio driver ([`wifi_sta.c`](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/wifi/wifi_sta.c#L1)),
  but its IP layer is [built exclusively for Ableton Link](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/wifi/wifi_ip.h#L2)
  multicast and [depends on fm1base](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/wifi/wifi_ip.h#L11).
  It cannot do HTTP, weather, or general TCP/IP.
- **fm1-base** provides [link-layer stubs](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/ble/wifi/wifi_shim.c#L7)
  that satisfy the JieLi SDK's binary WiFi libs, plus a
  [STA join/scan layer](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/device/wifi.c#L3).
  Applications supply their own network code via lwIP sockets.
- **Felucca** has a [USB host driver](https://github.com/hugelton/felucca-bsp/blob/5942e68b89ab9ccd23f2c541d712ca38fe635f11/chip/usb_host.c),
  but it only enumerates MIDI devices: no FatFs, no mass storage class, no file UI.
- **Melodee**'s [`browse.c`](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/browse.c#L3)
  navigates engine presets stored in flash, not files on a USB disk or SD card.
- **Optimist** has a full [SD-card stack](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/storage/sd/sd_save.c#L274)
  with [Petit FatFs](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/storage/sd/sd_save.c#L274)
  and an [SD browser UI](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/ui/optimist/op_sdbrowse.c#L1),
  but no USB mass storage or hub enumeration.
- **Theming**: every firmware except fm1-base has a colour palette system.
  modd is the only one with [vtable-based layout](../firmware/src/ui/ui_layout.h) (swappable layout and widget rendering).
  SLOOP adds [user-uploadable themes](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/gfx.c#L65) via a web editor.
  JIANT has [skins](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/gfx.c#L123) that change rendering mode (HUD, AIR), not just colours.
- **USB MIDI host**: Felucca, Melodee, and fm1-base can receive from external
  USB MIDI controllers (keyboards, pads). Their USB host driver only recognises
  MIDI class devices: plug in a USB disk and nothing happens.
  modd's USB host handles both MIDI and mass storage.
- **USB hubs**: modd, Felucca, Melodee, and fm1-base enumerate USB hubs.
  Felucca and Melodee's hubs only find MIDI devices behind them.
  Melodee supports [multi-level hubs](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/usb_host.c#L155).
  SLOOP, JIANT, Optimist, and fm1-dock have no hub driver.
- **Dock modules**: [fm1-dock](https://github.com/joakimeriksson/fm1-dock) is the
  origin of `.dock` instrument modules (23 ship with it). modd has experimental
  support: ABI tables, header/CRC/relocation, flash install from USB disk,
  and panel key routing.
- **Modular components**: Optimist has [key=value config profiles](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/config/profiles/synths.config)
  that toggle engines and drum kits. modd uses Kconfig with dependency resolution,
  menuconfig TUI, and defconfig presets. Other firmwares have no component selection.

</details>

## Audio, synthesis, and wireless

Audio engines plug in through the [`engine_t` vtable](../firmware/contracts/modd_engine.h).
The default build includes Melodee's audio core with engines from two upstreams (audio integration with the UI is in progress).

|  | modd | Felucca | Melodee | SLOOP | JIANT | Optimist | fm1-base | fm1-dock |
|--|:----:|:-------:|:-------:|:-----:|:-----:|:--------:|:--------:|:--------:|
| Audio engines | [5 engines](../firmware/src/audio/engines/engines.c), 2 upstreams | [15 engines](https://github.com/hugelton/Felucca/blob/v1.8.2/firmware/src/engines.c#L28) | [6-13 engines](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/engines.c#L102) | [12 engines](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/engines.c#L91) | [10 engines](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/engines.c#L49) | [**29 engines**](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/core/registry.h#L146) | [template](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/template/app.c#L27) | [23 .dock modules](https://github.com/joakimeriksson/fm1-dock/tree/v0.9.0/modules) |
| Voice budget | -- | [8v, 4 parts](https://github.com/hugelton/Felucca/blob/v1.8.2/firmware/src/core.h#L11) | [**18 units**, FPU](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/core.h#L17) | [8v, 3 parts](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/core.h#L9) | [8v, 6 tracks](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/core.h#L12) | [8v, 4 tracks](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/core/core.h#L10) | [1 voice](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/template/app.c#L29) | per module |
| BLE MIDI | -- | -- | [**Yes**](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/ble/ble_midi.c#L2) | -- | -- | [**Yes**](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/ble/ble_midi.c#L2) | [**Yes**](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/ble/midi/le_midi.c#L8) | [**Yes**](https://github.com/joakimeriksson/fm1-dock/tree/v0.9.0/firmware/src/ble) |
| Song/track workflow | -- | [4 tracks](https://github.com/hugelton/Felucca/blob/v1.8.2/firmware/src/seq.c#L6) | [4 parts + MIDI](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/core.h#L19) | [4 tracks](https://github.com/isod89/sloop-fm1/blob/v2.8.5/firmware/src/seq.c#L3) | [**6 tracks + song**](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/firmware/src/song_main.c) | [4 tracks](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/core/core.h#L22) | -- | -- |
| Tests | [100+ e2e](../tests/), [400+ unit](../tests/) | [74 unit](https://github.com/hugelton/Felucca/tree/v1.8.2/tests) | [228 unit](https://github.com/keremimo/melodee/tree/v1.6.1/tests) | [58 unit](https://github.com/isod89/sloop-fm1/tree/v2.8.5/tests) | [69 unit](https://github.com/juanjiant-bit/JIANT/tree/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/tests) + [emu](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/web/emu/build.sh) | [**345 unit**](https://github.com/w0ts/optimist/tree/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/tests) + [sim](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/sim/sim.c#L1) | [96 unit](https://github.com/charlesvestal/fm1-base/tree/c8e05997ce970875038156d2bbd69e712eac1543/tests) + [sim](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/host/plat_host.c#L3) | [15 tests](https://github.com/joakimeriksson/fm1-dock/tree/v0.9.0/tests) |

> modd's e2e tests run real firmware in a [WASM emulator](https://github.com/joakimeriksson/fm1-emu)
> with Playwright: they click buttons, turn encoders, and assert LCD pixels.
> Other firmwares test with host-compiled C or scripted checks,
> which cover logic but not the rendered UI (JIANT is the exception: its
> [`emu_test.mjs`](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/web/emu/emu_test.mjs) tests LCD pixels in the emulator).
> [Optimist](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/sim/sim.c#L1) and
> [fm1-base](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/host/plat_host.c#L3) have host simulators;
> [JIANT](https://github.com/juanjiant-bit/JIANT/blob/da8baa1eed7c4e75f1ed9bcf433797b3aeace107/web/emu/build.sh) has a WASM emulator build.

## Why not just use one of them?

- **Optimist** has 29 engines, BLE, WiFi, and 345 tests, but its [WiFi IP layer is hardwired to Ableton Link](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/firmware/src/wifi/wifi_ip.h#L2) (no general TCP/IP), its [config profiles](https://github.com/w0ts/optimist/blob/8e1a4e3f089caa36533d49cbe783ecea7819bd7f/config/profiles/synths.config) have no dependency resolution, and there is no way to swap in a module from another firmware.
- **Melodee** sets the audio bar with an [18-unit FPU voice budget](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/core.h#L17) and 6-13 engines, but its architecture is a monolith: engines depend on Melodee-specific globals, and its [`build_options.h`](https://github.com/keremimo/melodee/blob/v1.6.1/firmware/src/build_options.h) has only runtime toggles, not component selection.
- **fm1-base** is explicitly a framework, but WiFi is [link-layer stubs](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/ble/wifi/wifi_shim.c#L7), audio is a [1-voice template](https://github.com/charlesvestal/fm1-base/blob/c8e05997ce970875038156d2bbd69e712eac1543/template/app.c#L27), and there are no adapter contracts or Kconfig for mixing upstream modules.

Most FM-1 firmwares are monoliths: you use all of one or none of it. modd's
[adapter contracts](../firmware/contracts/) and [Kconfig build system](../Kconfig)
make mixing possible: swap an audio core, add an engine from a different
firmware, enable WiFi, and test the combination in the emulator, all by
toggling Kconfig symbols.
