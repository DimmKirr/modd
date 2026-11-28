# Development

## Build

```sh
task build
```

Produces `build/modd/modd.fwsc`. Requires the JieLi toolchain (downloaded
automatically on first run). `task build` delegates to `build:live`, which
caches on source checksums, one per defconfig, and skips when nothing changed
since that defconfig was last built.

### Toolchains: JieLi (hardware default) and open (emulator default)

The build has two compilers. **Hardware defconfigs** (modd, felucca) default to
JieLi clang 4.0.1 and write to `build/<defconfig>/`. **Emulator defconfigs**
(modd_emu) default to the open pi32v2 LLVM toolchain
(`jvitkauskas/pi32v2-llvm`, clang and `ld.lld`) and write to
`build/<defconfig>-open/`. `USE_OPEN_TOOLCHAIN=1` or `=0` in the shell
overrides either default:

```sh
task build                                                # JieLi (hardware default), build/modd/
task modd:build:modd_emu                                  # open (emulator default), build/modd_emu-open/
USE_OPEN_TOOLCHAIN=1 task build                           # force open for hardware, build/modd-open/
USE_OPEN_TOOLCHAIN=0 task modd:build:modd_emu             # force JieLi for emulator, build/modd_emu/
```

The variable applies to every task that builds or reads a build (`task flash`,
`task test:api`, `task modd:build:<name>`, `task docs`). Each toolchain has its
own output directory and build cache, so the two do not overwrite each other.
Only `0`, `1` and unset are accepted. The defaults live in the Taskfiles:
`build.py` and the test fixtures, run without Task, still read the variable as
"1 means open" and use JieLi when it is unset.

The open release is pinned by `OPEN_TOOLCHAIN_VERSION` in `Taskfile.yml`. A build
installs it into `~/.fm1-toolchain/open/<tag>` when it is missing. Run
`task toolchain:open:download` to install it ahead of time. Set `OPEN_TOOLCHAIN`
to use another install root. The binaries run natively on Linux and macOS. No
qemu, Rosetta, Docker or x86-64 glibc is needed. The JieLi SDK (`AC79_SDK`) is
still required, and the build still downloads the JieLi toolchain as a dependency.

- Flashing an open-toolchain image is refused unless `OPEN_TOOLCHAIN_FLASH_OK=1`
  is set: the open build boots on a real FM-1 but the hardware smoke checks are
  not done. `task flash` uses JieLi by default, so this gate only fires when
  `USE_OPEN_TOOLCHAIN=1` is set explicitly.
- `CONFIG_WIFI_NONFREE` is not supported with the open toolchain. Build it with
  JieLi (the default for hardware defconfigs).
- The memory report reads the lld map and applies the same free-RAM check. The
  region sizes come from the linker script, because an lld map does not list them.
- CI and releases use JieLi (`.github/workflows/ci.yml`) and ship JieLi-built
  firmware.

Status on 2026-11-20: a `modd_emu` open build boots in fm1-live and passes the
API test suite (67 tests). The `modd` open build `dev-20261120T091957Z` was flashed
to a real FM-1: it boots and runs for a while, then stops on the red FAULT screen. A JieLi
build of the same sources faults too, so the fault is in the source of that day, not in
the compiler. No open build of a healthy source tree has been checked on hardware yet.

## Environment

The environment has three layers. Each owns one thing:

| Layer | Owns | Files |
|-------|------|-------|
| Nix dev shell | System libraries: `qemu-x86_64`, libusb, FriBiDi, and on aarch64 the x86-64 glibc and libstdc++ the JieLi compiler runs against | `flake.nix`, `flake.lock` |
| mise | Tool versions: Task, uv, Python, vendir | `.mise.toml` |
| uv | Python dependencies in `.venv` | `pyproject.toml`, `uv.lock` |

Running a task through all three always works:

```sh
nix develop -c mise exec -- task docs
```

With mise activated in your shell the `mise exec --` prefix is optional, and
inside an interactive `nix develop` shell so is the first part.

When each layer matters:

- **mise**: the remote Taskfile includes need the pinned Task. An older `task`
  earlier on `PATH` (the devcell container ships 3.45 in its Nix profile) stops
  with `Remote taskfiles are not enabled` unless `TASK_X_REMOTE_TASKFILES=1` is
  set.
- **Nix dev shell, FriBiDi**: the font generators (`gen_aa_font.py`) need
  Pillow with raqm, and Pillow's wheel loads `libfribidi.so.0` at runtime. If it
  is missing, or is built for another architecture, Pillow silently reports
  `raqm: False` and the generator fails with
  `not supported without libraqm`. Fonts are rendered on the first build of a
  defconfig (the headers land in `build/<defconfig>/gen/` and are reused
  afterwards), by `task docs`, and by the upstream firmware builds. Check with:

  ```sh
  uv run python -c "from PIL import features; print(features.check('raqm'))"
  ```

- **Nix dev shell, x86-64 libraries**: on aarch64 the dev shell exports
  `JIELI_X86_GLIBC` and `JIELI_X86_STDCPP`, which `tools/build/build.py` reads
  before it falls back to `~/.fm1-toolchain/x86libs` or a search of the nix
  store. A loader from one glibc with libraries from another fails with
  `symbol lookup error ... __pointer_chk_guard`.

Rebuilds with the generated headers in place, `task test:unit` and the API
tests do not need the dev shell.

## Before committing

```sh
nix develop -c mise exec -- task docs         # regenerate diagrams and theme screenshots
nix develop -c mise exec -- task docs:check   # generated docs match their sources
mise exec -- uv lock --check                  # uv.lock matches pyproject.toml
mise exec -- task test:unit                   # Python unit tests
```

`task docs` takes 5 to 6 minutes on a 2-CPU container. About 80 s is two
firmware builds (the compiler is an x86-64 binary under emulation), 80 s is the
Felucca capture in the browser emulator, and 160 s is five modd shots that each
boot fm1-live and wait for something real: the ethernet link, the WiFi join,
the weather fetch, and the USB disk twice. The other fifteen modd shots take
under 10 s together.

## Flashing firmware

Two methods are available. Both require a built `.fwsc` package.

### USB-MIDI SysEx (`task flash`)

The normal update path. The running firmware receives the package over
USB-MIDI SysEx, boots the update loader, and the loader writes flash.

```sh
task flash
```

Prerequisites:
- The FM-1 must be connected via USB and enumerated as a MIDI device.
- `task flash:debug` lists visible MIDI ports and watches for the device.

How it works:
1. `fm1_install.py` finds the FM-1 MIDI port (matches "FM-1", "Felucca", or
   "Update" in the port name).
2. Sends a SysEx upgrade command. The firmware reads part of the package,
   validates it, and reboots into the update loader.
3. The update loader ("Felucca Update" MIDI port) requests data blocks via
   SysEx. The script replies with the corresponding chunks.
4. After the last block, the loader writes flash and reboots. The script
   waits for the device to reappear and verifies the installed identity.

Troubleshooting:
- **No MIDI port visible**: run `task flash:debug`. If no device appears,
  check the USB cable. If only "FM-1" appears but flash fails, the device
  may not be entering update mode: try `task flash:uboot` instead.
- **"MIDI inputs: (none)"**: USB polling may not be running. Ensure
  `usb_poll()` is called from Timer5 ISR (not the main loop).
- **Install hangs at "waiting for loader"**: the firmware did not enter
  update mode. Power-cycle and retry, or use UBOOT mode.

### UBOOT mode (`task flash:uboot`)

Direct flash write via the ROM-stage USB bootloader. Bypasses the running
firmware entirely: works even when the firmware is broken or MIDI does not
enumerate.

```sh
task flash:uboot
```

Entering UBOOT mode:
1. Slide the power switch to **OFF**.
2. Connect USB cable to the host (if not already connected).
3. Hold **OCT-** only (do NOT hold OCT+; both buttons together is
   the calibration gesture and will not trigger rescue).
4. Slide the power switch to **ON** while holding OCT-.
5. Keep holding for ~5 seconds until the host detects a new USB device
   (VID `4C4A`, PID `8057`, product "WL80UBOOT1.00").

The script:
1. Prompts for confirmation (skip with `--yes`).
2. Uploads `wl82loader.bin` to the SoC RAM (MengLi-ciphered via SCSI
   vendor commands over USB Bulk-Only Transport).
3. Jumps to the loader.
4. Reads the existing flash content in the application area
   (`0x4000`..`0x93000`).
5. Compares sectors (4 KiB each), erases and writes only changed ones.
6. Verifies each written sector by reading back.
7. Reboots the device.

Requirements:
- `libusb` must be installed (`brew install libusb` on macOS,
  `apt install libusb-1.0-0` on Linux, or via nix).
- macOS needs `sudo` because the kernel claims USB mass storage devices.

Troubleshooting:
- **"No backend available"**: libusb is not found. Install it or check that
  the nix store path is reachable.
- **"Access denied"**: run with `sudo task flash:uboot` (macOS).
- **Device not found**: ensure the FM-1 is in UBOOT mode (hold OCT-
  only while sliding power on). Check `lsusb` or `system_profiler SPUSBDataType`
  for VID `4C4A`.

### Choosing a method

| | SysEx (`task flash`) | UBOOT (`task flash:uboot`) |
|---|---|---|
| Requires working firmware | Yes | No |
| Requires MIDI enumeration | Yes | No |
| Needs sudo (macOS) | No | Yes |
| Speed | ~30 s | ~15 s (diff-based) |
| Risk | Low (loader validates) | Low (header area untouched) |
| Use when | Normal development | Firmware is broken, MIDI not working |

### Flash endurance

The FM-1 uses an ESMT SPI NOR flash (JEDEC `0x856014`, 1 MB). Each 4 KiB
sector supports ~100,000 program/erase cycles. The UBOOT flasher only
writes changed sectors, so incremental builds during development use
very few cycles per update.

## Building upstream firmwares (for testing)

When porting or adapting upstream code, it helps to build and compare against
the original. These tasks build a clean upstream firmware from its vendored
source using modd's JieLi toolchain:

```sh
task vendor:build                   # build all upstream firmwares
task vendor:build:felucca           # → firmware/upstream/felucca/build/felucca.fwsc
task vendor:build:melodee           # → firmware/upstream/melodee/build/melodee.fwsc
task vendor:build:sloop             # → firmware/upstream/sloop/build/felucca.fwsc
task vendor:build:fmlab             # → firmware/upstream/fmlab/build/fmlab.fwsc
task vendor:build:skiff             # → firmware/upstream/skiff/build/felucca.fwsc
task vendor:build:fm1-dock          # → firmware/upstream/fm1-dock/build/fm1-dock.fwsc
task vendor:build:optimist          # → firmware/upstream/optimist/build/felucca.fwsc
task vendor:build:felucca:emulator  # build + ensure WASM emulator is ready
task vendor:flash:felucca           # flash a pre-built upstream to FM-1
```

Available upstreams: `felucca`, `melodee`, `sloop`, `fmlab`, `skiff`,
`fm1-dock`, `optimist`. Output stays in each upstream's own `build/`
directory. The upstream sources under `firmware/upstream/` are never modified.

The build requires Pillow with raqm support (for upstream font generation),
which in turn needs the FriBiDi library at runtime; the Nix dev shell provides
it (see [Environment](#environment)). The task checks for raqm before it
starts. On macOS, build in the container first, then flash from the host:

```sh
# container:  task vendor:build:felucca
# macOS:      task vendor:flash:felucca
```

## Default build behaviour

The `modd` and `modd_emu` defconfigs enable WiFi, USB ethernet and the weather
client. Each can be disabled in Kconfig. When enabled, the link auto-starts at
boot and stays up while any consumer needs it (`net_need.c`, `wifi/need.c`):
the weather client, the LLM client and the Settings NET page each register
interest, and the link is brought down only when no consumer remains.

## Debugging

### USB error codes on LCD

Navigate to the FILES page (it switches the port to USB host mode while it is
open, with DISK on in Settings > HW). If a USB disk connection fails,
the LCD shows `USB:ERR -N` where N is the error code:

| Code | Meaning |
|------|---------|
| -1 to -7 | USB enumeration error (step number) |
| -11 to -16 | MSC (Mass Storage Class) init error, offset by -10 |

### Fault screen

Stack overflow canaries guard each task stack. On a fault (illegal instruction,
bus error, stack overflow) the LCD shows a red FAULT screen with the exception
type, faulting PC, saved registers and the tail of the debug log ring. The same
information is printed to the debug log. A UBOOT reflash recovers from any
fault in the application area that makes the firmware unbootable.

### Flash debug

```sh
task flash:debug
```

Lists all visible MIDI ports and watches for FM-1 or update loader
devices for 10 seconds.
