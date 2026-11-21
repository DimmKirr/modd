# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

modd is a modular firmware framework for the M-VAVE FM-1 synthesizer (JieLi AC791N SoC, pi32v2 architecture). It brings modules from any FM-1 firmware into one configurable source tree. Three community upstreams are vendored today: **Felucca** (hugelton), **Melodee** (keremimo), and **SLOOP** (isod89). Any FM-1 firmware that builds with the JieLi toolchain can be added the same way (see [Adding Upstream Firmware](#adding-upstream-firmware)). Everything in modd is GPL-3.0-only; the upstream Felucca-Engines interface (`felucca_engine.h`) and engines are MIT.

## Build System

Kconfiglib-based configuration. All commands go through [Taskfile](https://taskfile.dev), which wraps the JieLi toolchain env vars (`JIELI_TOOLCHAIN`, `AC79_SDK`) automatically:

```
task build                              # build modd firmware with JieLi (default for hardware)
task build -- --defconfig modd_emu      # build a specific defconfig from configs/
task modd:build:modd_emu                # same, via per-defconfig task (emulator defconfigs default to the open toolchain)
USE_OPEN_TOOLCHAIN=1 task build         # force the open pi32v2 LLVM toolchain; output in build/modd-open/
task modd:test:modd_emu:api             # build + API-test a specific defconfig
task build:live                         # build with source-change caching (the real build target)
task flash                              # build if sources or .env changed, then flash over USB-MIDI
task build:size-check DEFCONFIG=modd       # memory report of the last build (printed after every link too)
task config                             # interactive menuconfig
task config:generate:defconfig -- melodee  # load a preset defconfig and generate config.h
task config:generate:local                # .config -> build/modd/gen/config.h
task dock:modules -- /path/to/disk      # copy fm1-dock's prebuilt .dock modules to a USB disk / SD card root
task test                               # run all tests (unit, then direct-API)
task test:unit                          # Python unit tests only
task test:api                           # build modd_emu, drive it over HTTP in fm1-live
task vendor:build                       # (testing) build all upstream firmwares
task vendor:build:felucca               # (testing) build clean upstream Felucca
task vendor:flash:melodee               # (testing) flash pre-built upstream Melodee
task debug:usb                          # dump USB host state, SIE registers, filtered USB log lines
task debug:usb -- --follow              # same + tail all log lines
task logs:debug                         # full debug dump: build facts, crash record, log ring + tail
```

**Important: never call `python tools/build.py` directly.** Always use `task build`. The Taskfile sets required env vars (`JIELI_TOOLCHAIN`, `AC79_SDK`), downloads the toolchain if missing, and ensures vendored dependencies are present. Without the Taskfile, `build.py` will fail with `JIELI_TOOLCHAIN not set`. No arguments builds the default modd defconfig; pass `-- --defconfig <name>` for others.

`task build` delegates to `build:live`, which has source-change caching (skips when sources and `.env` are unchanged). The cache is kept per defconfig: each `--defconfig <name>` has its own checksum, and `task modd:build:<name>` shares it. Use `build:live` only when you need to reference the cacheable target directly (e.g. as a dependency).

### Environment: flake, mise, uv

Three layers, each owning one thing. Use all three when in doubt:

```
nix develop -c mise exec -- task docs
```

| Layer | Owns | Files |
|-------|------|-------|
| Nix dev shell | System libraries: `qemu-x86_64`, libusb, FriBiDi (Pillow needs it at runtime for raqm), and on aarch64 the x86-64 glibc/libstdc++ for the JieLi compiler, exported as `JIELI_X86_GLIBC` / `JIELI_X86_STDCPP` | `flake.nix`, `flake.lock` |
| mise | Tool versions: Task, uv, Python, vendir | `.mise.toml` |
| uv | Python dependencies in `.venv` | `pyproject.toml`, `uv.lock` |

- **`mise exec --` selects the pinned Task.** If another `task` comes first on `PATH` (the devcell container ships Task 3.45 in its Nix profile), it refuses the remote Taskfile includes with `Remote taskfiles are not enabled` unless `TASK_X_REMOTE_TASKFILES=1` is set. The pinned Task needs no flag.
- **`nix develop` is needed whenever fonts are rendered**: the first build of any defconfig (font headers are generated once per `build/<defconfig>/gen/`, then reused), `task docs`, `task docs:themes:ui:screenshots`, and the upstream firmware builds. Without FriBiDi, Pillow reports `raqm: False` and `gen_aa_font.py` fails with `not supported without libraqm`. Check: `uv run python -c "from PIL import features; print(features.check('raqm'))"`.
- Rebuilds with the generated headers already in place, `task test:unit` and the API tests run without the dev shell.
- Do not add the x86-64 glibc to the dev shell's `buildInputs`: it leaks x86 headers into the native `cc` and breaks the host-compiled C harnesses in `tests/` (`gnu/stubs-32.h` not found).

Before committing: `task docs` (about 5-6 minutes: two firmware builds plus emulator captures for every theme page), `task docs:check`, `uv lock --check`, `task test:unit`. See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#before-committing).

### .config and defconfigs

`.config` is a symlink to `configs/modd_defconfig`, the main development profile. Do not replace it with a regular file. To build with a different defconfig from `configs/`, pass `--defconfig <name>` (without the `_defconfig` suffix):

```
task build -- --defconfig modd_emu      # builds with configs/modd_emu_defconfig
```

Generated files go to `build/<defconfig>/gen/config.h` (e.g. `build/modd/gen/config.h`).

## Architecture

modd compiles as a **single translation unit** (unity build): one `.c` file `#include`s all others. Every function is `static`. Kconfig controls which `#include` directives are active via `CONFIG_*` defines in `autoconf.h`.

### Terminology

See [docs/COMPONENTS.md](docs/COMPONENTS.md) for full component map with diagrams.

- **Module**: domain grouping (Audio, UI, MIDI, USB, WiFi, BLE, HAL, Storage, Boot, Debug)
- **Component**: a specific thing within a module (FM6, Shell, Weather, Effects, OTA)
- **Upstream**: whose code a component was ported from (Felucca, Melodee, SLOOP, Optimist, FMLab; omit if native to modd)

### Modules (bottom to top)

1. **HAL** (header-only, register-level): `fm1_*.h` files for AC791N peripherals. Identical files shared; diverged files selected via Kconfig radio choices.
2. **Audio**: voice allocator, render pipeline, DMA half-buffer (128 samples @ 44.1kHz stereo). Contains components for Core, Engines, Effects, Sequencer, Scales, and Modulation. Three Core upstreams: Felucca (14 voices), SLOOP, Melodee (18 voice budget units, FPU).
3. **Engine Plugin** (`engine_t` vtable): the cleanest boundary. `note_on`, `render`, `amp`, `block`, `done` + optional fields. Melodee's 14-field superset is canonical. Engines compile in/out independently.
4. **MIDI, UI, Storage, Boot**: each a module with upstream-specific component implementations selected via Kconfig radio choices.
5. **USB**: device driver, USB Audio, host controller, mass storage.
6. **BLE**: optional module (Optimist upstream) for Bluetooth Low Energy; depends on HAL, enables BLE MIDI transport.
7. **Network** (`firmware/src/net/`, `CONFIG_LWIP`): the lwIP stack, HTTP client and LLM client, link-agnostic. Links add a `netif` on top: WiFi (`CONFIG_WIFI_LWIP`) today, USB ethernet / iPhone tethering (`CONFIG_USB_NET`) planned. `net_on_up()` is the "an interface got an address" hook; RPC and LLM depend on `LWIP`, not on a link.
8. **WiFi**: STA driver, WPA, the WiFi `netif`, weather client.
9. **Debug**: log buffer.

### Contracts (defined in `.scratch/GLUE.md`)

Thin compatibility headers (`modd_*.h`) that let components from different upstreams plug into modd. Each contract defines the interface (types, vtable, signatures) that both sides conform to.

| Contract | Header | Enables |
|---------|--------|---------|
| Note Dispatch | `modd_note_dispatch.h` | Mix any sequencer with any audio core |
| Engine Plugin | `modd_engine.h` | Any engine on any audio core |
| Effects Chain | `modd_fx.h` | Swap FX implementations |
| Storage | `modd_storage.h` | Tagged-blob project format |
| MIDI Routing | `modd_midi.h` | Swap MIDI implementations |
| Scales/Tuning | `modd_scales.h` | Swap scale sets |
| Debug DAP | `modd_debug.h` | NOINIT byte ring + USB SysEx debug protocol |
| Dock modules | `modd_dock.h` | fm1-dock `.dock` instruments: ABI tables, header/CRC/relocation; installed from a `dock_src_t` (USB disk today) and run one at a time |
| Lists | `modd_list.h` | One scrolling-list widget (modd theme `list.c`) over any source: a USB disk directory, the dock instruments, later an HTTP index; four control slots (short/long OCT-/OCT+) with glyph, label and default hints |

### The P_COUNT Problem

Each firmware defines different parameter counts and IDs. modd defines a `P_*` superset; unused params are stored but ignored. Engine params `P_E0..P_E7` are always the last 8.

## Directory Layout

```
Kconfig                     # root Kconfig (sources kconf/*.kconfig)
kconf/                      # per-module Kconfig menus (not kconfig/ due to macOS case-insensitive FS)
configs/                    # saved defconfigs per upstream preset
vendir.yml                  # pins for firmware/upstream/*, firmware/vendor/* and docs/lvgl (vendir.lock.yml = resolved SHAs)
firmware/
  hal/                      # HAL headers (common + per-variant)
  src/                      # source tree (future: populated from reference firmwares)
    modd.c              # unity root with CONFIG_* conditional includes
    engines.c               # engine registry (conditional includes)
    net/                    # Network module: lwip_core.c (lwIP source list), lwipopts.h, net.c (init, tick, net_on_up), http.c
    dock/                   # fm1-dock module host: dock.c (slots, flash install, start/stop, ISR hook), dock_disk.c (USB disk source)
  upstream/                 # vendored community firmwares, never patched (see vendir.yml)
  vendor/                   # vendored libraries: lwIP, FatFs (see vendir.yml)
tools/
  build/                    # build pipeline
    build.py                # full build entry point (genconfig + compile)
    genconfig.py            # .config -> build/<defconfig>/gen/config.h
    menuconfig.py           # Kconfiglib TUI wrapper
    fm1pkg_make.py          # .fwsc package builder
    mem_report.py           # memory report printed after every link (regions, audio arena, stacks, largest objects)
    lz4blk.py               # LZ4 block compression
    vendor.py               # vendir manifest helpers: present (build precondition), check (CI)
  device/                   # hardware interaction (USB-MIDI)
    fm1_midi.py             # shared MIDI discovery and 7-bit encoding
    hbdeploy.py             # fm1-dock SysEx transport library
    fm1log.py               # USB debug log reader (uses hbdeploy.py)
    fm1_install.py          # OTA firmware installer
    fm1_uboot_flash.py      # UBOOT mode flash via pyusb
    fm1_settings_reset.py   # factory reset via SysEx
  codegen/                  # build-time code generators
    gen_engine_tables.py    # engine preset tables
    gen_ui_weather.py       # weather colors and condition names
    gen_weather_art.py      # weather pixel-art sprites and sky gradients
    gen_openapi.py          # firmware/src/rpc/*.c + docs/api/openapi.overlay.yaml -> build/<defconfig>/openapi.json
    logo_convert.py         # boot logo converter (ink-on-cream splash)
  debug/                    # offline analysis tools
    ram_top.py              # RAM usage from linker map
    wifi_ind_timeline.py    # WiFi indicator log decoder
    wifi_sweep_diff.py      # register sweep differ
.scratch/tasks/todo/         # implementation plans and tasks
.scratch/                   # research data, examples, analysis (not shipped)
.product/                   # product specs and architecture docs
```

## Toolchain

- **Compiler**: JieLi clang 4.0.1 for pi32v2 (proprietary target, x86-64 binary). The build/test environment must have **one of**: (1) x86-64 architecture (native), (2) `qemu-x86_64` (user-mode emulation), or (3) Rosetta/binfmt_misc (Apple-silicon VMs). Runner selection is automatic: native on x86-64 Linux; on other Linux, `task build` tests the ld.so runner with an actual compile and falls back to `qemu-x86_64` if it segfaults; otherwise Docker (macOS, or `JIELI_DOCKER=1`). The vendor build shim (`.scratch/tools/vendor-build/tc-shim.sh`) follows the same logic. The non-native Linux runners need x86-64 glibc/libstdc++, looked up in this order: `JIELI_X86_GLIBC`/`JIELI_X86_STDCPP` (the `flake.nix` dev shell exports both on aarch64), `~/.fm1-toolchain/x86libs`, then the first match in the nix store.
- **Two toolchains**: Hardware defconfigs (modd, felucca) default to JieLi clang 4.0.1 (x86-64 binary, `build/<defconfig>/`). Emulator defconfigs (modd_emu) default to the open pi32v2 LLVM toolchain (`jvitkauskas/pi32v2-llvm`, clang + `ld.lld`, native binaries, `build/<defconfig>-open/`; release pinned by `OPEN_TOOLCHAIN_VERSION` in `Taskfile.yml`). `USE_OPEN_TOOLCHAIN=1` or `=0` in the shell overrides either default. CI and releases use JieLi. Paths in this file written as `build/<defconfig>/` mean the directory of the toolchain in use. Flashing an open-toolchain image needs `OPEN_TOOLCHAIN_FLASH_OK=1`: the open build boots on a real FM-1 (2026-11-20) but hardware smoke checks are not done. See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#toolchains-open-default-and-jieli).
- **Finding the toolchain**: `task build` sets `JIELI_TOOLCHAIN` and `AC79_SDK` automatically and downloads the toolchain if missing. In CI, `dimmkirr/setup-fm1-toolchain@v1` handles the same setup.
- **Toolchain paths** (check in order):
  1. `$JIELI_TOOLCHAIN` / `$AC79_SDK` env vars (if set, used as-is)
  2. `~/.fm1-toolchain/toolchain` and `~/.fm1-toolchain/AC79_SDK` (Taskfile default, installed by `task fm1-platform:download`)
  3. JieLi upstream default: `/opt/jieli/pi32v2` (JieLi SDK docs install location)
  The key binary to verify: `$JIELI_TOOLCHAIN/pi32v2/bin/clang`. The installed layout under `~/.fm1-toolchain/`:
  ```
  ~/.fm1-toolchain/
    toolchain/          # JIELI_TOOLCHAIN points here
      pi32v2/bin/       # clang, ld, ar, cc, LLVMgold.so
      common/bin/       # objdump, objcopy
    AC79_SDK/           # AC79_SDK points here (SDK headers, libs, linker scripts)
    x86libs/            # x86-64 glibc/libstdc++ for qemu/Rosetta runners
  ```
- **Target**: AC791N dual-core MCU, bare-metal, no OS
- **Audio**: 44.1kHz stereo, 128-sample half-buffer DMA, I2S codec
- **Flash**: SPI NOR for storage, projects, presets, OTA
- **Output**: `.fwsc` firmware package (lz4-compressed, signed)

## Working with Engines

Engines implement the `engine_t` vtable (defined in `firmware/adapters/modd_engine.h`, GPL-3.0-only, derived from the MIT `felucca_engine.h`). Required fields: `note_on`, `render`. Optional: `amp`, `block`, `done`, `desc`, `keys`, `alloc`, `legato`, `mono_key`, `post`, `cap`, `units`, `preset_make`. Felucca engines set extra optional fields to NULL/0.

The registry is `firmware/src/audio/engines/engines.c`: each engine `#include`s its source under a `CONFIG_ENGINE_*` guard, then adds its `ENG_*` to the `ENGINES[]` array.

### Engine source locations

**In modd (ready to compile):**

| Engine | Source | Vtable | Notes |
|--------|--------|--------|-------|
| FM6 FMLab | `firmware/src/audio/engines/fm6/fm6_fmlab.c` | `ENG_FM6` | Integer Q24, 20 factory voices |
| Analog | `firmware/upstream/felucca-engines/src/eng_analog.c` | `ENG_ANALOG` | MIT, SVF filter, 7 waveforms |
| Noise | `firmware/upstream/felucca-engines/src/eng_noise.c` | `ENG_NOISE` | MIT |
| Stub | `firmware/src/audio/engines/eng_stub.c` | `ENG_STUB` | Silent, UI testing |

**In upstream repos (need porting/wiring):**

| Engine | Source | Vtable | Blockers |
|--------|--------|--------|----------|
| FM6 Melodee | `firmware/upstream/melodee/firmware/src/eng_fm6.c` | `ENG_FM6` | FPU paths, `eng_fm6_rom.h`, `fm6_store.c` |
| Drum Melodee | `firmware/upstream/melodee/firmware/src/eng_drum.c` | `ENG_DRUM` | Includes `drum_808.c`, `drum_sid.c`, `x0x/drum909.c`; defines `NLANE` locally |
| Prophet | `firmware/upstream/melodee/firmware/src/eng_trio.c` (misleading name) | `ENG_PROPHET` | `prophet_patch.c`, `prophet_user.c`, `prophet_edit.h` |
| CZ-1 | `firmware/upstream/melodee/firmware/src/eng_cz.c` | `ENG_CZ` | `cz_patch.h`, `cz_panel.h`, `cz_edit.h`, `cz_store.c`, `cz_native.c`, `cz_bank.c` |
| SID | `firmware/upstream/melodee/firmware/src/eng_sid.c` | `ENG_SID` | Uses `eng_idx()` (not in modd), Melodee dsp paths |
| Trio | `firmware/upstream/felucca-engines/src/eng_trio.c` | `ENG_TRIO` | MIT, likely ready |
| LoFi | `firmware/upstream/felucca-engines/src/eng_lofi.c` | `ENG_LOFI` | MIT, likely ready |
| Wheel | `firmware/upstream/felucca-engines/src/eng_wheel.c` | `ENG_WHEEL` | MIT |
| Formant | `firmware/upstream/melodee/firmware/src/eng_formant.c` | ? | Melodee |
| Phase | `firmware/upstream/melodee/firmware/src/eng_phase.c` | ? | Melodee |
| Physical | `firmware/upstream/melodee/firmware/src/eng_phys.c` | ? | `phys_dsp.c`, `phys_symp.c` |

**Felucca-engines repo** (`firmware/upstream/felucca-engines/src/`): MIT-licensed standalone engines. These use `felucca_engine.h` (compatible with `modd_engine.h`) and `dsp.c` helpers. Most can be wired directly.

**Melodee upstream** (`firmware/upstream/melodee/firmware/src/eng_*.c`): GPL engines that live inside Melodee's unity build. They often depend on Melodee-specific globals (`eng_idx()`, `drum_track()`, Melodee `dsp.c`). Each needs dependency analysis before wiring.

**DSP helpers**: `firmware/src/audio/engines/dsp.c` (modd's shared DSP: `tsvf_t`, `clamp`, `pitch_inc`, polyBLEP). Felucca-engines share a compatible `dsp.c` in their repo. Melodee's `dsp.c` is a superset with FPU paths.

## Reference Firmwares

Upstream trees are fetched with vendir and are not committed: `vendir.yml` pins each tree to a tag or commit, `vendir.lock.yml` records the resolved SHA. `task vendor:ensure` (a dependency of `task build` and `task test:unit`) fetches them at the locked commits when missing; `task vendor:sync` re-resolves the pins and rewrites the lock. Never edit files under `firmware/upstream/` or `firmware/vendor/`: adapt them through `firmware/adapters/` and project-owned config headers. Pins, licenses and uses: `THIRD_PARTY.md`.
- Felucca full firmware: `firmware/upstream/felucca/` (research copy also at `.scratch/examples/felucca/`)
- Felucca engines: `firmware/upstream/felucca-engines/`
- Melodee: `firmware/upstream/melodee/`
- SLOOP: `firmware/upstream/sloop/`
- FMLab: `firmware/upstream/fmlab/`
- Skiff: `firmware/upstream/skiff/`
- fm1-dock: `firmware/upstream/fm1-dock/`
- Optimist (BLE): `firmware/upstream/optimist/`
- lwIP: `firmware/vendor/lwip/`; FatFs: `firmware/vendor/fatfs/` (project config in `firmware/src/udisk/`)
- LVGL v9.6.0: `firmware/vendor/lvgl/` (project config in `firmware/src/ui/themes/lvgl/lv_conf.h`); its manual and examples at the same pin: `docs/lvgl/` (see [LVGL](#lvgl))

Research data: `.scratch/fw-test.json` (firmware catalog), `.scratch/kconfig-subsystems.json` (layer analysis), `.scratch/GLUE.md` (contract specs).

## LVGL

The `lvgl` theme (`CONFIG_UI_LVGL`, `firmware/src/ui/themes/lvgl/`, defconfigs `modd` and `modd_emu`; the main theme) is drawn by LVGL 9.6 widgets. **Before any LVGL work (a widget, a style, the theme, input, the display port, `lv_conf.h`), read the vendored manual.** Do not work from LVGL 8 knowledge or from tutorials on the web: names, signatures and behaviour differ.

- Manual: `docs/lvgl/docs/src/**/*.mdx` (a widget: `widgets/<name>.mdx`; styles and themes: `common-widget-features/styles/`; encoder and groups: `main-modules/indev/`; subjects: `main-modules/data_binding/`). Examples: `docs/lvgl/examples/`.
- Headers: `firmware/vendor/lvgl/include/lvgl/`. Where the manual and a header disagree, the header is what compiles.
- Project guide: [docs/ui/LVGL.md](docs/ui/LVGL.md). Where to edit, the rules a theme has to follow on the FM-1 (one task, heap, stack, density, encoder input), how to preview and measure, community projects.
- LVGL 9.6 has no XML engine (removed in 9.5.0; XML is LVGL Pro, commercial). Pages are C.
- `docs/lvgl/` and `firmware/vendor/lvgl/` are fetched by vendir and never edited. Changing `lv_conf.h` recompiles the library (483 files, about 3 minutes).

## Testing

Two test layers (host unit tests and direct-API tests), all via Taskfile. See [docs/TESTING.md](docs/TESTING.md)
for mise setup, commands, and screenshot locations.

### Unit tests (host, no hardware)

Python tests cover configuration, packaging, the build pipeline and tooling:

```
task test:unit                    # Python unit tests (genconfig, defconfigs, tooling)
```

Test files live in `tests/`. Three upstream preset tests skip until their
defconfigs are implemented.

### Direct-API tests (fm1-live + HTTP)

```
task test:api                           # builds modd_emu, runs tests/**/api_*_test.py and tests/ui/themes/lvgl/api/
```

`ApiTestCase` (`tests/api_fixtures.py`) boots fm1-live per test class with a port forward to the firmware's REST API and drives everything over HTTP GET: pages (`/modd/<page>`, `/api/page`), panel input ORed into the key scan (`/api/input/...`, `CONFIG_RPC_INPUT`), the audio output (`/api/audio/capture`, `CONFIG_RPC_AUDIO_TAP`), memory (`/api/mem/...`, `CONFIG_RPC_MEM`), LCD and log. Routes live in `API_ROUTES[]` (`firmware/src/rpc/api_ctl.c`); `task build` generates the OpenAPI 3.1 document from the C (`tools/codegen/gen_openapi.py`, prose from `docs/api/openapi.overlay.yaml`) into `build/<defconfig>/openapi.json`, served at `GET /openapi.json` (`CONFIG_RPC_OPENAPI`). The generator stops the build on a handler it cannot read and says what to change; rules in docs/TESTING.md, "Writing a REST handler". Boot, audio, engine, USB-safety and weather tests live here; `test:unit` skips them (except `tests/usb/usb_otg_presence_safety_test.py`, which runs under both). fm1-emu's NAT allows ~30 connections per 30 s, so the client spaces its requests (see docs/TESTING.md, "Direct-API tests").

### CI

GitHub Actions (`.github/workflows/ci.yml`) has two jobs. The `test` job installs the tools
pinned in `.mise.toml` with `mise install`, syncs `uv.lock`, runs `task test:unit`, installs the
SDK and compiler with `dimmkirr/setup-fm1-toolchain@v1`, and builds the production `modd`
firmware with `task build` (JieLi compiler, `build/modd/modd.fwsc`). Its `tests/results/` is kept
as the `test-results` artifact, and for releases it packages the release bundle from that build.
The `api` job runs `task test:api` and is off unless the repository variable `FM1_EMU_REF` names
a ref of `DimmKirr/fm1-emu` that has the `fm1-live` port forward and NAT fix (not pushed or
released yet). When it is on, the job builds `fm1-live` from that ref with cargo, starts Xvfb on
`:99` and runs the API suite. It never blocks the run (`continue-on-error`), does not run for
release tags, keeps `tests/results/` as the `api-results` artifact, and on `main` push/manual
runs publishes the screenshots to the `ci-screenshot` prerelease (it requests `contents: write`;
the `test` job needs only `contents: read`). `release.yml` handles `v[0-9]*` tags by calling the
same workflow, then publishing the tested firmware, `modd.config` (the resolved configuration of
that build), source, licenses and checksums as a prerelease. Failed verification never publishes
a versioned release.

### Screenshot generation

`tools/codegen/gen_ui_screenshots.py` captures per-page screenshots for each UI theme using the fm1-live headless emulator (modd and lvgl themes) or the WASM emulator via Playwright in Chromium (the Felucca theme). Run via Taskfile:

```
task docs:themes:ui:screenshots          # all themes
python tools/codegen/gen_ui_screenshots.py modd          # modd only
python tools/codegen/gen_ui_screenshots.py modd --no-build   # skip firmware build
```

The modd theme uses fm1-live with `--usb ethernet --net internet` for network screenshots (Settings NET, Weather) and a 34MB FAT32 disk image (`tools/codegen/screenshot_disk.img`) for Files/Disk screenshots. The disk image contains prebuilt `.dock` modules from fm1-dock.

Output and auto-injection:
- Screenshots go to `docs/ui/themes/<theme>/screenshots/` as both numbered (`00_home.png`) and stable-named (`home.png`) files.
- `THEME.md` gallery: the generator injects a full screenshot table between `<!-- theme-gallery:start/end -->` markers in `docs/ui/themes/<theme>/THEME.md`.
- `README.md` hero section: the generator injects selected screenshots between `<!-- theme-screenshots:start/end -->` markers. Hero shots and their order are defined by `HERO_SHOTS` in the script.
- Both injections happen automatically on every run.

Environment: set `FM1_EMU_ROOT` (in `devcell.toml [env]` or shell) to the fm1-emu worktree root so the generator finds fm1-live.

## Conventions

- Unity build: all `.c` files are `#include`d into one translation unit. All functions are `static`.
- HAL is header-only: register macros, no `.c` files.
- Kconfig symbols use `CONFIG_` prefix. Engine symbols: `CONFIG_ENGINE_*`. HAL upstream variants: `CONFIG_HAL_*_FELUCCA/SLOOP/MELODEE`.
- Voice budget is a shared resource pool. Engine cost is measured in VBUDGET units (Prophet: 2-3, FM6: 2, CZ-1: 1, etc.).
- License: all modd code is GPL-3.0-only. Upstream `felucca_engine.h` and Felucca-Engines are MIT; `modd_engine.h` derives from it under GPL-3.0-only with the MIT notice kept. Every file copied or adapted from an upstream carries an `Origin:` line pinned to the upstream commit; the list is in `THIRD_PARTY.md`.

## Moving Modules Around

Three levels of module assembly, from simplest to most involved.

### 1. Enable or disable a module

Toggle a `CONFIG_*` symbol in `task config` (or edit `.config` directly) and rebuild. Examples:

- Enable WiFi: set `CONFIG_WIFI=y`
- Add an engine: set `CONFIG_ENGINE_ANALOG=y`
- Disable BLE: set `CONFIG_BLE=n`

The Kconfig dependency system prevents invalid combinations (e.g. enabling an engine without its audio core).

### 2. Swap an upstream implementation

Many modules have radio choices that select which upstream provides the implementation. The adapter contract (`modd_*.h`) stays the same; only the backing code changes.

Examples in `modd.c`:

```c
#if defined(CONFIG_HAL_SYS_MELODEE)
  #include "hal/melodee/fm1_sys.h"
#elif defined(CONFIG_HAL_SYS_FELUCCA)
  #include "hal/felucca/fm1_sys.h"
#elif defined(CONFIG_HAL_SYS_SLOOP)
  #include "hal/sloop/fm1_sys.h"
#endif
```

To swap: change the Kconfig radio choice, rebuild. The adapter contract guarantees the rest of the firmware sees the same interface.

### 3. Add your own module

1. Write a `.c` file that conforms to the relevant `modd_*.h` contract. For engines, implement `engine_t` (required: `note_on`, `render`; see [Working with Engines](#working-with-engines)).
2. Add a `CONFIG_*` Kconfig symbol in the appropriate `kconf/*.kconfig` file.
3. Add a `#include` guarded by that symbol in `modd.c` (or `engines.c` for engines).
4. Build and test: `task build && task test:api`.

For the mock-first porting strategy (start with stub data, wire real components later), see [docs/PORTING.md](docs/PORTING.md).

### Combining modules from multiple firmwares

If you want modules from multiple firmwares in one build (e.g. one engine from Felucca, one engine from Melodee, a UI theme from SLOOP), the recommended path is to add them as modules via adapters to modd. Each module conforms to its adapter contract independently, so modules from different upstreams coexist as long as they fit the voice budget and satisfy Kconfig dependencies.

Upstream firmwares are vendored under `firmware/upstream/` via vendir and are never modified. To port a component, copy or adapt it into `firmware/src/` (or `firmware/adapters/`), wire it through the relevant contract, and add its `CONFIG_*` guard. The [Engine source locations](#engine-source-locations) tables show what is ready and what needs porting.

## Adding Upstream Firmware

To add a new community FM-1 firmware to the vendor build system, follow these steps. All FM-1 firmwares share the same JieLi AC791N toolchain and a similar `build.sh` + `tools/build.py` pattern. The tasks live in `firmware/upstream/Taskfile.yml` (included from the root `Taskfile.yml` under the `vendor:` namespace, so e.g. its `build:felucca` task runs as `task vendor:build:felucca`), not in the root Taskfile itself. `firmware/upstream/` is gitignored except for that one Taskfile.

### 1. Add to vendir.yml

Add a new entry under the `# --- Community FM-1 firmwares` section:

```yaml
- path: firmware/upstream/<name>
  contents:
  - path: .
    git:
      url: https://github.com/<owner>/<repo>
      ref: <tag-or-commit>
    excludePaths: [".git/**"]
```

Pin to a release tag when available (e.g. `v2.2`), or a full commit SHA when there is no suitable tag.

### 2. Sync the vendor tree

```sh
vendir sync --directory firmware/upstream/<name>
```

This fetches the repo and updates `vendir.lock.yml` with the resolved SHA. Commit both `vendir.yml` and `vendir.lock.yml`.

### 3. Inspect the build

Check the new upstream's build system:

- **`build.sh`**: all FM-1 firmwares have one. Read it to find the output filename and any special dependencies.
- **Output filename**: most produce `felucca.fwsc` (the default FM-1 package name) or `<name>.fwsc`. Check `tools/build.py` for the exact name (look for `.fwsc`).
- **Pillow + raqm**: if the build generates font headers from TTF files (look for `gen_aa_font.py`, `ImageFont`, `layout_engine=...RAQM`), the upstream needs Pillow with raqm support. Most do.
- **Submodules**: check `.gitmodules` or `firmware/engines/`. Felucca and SLOOP need the felucca-engines submodule; the `_build` task symlinks it automatically. Newer firmwares (Skiff, fm1-dock) are typically self-contained.
- **Special config**: some upstreams (e.g. Optimist) have non-standard config systems or overflow default flash budgets. Check if a specific config profile is needed.

### 4. Add Taskfile tasks

Three tasks are needed for each upstream, plus an entry in the umbrella.

All of the following tasks go in `firmware/upstream/Taskfile.yml`, not the root Taskfile. Task names below omit the `vendor:` prefix that the root's include adds (e.g. `build:felucca` in that file runs as `task vendor:build:felucca`).

**If the firmware fits the standard pattern** (has `build.sh`, uses JieLi toolchain, needs raqm): delegate to `_build`:

```yaml
build:<name>:
  desc: "Build upstream <Name> for testing (→ firmware/upstream/<name>/build/<output>.fwsc)"
  cmds:
    - task: _build
      vars: { UPSTREAM: <name>, OUTPUT: '<output>.fwsc', CLI_ARGS: '{{.CLI_ARGS}}' }
```

If the output is `<name>.fwsc`, you can omit `OUTPUT` (it defaults to `<upstream>.fwsc`). If the output is `felucca.fwsc`, set `OUTPUT: 'felucca.fwsc'`.

**If the firmware has a non-standard build** (e.g. Optimist's custom config system): write a custom task following the `build:optimist` pattern. Copy the env, deps, status, and shim setup, then adapt the build command.

**Emulator variant:**

```yaml
build:<name>:emulator:
  desc: Build upstream <Name> for testing + ensure emulator is ready
  deps: [fm1-emulator:download:wasm]
  cmds:
    - task: build:<name>
      vars: { CLI_ARGS: '{{.CLI_ARGS}}' }
```

**Flash task** (calls `fm1_install.py` directly, no modd build dependency):

```yaml
flash:<name>:
  desc: Flash upstream <Name> to FM-1 (run vendor:build:<name> first)
  vars:
    FIRMWARE: firmware/upstream/<name>/build/<output>.fwsc
  preconditions:
    - sh: test -f '{{.FIRMWARE}}'
      msg: '{{.FIRMWARE}} not found. Build it first: task vendor:build:<name>'
  cmds:
    - uv run --quiet --group flash python tools/device/fm1_install.py '{{.FIRMWARE}}' --force --yes {{.CLI_ARGS}}
```

**Add to the umbrella** in `build`:

```yaml
build:
  desc: Build all upstream firmwares for testing
  cmds:
    # ... existing entries ...
    - task: build:<name>
```

### 5. Update documentation

- Add a bullet to the **Reference Firmwares** list in this file.
- Add the entry to `THIRD_PARTY.md` with license and provenance.
- Update `docs/DEVELOPMENT.md` if the new upstream has special build requirements.

### 6. Test

```sh
task vendor:build:<name>              # should produce the .fwsc
task vendor:build:<name>:emulator     # should also download/check emulator
```

On macOS, the build runs through Docker (each upstream's `build.sh` gates on it). In the Linux container (aarch64), the `_build` task sets up a toolchain shim that runs the x86-64 JieLi clang through an explicit `ld.so` loader (`.scratch/tools/vendor-build/tc-shim.sh`).

### Key constraints

- Never modify files under `firmware/upstream/`, except `firmware/upstream/Taskfile.yml` itself (the one file excluded from `.gitignore`). The vendored trees are managed by vendir and will be overwritten on sync.
- `build.sh` may not be executable after vendir sync. The `_build` task uses `bash build.sh` instead of `./build.sh`.
- `vendir sync` replaces the entire target directory, wiping any build output. Always build after syncing.
- The `_build` preconditions are guarded with `test -f 'build/<output>' ||` so they skip when the firmware is already built (avoids raqm failures on macOS when the output exists from a container build).
- Tasks inside `firmware/upstream/Taskfile.yml` cannot reference the root Taskfile's `fm1-platform:*` or `fm1-emulator:*` tasks directly (Taskfile resolves deps within the include's own namespace). That file carries its own local `includes:` for both, which is why `fm1-platform:download:sdk` resolves there.
