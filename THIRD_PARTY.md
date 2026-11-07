# Non-GPL components

modd's firmware and project code are licensed under
[GPL-3.0-only](LICENSE). The components below retain their own licenses.
This inventory covers the current minimal build, not engines planned for future
integration. Updated: 2026-11-07.

## Included in firmware packages

These three files belong to the **JieLi AC79 SDK**, published by Zhuhai JieLi
Technology under Apache-2.0. They are downloaded by the SDK task in
[Taskfile.yml](Taskfile.yml), outside this repository. During the build,
[`flash_image()` in the packager](tools/build/fm1pkg_make.py) reads them from
`$AC79_SDK/cpu/wl82/tools/` and incorporates their bytes into the flash image
inside `build/modd.fwsc`.

| Component                 | SDK path and upstream source                                                                                                                            | Purpose                                                    | License    |
|---------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------|------------|
| JieLi SPL                 | [`cpu/wl82/tools/uboot.boot`](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/cpu/wl82/tools/uboot.boot)               | First-stage boot loader in the package's flash header area | Apache-2.0 |
| Chip configuration        | [`cpu/wl82/tools/cfg_tool.bin`](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/cpu/wl82/tools/cfg_tool.bin)           | Configuration data in the package's application area       | Apache-2.0 |
| Hardware EQ configuration | [`cpu/wl82/tools/cfg/eq_cfg_hw.bin`](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/cpu/wl82/tools/cfg/eq_cfg_hw.bin) | Default EQ data in the package's application area          | Apache-2.0 |

License basis: the
SDK's [upstream license](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/LICENSE)
and [publisher's license statement](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/release/AC79NN_SDK_V1.2.0/README-en.md).
The downloaded SDK's root `LICENSE` was checked and is byte-identical to our
local copy, [Apache-2.0.txt](LICENSES/Apache-2.0.txt). No separate license or NOTICE was
found alongside these three files. This attribution follows the SDK-wide
license; the binaries do not carry individual source-license headers.

`uboot.boot` here means JieLi's SDK SPL, not the separate Das U-Boot project.
It is also distinct from modd's GPL-3.0 boot stub in `firmware/stub/` and
update loader in `firmware/loader/`.

The current downloader selects SDK tag `AC79NN_SDK_V1.2.14_2026-10-13`.
An existing SDK installation or an `AC79_SDK` override can supply different
bytes. The SDK files checked for this build have these SHA-256 digests:

```text
64ccc63e4df3238046fadb200f1b134311d8fe6ec6e3ccb8c8b81379231b867e  uboot.boot
276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b  cfg_tool.bin
179ac9f4501d2cf8c3bdbc06fcda1e46228b7ace7e339342c767bb6568de57d8  cfg/eq_cfg_hw.bin
```

Distributions containing these files must include their Apache-2.0 license
and applicable notices. Apache-2.0 is compatible with GPL-3.0; retaining these
notices does not change modd's project license. See the
[Apache Software Foundation's compatibility explanation](https://apache.org/licenses/GPL-compatibility.html).

## Vendored upstream trees

Upstream source trees are fetched with [vendir](https://carvel.dev/vendir):
[vendir.yml](vendir.yml) pins each tree to a tag or commit and `vendir.lock.yml`
records the resolved SHA. The trees themselves are not committed: `task build`
and `task test:unit` run `task vendor:ensure`, which fetches them at the locked
commits when missing; `task vendor:sync` re-resolves the pins. The release
workflow packs the fetched trees into `modd-source.tar.gz` so the corresponding
source is complete. Vendored files are never patched: modd adapts them through
`firmware/adapters/` and project-owned config headers.

| Path | Source | Pin | License | Use |
|------|--------|-----|---------|-----|
| `firmware/upstream/felucca/` | [hugelton/Felucca](https://github.com/hugelton/Felucca) | v1.6.1 | GPL-3.0 | UI renderer (`gfx.c`, `lcd.c`), audio core reference, sequencer reference |
| `firmware/upstream/felucca-engines/` | [hugelton/Felucca-Engines](https://github.com/hugelton/Felucca-Engines) | c46e008 | MIT | ANALOG, NOISE, LOFI, TRIO, WHEEL engines; `dsp.c` DSP helpers; `felucca_engine.h` vtable |
| `firmware/upstream/melodee/` | [keremimo/melodee](https://github.com/keremimo/melodee) | v1.4.5 | GPL-3.0 | CZ, SID, PROPHET, FORMANT + 7 more engines; extended `dsp.c`; FPU-optimized audio core reference |
| `firmware/upstream/sloop/` | [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) | v2.8 | GPL-3.0 | DRAWBAR, GRAIN, SAMPLE, SLICE engines; arranger/scene system reference |
| `firmware/upstream/fmlab/` | [jsmith13/fm_lab](https://github.com/jsmith13/fm_lab) | v1.1.0 | GPL-3.0; `dx7_core.c` is Apache-2.0 AND GPL-3.0-only (Dexed/msfa, Google and Pascal Gauthier; integer port Sven Trogus) | Q23 24-bit audio pipeline; DX7 engine; 2x oversampling; FDN reverb; TPDF dithering |
| `firmware/upstream/optimist/` | [w0ts/optimist](https://github.com/w0ts/optimist) | a4104fc | GPL-3.0 | BLE stack (`firmware/src/ble/`): link layer, host, SMP, ATT/GATT, BLE MIDI |
| `firmware/vendor/lwip/` | [lwip-tcpip/lwip](https://github.com/lwip-tcpip/lwip), mirror of [Savannah](https://git.savannah.nongnu.org/cgit/lwip.git) | STABLE-2_2_1_RELEASE (77dcd25) | BSD-3-Clause | IPv4, DHCP, DNS, TCP, UDP for the WiFi module; config in `firmware/src/wifi/lwipopts.h` |
| `firmware/vendor/fatfs/` | [FatFs by ChaN](http://elm-chan.org/fsw/ff/), `ff15.zip` with pinned SHA-256 | R0.15 | [FatFs license](http://elm-chan.org/fsw/ff/doc/appnote.html#license) (1-clause BSD) | FAT32 for USB mass storage; config in `firmware/src/udisk/ffconf.h`, disk I/O in `firmware/src/udisk/diskio.[ch]`, `strchr` in `firmware/src/libc.c` |

Felucca-Engines is vendored separately from Felucca because upstream Felucca
references it as a git submodule (MIT-licensed engines kept apart from the
GPL-3.0 firmware). The pinned commit `c46e008` is the submodule pointer in
Felucca v1.6.1.

modd includes upstream source via `#include` directives (unity build), not
by copying. Engine source files are included conditionally from the vendored
paths (e.g. `#include "upstream/felucca-engines/src/eng_noise.c"`), gated by
Kconfig symbols. See [docs/PORTING.md](docs/PORTING.md) for the adapter pattern.

## Derived files

Files modd carries that were copied or adapted from an upstream tree or from fm1-dock.
Each file header has `Origin:` lines with the same pinned links (the commits in
`vendir.lock.yml`; for fm1-dock, the commit the files were taken from), the upstream
copyright holders, and a `Modifications Copyright` line where modd changed the code.
"(unchanged)" means the file is byte-identical to the pinned upstream file apart from
the added Origin lines. Generated from the headers by
`.scratch/tools/adapter-provenance/apply_fixes.py` on 2026-11-07.

| modd file | Origin (pinned) | Upstream license | modd license |
|---|---|---|---|
| `firmware/adapters/modd_audio.h` | [hugelton/Felucca firmware/src/core.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/core.h) @ v1.6.1 (a53ecf42) | GPL-3.0-only | GPL-3.0-only |
| `firmware/adapters/modd_debug.h` | [joakimeriksson/fm1-dock firmware/src/dock.c](https://github.com/joakimeriksson/fm1-dock/blob/2118c7299fdc5fabf81eb81962f1168a21792fd7/firmware/src/dock.c) @ main (2118c729) | GPL-3.0-only | GPL-3.0-only |
| `firmware/adapters/modd_engine.h` | [hugelton/Felucca-Engines src/felucca_engine.h](https://github.com/hugelton/Felucca-Engines/blob/c46e0086f69bdc26df1cd144dca9e683cc4531dd/src/felucca_engine.h) @ (c46e0086)<br>[keremimo/melodee firmware/src/core.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/core.h) @ v1.4.5 (398ada1e) | GPL-3.0-only, MIT | GPL-3.0-only |
| `firmware/adapters/modd_ota.h` | [keremimo/melodee firmware/src/ota_hw.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/ota_hw.c) @ v1.4.5 (398ada1e)<br>[isod89/sloop-fm1 firmware/src/felucca.c](https://github.com/isod89/sloop-fm1/blob/b77bf55e3bf804665b8f5dffb5955bd8443311f0/firmware/src/felucca.c) @ v2.8 (b77bf55e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/adapters/modd_seq.h` | [hugelton/Felucca firmware/src/ui.c](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/ui.c) @ v1.6.1 (a53ecf42) | GPL-3.0-only | GPL-3.0-only |
| `firmware/app.ld` | [keremimo/melodee firmware/app.ld](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/app.ld) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/crt0.S` | [keremimo/melodee firmware/crt0.S](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/crt0.S) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_adc.h` | [hugelton/Felucca firmware/hal/fm1_adc.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_adc.h) @ v1.6.1 (a53ecf42) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_audio.h` | [keremimo/melodee firmware/hal/fm1_audio.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_audio.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_ble.h` | [w0ts/optimist firmware/hal/fm1_ble.h](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/hal/fm1_ble.h) @ v0.1 (a4104fcd) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_ble_rf.h` | [w0ts/optimist firmware/hal/fm1_ble_rf.h](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/hal/fm1_ble_rf.h) @ v0.1 (a4104fcd) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_cache.h` | [keremimo/melodee firmware/hal/fm1_cache.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_cache.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_cc.h` | [hugelton/Felucca firmware/hal/fm1_cc.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_cc.h) @ v1.6.1 (a53ecf42) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_clock.h` | [keremimo/melodee firmware/hal/fm1_clock.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_clock.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_core1_bar.h` | [keremimo/melodee firmware/hal/fm1_core1_bar.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_core1_bar.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_core1_stop.h` | [keremimo/melodee firmware/hal/fm1_core1_stop.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_core1_stop.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_eq.h` | [keremimo/melodee firmware/hal/fm1_eq.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_eq.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_flash.h` | [keremimo/melodee firmware/hal/fm1_flash.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_flash.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_gpio.h` | [hugelton/Felucca firmware/hal/fm1_gpio.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_gpio.h) @ v1.6.1 (a53ecf42) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_guard.h` | [keremimo/melodee firmware/hal/fm1_guard.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_guard.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_input.h` | [keremimo/melodee firmware/hal/fm1_input.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_input.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_irq.h` | [keremimo/melodee firmware/hal/fm1_irq.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_irq.h) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_isr.S` | [keremimo/melodee firmware/hal/fm1_isr.S](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_isr.S) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_lcd_hw.h` | [keremimo/melodee firmware/hal/fm1_lcd_hw.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_lcd_hw.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_led_pwm.h` | [keremimo/melodee firmware/hal/fm1_led_pwm.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_led_pwm.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_multicore.h` | [keremimo/melodee firmware/hal/fm1_multicore.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_multicore.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_power.h` | [keremimo/melodee firmware/hal/fm1_power.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_power.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_sys.h` | [keremimo/melodee firmware/hal/fm1_sys.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_sys.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_time.h` | [hugelton/Felucca firmware/hal/fm1_time.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_time.h) @ v1.6.1 (a53ecf42) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_timer.h` | [hugelton/Felucca firmware/hal/fm1_timer.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_timer.h) @ v1.6.1 (a53ecf42) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_uart.h` | [keremimo/melodee firmware/hal/fm1_uart.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_uart.h) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_usb.h` | [hugelton/Felucca firmware/hal/fm1_usb.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_usb.h) @ v1.6.1 (a53ecf42) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_vec.S` | [keremimo/melodee firmware/hal/fm1_vec.S](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/hal/fm1_vec.S) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_wifi_rf.h` | [w0ts/optimist firmware/hal/fm1_ble_rf.h](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/hal/fm1_ble_rf.h) @ v0.1 (a4104fcd) | GPL-3.0-only | GPL-3.0-only |
| `firmware/hal/fm1_xip.h` | [hugelton/Felucca firmware/hal/fm1_xip.h](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_xip.h) @ v1.6.1 (a53ecf42) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/loader/crt0_ldr.S` | [keremimo/melodee firmware/loader/crt0_ldr.S](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/loader/crt0_ldr.S) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/loader/ldr_core.c` | [keremimo/melodee firmware/loader/ldr_core.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/loader/ldr_core.c) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/loader/loader.c` | [keremimo/melodee firmware/loader/loader.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/loader/loader.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/loader/loader.ld` | [keremimo/melodee firmware/loader/loader.ld](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/loader/loader.ld) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/audio/core/audio.c` | [keremimo/melodee firmware/src/audio.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/audio.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/audio/engines/dsp.c` | [hugelton/Felucca-Engines src/dsp.c](https://github.com/hugelton/Felucca-Engines/blob/c46e0086f69bdc26df1cd144dca9e683cc4531dd/src/dsp.c) @ (c46e0086) (unchanged) | MIT | MIT |
| `firmware/src/audio/voice.c` | [isod89/sloop-fm1 firmware/src/voice.c](https://github.com/isod89/sloop-fm1/blob/b77bf55e3bf804665b8f5dffb5955bd8443311f0/firmware/src/voice.c) @ v2.8 (b77bf55e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ble/ble_att_ft.c` | [w0ts/optimist firmware/src/ble/ble_att.c](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/src/ble/ble_att.c) @ v0.1 (a4104fcd) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ble/ble_scan.c` | [w0ts/optimist firmware/src/ble/ble_scan.c](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/src/ble/ble_scan.c) @ v0.1 (a4104fcd) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ble/ble_scan.h` | [w0ts/optimist firmware/src/ble/ble_scan.h](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/src/ble/ble_scan.h) @ v0.1 (a4104fcd) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ble/ble_hid.c` | [w0ts/optimist firmware/src/ble/ble_gattc.c](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/src/ble/ble_gattc.c) @ v0.1 (a4104fcd) (ATT helpers and discovery pattern) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ble/ble_hw_stub_sim.c` | [w0ts/optimist firmware/src/ble/ble_hw_stub.c](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/src/ble/ble_hw_stub.c) @ v0.1 (a4104fcd) (passive stub copied, simulation added) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/boot/ota.c` | [keremimo/melodee firmware/src/ota.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/ota.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/debug/debug_dap.c` | [joakimeriksson/fm1-dock firmware/src/dock.c](https://github.com/joakimeriksson/fm1-dock/blob/2118c7299fdc5fabf81eb81962f1168a21792fd7/firmware/src/dock.c) @ main (2118c729) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/libc.c` | [keremimo/melodee firmware/src/libc.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/libc.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/midi/melodee/midi_routing.c` | [keremimo/melodee firmware/src/midi_control.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/midi_control.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/midi/usb.c` | [keremimo/melodee firmware/src/usb.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/usb.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/modd.c` | [keremimo/melodee firmware/src/main.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/main.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/seq/melodee/seq.c` | [keremimo/melodee firmware/src/seq.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/seq.c) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/settings/settings_fmlab.c` | [jsmith13/fm_lab firmware/src/platform.c](https://github.com/jsmith13/fm_lab/blob/cf35dc26709ede9c974654dfc2135f8c25a00082/firmware/src/platform.c) @ v1.1.0 (cf35dc26) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ui/melodee/gfx.c` | [keremimo/melodee firmware/src/gfx.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/gfx.c) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ui/melodee/lcd.c` | [keremimo/melodee firmware/src/lcd.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/src/lcd.c) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/udisk/diskio.h` | [FatFs R0.15 source/diskio.h](https://elm-chan.org/fsw/ff/arc/ff15.zip) (trimmed to the unity build) | FatFs | FatFs |
| `firmware/src/udisk/ffconf.h` | [FatFs R0.15 source/ffconf.h](https://elm-chan.org/fsw/ff/arc/ff15.zip) (configuration template, values changed) | FatFs | FatFs |
| `firmware/src/ui/themes/felucca/draw.c` | [hugelton/Felucca firmware/src/ui_draw.c](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/ui_draw.c) @ v1.6.1 (a53ecf42) (layout and drawing sequence; reimplemented) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ui/themes/felucca/fui_icons.c` | [hugelton/Felucca firmware/src/icons.c](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/icons.c) @ v1.6.1 (a53ecf42) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/ui/themes/felucca/ui.c` | [hugelton/Felucca firmware/src/ui.c](https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/src/ui.c) @ v1.6.1 (a53ecf42) (layout constants; reimplemented) | GPL-3.0-only | GPL-3.0-only |
| `firmware/src/wifi/arch/cc.h` | [lwip-tcpip/lwip src/include/lwip/arch.h](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/include/lwip/arch.h) @ STABLE-2_2_1_RELEASE (77dcd25a) | BSD-3-Clause | BSD-3-Clause |
| `firmware/src/wifi/lwipopts.h` | [lwip-tcpip/lwip contrib/examples/example_app/test_configs/opt_nosys_ipv4.h](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/contrib/examples/example_app/test_configs/opt_nosys_ipv4.h) @ STABLE-2_2_1_RELEASE (77dcd25a) | BSD-3-Clause | BSD-3-Clause |
| `firmware/src/wifi/nonfree/nf_main.c` | [charlesvestal/fm1-base device/radio.c](https://github.com/charlesvestal/fm1-base/blob/02f422c7d9de121ca099bb2fdebe5c035f59e8ad/device/radio.c) @ main (02f422c7) | MPL-2.0 | MPL-2.0 |
| `firmware/src/wifi/nonfree/nf_wifi_station.c` | [charlesvestal/fm1-base device/wifi.c](https://github.com/charlesvestal/fm1-base/blob/02f422c7d9de121ca099bb2fdebe5c035f59e8ad/device/wifi.c) @ main (02f422c7) | MPL-2.0 | MPL-2.0 |
| `firmware/src/wifi/wifi_rf.c` | [w0ts/optimist firmware/hal/fm1_ble_rf.h](https://github.com/w0ts/optimist/blob/a4104fcdc78b5173ef058b282058c2366c627f9b/firmware/hal/fm1_ble_rf.h) @ v0.1 (a4104fcd) (RF-die SPI and BBP window helpers) | GPL-3.0-only | GPL-3.0-only |
| `firmware/stub/crt0_stub.S` | [keremimo/melodee firmware/stub/crt0_stub.S](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/stub/crt0_stub.S) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/stub/stub.c` | [keremimo/melodee firmware/stub/stub.c](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/stub/stub.c) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `firmware/stub/stub.ld` | [keremimo/melodee firmware/stub/stub.ld](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/stub/stub.ld) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `firmware/stub/stub_core.h` | [keremimo/melodee firmware/stub/stub_core.h](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/firmware/stub/stub_core.h) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `tools/build/fm1pkg_make.py` | [keremimo/melodee tools/fm1pkg_make.py](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/tools/fm1pkg_make.py) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `tools/build/lz4blk.py` | [keremimo/melodee tools/lz4blk.py](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/tools/lz4blk.py) @ v1.4.5 (398ada1e) (unchanged) | GPL-3.0-only | GPL-3.0-only |
| `tools/codegen/gen_engine_tables.py` | [hugelton/Felucca-Engines tools/gen_engine_tables.py](https://github.com/hugelton/Felucca-Engines/blob/c46e0086f69bdc26df1cd144dca9e683cc4531dd/tools/gen_engine_tables.py) @ (c46e0086) | MIT | GPL-3.0-only |
| `tools/device/fm1_install.py` | [keremimo/melodee tools/fm1_install.py](https://github.com/keremimo/melodee/blob/398ada1e09d864c12f3b778a43e45c7c2a8031b8/tools/fm1_install.py) @ v1.4.5 (398ada1e) | GPL-3.0-only | GPL-3.0-only |
| `tools/device/fm1_midi.py` | [isod89/sloop-fm1 tools/fm1_install.py](https://github.com/isod89/sloop-fm1/blob/b77bf55e3bf804665b8f5dffb5955bd8443311f0/tools/fm1_install.py) @ v2.8 (b77bf55e) | GPL-3.0-only | GPL-3.0-only |
| `tools/device/hbdeploy.py` | [joakimeriksson/fm1-dock tools/hbdeploy.py](https://github.com/joakimeriksson/fm1-dock/blob/2118c7299fdc5fabf81eb81962f1168a21792fd7/tools/hbdeploy.py) @ main (2118c729) | GPL-3.0-only | GPL-3.0-only |

Permissive notices that must travel with these files are tracked here because the
upstream trees are not committed: `LICENSES/MIT-Felucca-Engines.txt` (for `dsp.c`,
`modd_engine.h`, `gen_engine_tables.py`), `LICENSES/BSD-3-Clause-lwIP.txt` (for
`wifi/arch/cc.h`, `wifi/lwipopts.h`), `LICENSES/FatFs.txt` (for the FatFs
configuration in `firmware/src/udisk/`) and `LICENSES/MPL-2.0.txt` (for the two
`wifi/nonfree/` files adapted from fm1-base, which stay MPL-2.0 and join the GPL-3.0-only
firmware under MPL-2.0 §3.3; they compile only with `CONFIG_WIFI_NONFREE`). `dsp.c` is an unchanged copy and keeps its
MIT header; `modd_engine.h` and `gen_engine_tables.py` are modified and are GPL-3.0-only
with the MIT copyright notice kept. The release source archive excludes Melodee's
Casio CZ-1 and Sequential Prophet-5 factory preset `.syx` assets, which Melodee's
LICENSING.md marks "no licence granted" and modd does not use.

## Included in the source tree

| Component | Local path | Source and license | Use |
| --- | --- | --- | --- |
| setup-fm1-emulator control helpers | [tests/fm1_controls.py](tests/fm1_controls.py) | [MIT-licensed upstream example](https://github.com/DimmKirr/setup-fm1-emulator/blob/9d0fdd259343edbe29f1e4a3bfea85c7739f5d3c/examples/e2e_firmware_ui_test.py), [local license](LICENSES/MIT.txt) | Named encoder, button, and note controls for E2E tests; not included in firmware |

The port retains the example's Kerem Kilic (Ellic Studio) copyright and the
repository's DimmKirr notice. Coordinates and encoder aliases are adapted to
fm1-emu v0.2.0; key release on errors and input validation are added locally.

## Build and test dependencies

These run on the development host and are not copied into the firmware.
Python package versions are recorded in [uv.lock](uv.lock).

| Component  | License / upstream evidence                                                    | Use                                          |
|------------|--------------------------------------------------------------------------------|----------------------------------------------|
| Kconfiglib | [ISC](https://github.com/ulfalizer/Kconfiglib/blob/master/LICENSE.txt)         | Configuration parser and menuconfig          |
| pytest     | [MIT](https://github.com/pytest-dev/pytest/blob/main/LICENSE)                  | Unit and E2E test runner                     |
| Playwright | [Apache-2.0](https://github.com/microsoft/playwright-python/blob/main/LICENSE) | Browser automation                           |
| Task       | [MIT](https://github.com/go-task/task/blob/main/LICENSE)                       | Build/download/test orchestration            |
| uv         | [MIT OR Apache-2.0](https://github.com/astral-sh/uv#license)                   | Python environment and dependency management |
| mise       | [MIT](https://github.com/jdx/mise/blob/main/LICENSE)                         | Pinned Python, uv, and Task installations     |

Transitive Python dependencies currently used by pytest and Playwright:

| Component | License / upstream evidence                                                        |
|-----------|------------------------------------------------------------------------------------|
| iniconfig | [MIT](https://github.com/pytest-dev/iniconfig/blob/main/LICENSE)                   |
| pluggy    | [MIT](https://github.com/pytest-dev/pluggy/blob/main/LICENSE)                      |
| packaging | [Apache-2.0 OR BSD-2-Clause](https://github.com/pypa/packaging/blob/main/LICENSE)  |
| Pygments  | [BSD-2-Clause](https://github.com/pygments/pygments/blob/master/LICENSE)           |
| pyee      | [MIT](https://github.com/jfhbrook/pyee/blob/main/LICENSE)                          |
| greenlet  | [MIT AND PSF-2.0](https://github.com/python-greenlet/greenlet/blob/master/LICENSE) |

The development environment also supplies Python, Chromium, Rust for optional
emulator builds, and the JieLi compiler toolchain. Their own distributions carry
their component licenses; this document is not a full host-environment SBOM.
In particular, the SDK's Apache-2.0 license must not be assumed to cover the
separately downloaded proprietary pi32v2 compiler. No compiler runtime library
is linked by the current freestanding build.

## Updating this inventory

Add an entry when importing a non-GPL source, asset, binary, or dependency.
Record its exact path, origin, license evidence, and whether it ships in the
source tree, firmware package, or only the development environment. Planned
MIT engine interfaces and third-party engines should be listed when imported.
