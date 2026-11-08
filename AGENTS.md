# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

modd is a modular firmware framework for the M-VAVE FM-1 synthesizer (JieLi AC791N SoC, pi32v2 architecture). It brings modules from any FM-1 firmware into one configurable source tree. Three community upstreams are vendored today: **Felucca** (hugelton), **Melodee** (keremimo), and **SLOOP** (isod89). Any FM-1 firmware that builds with the JieLi toolchain can be added the same way (see [Adding Upstream Firmware](#adding-upstream-firmware)). Everything in modd is GPL-3.0-only; the upstream Felucca-Engines interface (`felucca_engine.h`) and engines are MIT.

## Build System

Kconfiglib-based configuration. All commands go through [Taskfile](https://taskfile.dev), which wraps the JieLi toolchain env vars (`JIELI_TOOLCHAIN`, `AC79_SDK`) automatically:

```
task build                              # build modd firmware (alias for build:live)
task build -- --defconfig wifi_debug    # build a specific defconfig from configs/
task build:live                         # build with source-change caching (the real build target)
task flash                              # build if sources or .env changed, then flash over USB-MIDI
task config                             # interactive menuconfig
task config:generate:defconfig -- melodee  # load a preset defconfig and generate config.h
task config:generate:local                # .config -> build/gen/config.h
task test                               # run all tests (unit + e2e)
task test:unit                          # Python unit tests only
task test:e2e                           # emulator + Playwright e2e tests
task vendor:build                       # (testing) build all upstream firmwares
task vendor:build:felucca               # (testing) build clean upstream Felucca
task vendor:flash:melodee               # (testing) flash pre-built upstream Melodee
```

**Important:** always use `task build` (not `python tools/build.py` directly) because the Taskfile sets required env vars for the JieLi toolchain. If calling `build.py` directly, you must set `JIELI_TOOLCHAIN` and `AC79_SDK`.

`task build` delegates to `build:live`, which has source-change caching (skips when sources and `.env` are unchanged). Use `build:live` only when you need to reference the cacheable target directly (e.g. as a dependency).

### .config and defconfigs

`.config` is a symlink to `configs/modd_defconfig`, the main development profile. Do not replace it with a regular file. To build with a different defconfig from `configs/`, pass `--defconfig <name>` (without the `_defconfig` suffix):

```
task build -- --defconfig wifi_debug    # builds with configs/wifi_debug_defconfig
```

Generated files go to `build/gen/config.h`.

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
7. **WiFi**: STA driver, lwIP, WPA, weather client, CDP server.
8. **Debug**: log buffer.

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

### The P_COUNT Problem

Each firmware defines different parameter counts and IDs. modd defines a `P_*` superset; unused params are stored but ignored. Engine params `P_E0..P_E7` are always the last 8.

## Directory Layout

```
Kconfig                     # root Kconfig (sources kconf/*.kconfig)
kconf/                      # per-module Kconfig menus (not kconfig/ due to macOS case-insensitive FS)
configs/                    # saved defconfigs per upstream preset
vendir.yml                  # pins for firmware/upstream/* and firmware/vendor/* (vendir.lock.yml = resolved SHAs)
firmware/
  hal/                      # HAL headers (common + per-variant)
  src/                      # source tree (future: populated from reference firmwares)
    modd.c              # unity root with CONFIG_* conditional includes
    engines.c               # engine registry (conditional includes)
  upstream/                 # vendored community firmwares, never patched (see vendir.yml)
  vendor/                   # vendored libraries: lwIP, FatFs (see vendir.yml)
tools/
  build/                    # build pipeline
    build.py                # full build entry point (genconfig + compile)
    genconfig.py            # .config -> build/gen/autoconf.h
    menuconfig.py           # Kconfiglib TUI wrapper
    fm1pkg_make.py          # .fwsc package builder
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

- **Compiler**: JieLi clang 4.0.1 for pi32v2 (proprietary target, x86-64 binary). Runner order in `tools/build/build.py`: native on x86-64 Linux; on other Linux, direct execution through an x86-64 `ld.so` when the kernel runs x86-64 ELFs itself (Rosetta on Apple-silicon VMs, binfmt_misc), then `qemu-x86_64`; otherwise Docker (macOS, or `JIELI_DOCKER=1`). The two Linux fallbacks need x86-64 glibc/libstdc++: `~/.fm1-toolchain/x86libs` `JIELI_X86_GLIBC`/`JIELI_X86_STDCPP`, or the nix store.
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

Research data: `.scratch/fw-test.json` (firmware catalog), `.scratch/kconfig-subsystems.json` (layer analysis), `.scratch/GLUE.md` (contract specs).

## Testing

Two test layers, all via Taskfile. See [docs/TESTING.md](docs/TESTING.md)
for mise setup, commands, and screenshot locations.

### Unit tests (host, no hardware)

Python tests cover configuration, packaging, and emulator control helpers:

```
task test:unit                    # Python unit tests (genconfig, defconfigs, tooling)
```

Test files live in `tests/`. Three upstream preset tests skip until their
defconfigs are implemented.

### E2E tests (emulator + Playwright)

Browser-based tests using the fm1-emu WASM emulator and Playwright. No hardware needed:

```
task test:e2e                     # downloads emulator, builds firmware, runs e2e suite
```

E2E tests inherit from `E2ETestCase` (in `tests/e2e_fixtures.py`), which:
- Starts a local HTTP server serving the emulator
- Launches headless Chromium via Playwright
- Provides `controls(page)` for named encoders, buttons, and notes
- Provides helpers: `push(page, key)`, `long_push(page, key)`, `send_enc(page, enc, clicks)`, `screenshot(page, name, desc)`
- Saves panel-only screenshots and logs in `tests/results/<timestamp>-<module>/`

A typical E2E test uploads the built `.fwsc` into the emulator, waits for boot, then interacts with the virtual panel (buttons, encoders) and asserts LCD canvas state.

### CI

GitHub Actions (`.github/workflows/ci.yml`) installs the tools pinned in
`.mise.toml` with `mise install`, syncs `uv.lock`, and runs both layers through
Taskfile. CI uses `dimmkirr/setup-fm1-toolchain@v1` and
`dimmkirr/setup-fm1-emulator@v1`; local Taskfile downloads remain the fallback.
E2E results are retained as an Actions artifact. The test job also publishes
captured screenshots to the `ci-screenshot` prerelease on `main` push/manual
runs, including after E2E failures. It requests `contents: write`; publication
is disabled for pull requests, but same-repository PR jobs can receive that
permission (fork PR tokens are normally downgraded to read-only).
`release.yml` handles `v[0-9]*` tags by calling the same CI workflow, then
publishing the tested firmware, source, licenses, checksums, and screenshots
as a prerelease. Failed verification never publishes a versioned release.

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
4. Build and test: `task build && task test:e2e`.

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
