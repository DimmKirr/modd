# Patches to upstream firmwares

modd builds on other people's FM-1 firmwares (Felucca, Melodee, SLOOP, Optimist and more). Their source is fetched into `firmware/upstream/` and compiled as it is: modd does not keep edited copies. Almost everything modd needs from an upstream file can be done from outside it, in an adapter or a thin wrapper.

Sometimes that is not enough. An upstream file may fix a value in a way nothing outside the file can change, for example the size of a delay buffer written as a plain `#define`. For those cases this folder holds small patches: each one adds a hook to an upstream file, and modd's configuration supplies the value through the hook.

## What is in here

```
patches/
  README.md            this file
  felucca/
    0001-buffer-sizes-from-config.patch
  melodee/
    0001-lcd-debug-framebuffer-hooks.patch
    0002-ota-build-hooks.patch
    0003-usb-midi-build-hooks.patch
  optimist/
    0001-ble-scan-hid-hooks.patch
    0002-ble-att-ft-hooks.patch
```

One folder per upstream, patches numbered in the order they are applied.

| Patch | Upstream files | What it does | Why |
|---|---|---|---|
| `felucca/0001-buffer-sizes-from-config.patch` | `firmware/src/fx.c`, `firmware/src/slicer.c` | The delay, chorus and slicer buffer sizes can be set by the build (`CONFIG_FELUCCA_DLY_LEN`, `CONFIG_FELUCCA_CHO_LEN`, `CONFIG_FELUCCA_SL_LEN`) | Felucca's delay line alone is 128 KB; the FM-1's memory map does not hold it next to modd's shell |
| `melodee/0001-lcd-debug-framebuffer-hooks.patch` | `firmware/src/lcd.c` | `lcd_fill` and `lcd_blit` call debug framebuffer hooks under `CONFIG_DEBUG_FRAMEBUFFER` | The emulator's shadow framebuffer (screenshot capture, test assertions) must see every draw |
| `melodee/0002-ota-build-hooks.patch` | `firmware/src/ota.c` | `OTA_AREA`, `OTA_LOADER_TAG`, `OTA_USB_ID`, `OTA_DEC_DEFINED` and `OTA_BOOT_INSTALLER` hooks | modd uses its own flash map, loader tag, USB identity and decode buffer; drops the pinned m-bootloader installer |
| `melodee/0003-usb-midi-build-hooks.patch` | `firmware/src/usb.c` | `USB_HAL_HEADER`, `EP0BUF_DEFINED`, `USB_STRINGS_DEFINED` and `USB_HOOK_*` callbacks for events, MIDI input and SysEx | modd shares ep0buf with the host stack, uses its own USB strings and routes SysEx to the identity handshake, settings reset and debug DAP |
| `optimist/0001-ble-scan-hid-hooks.patch` | `firmware/src/ble/ble_scan.c` | `BLE_SCAN_HID_VARS`, `BLE_SCAN_HID_AFTER_PARSE`, `BLE_SCAN_HID_IS_LISTED`, `BLE_SCAN_HID_KIND` and `BLE_SCAN_HID_AFTER_LISTED` hooks in `ble_scan_add` | modd adds HID-over-GATT device detection to the BLE scanner; the hooks let the wrapper inject variable declarations, an extended AD parse, slot priority for HID devices, a kind-assignment guard, and HID listing |
| `optimist/0002-ble-att-ft-hooks.patch` | `firmware/src/ble/ble_att.c` | `ATT_HANDLES_TAIL`, `ATT_EXTRA_DECLS`, `ATT_EXTRA_DB`, `ATT_EXTRA_STATE`, `ATT_RESET_HOOK`, `ATT_LEN_HOOK`, `ATT_128UUID`, `ATT_WRITE_HOOK` and `ATT_EVENT_HOOK` hooks, plus `#ifdef BLE_DG`/`BLE_DIAG` diagnostic blocks | modd extends the ATT server with the File Transfer GATT service (handles 16-25) and BLE diagnostics instrumentation; the hooks let the wrapper add handles, database entries, state, attribute dispatch and event callbacks |

## How it works

1. `vendir` fetches the upstream trees. They are never edited.
2. Before compiling, the build copies the few files that have patches into `build/<defconfig>/gen/patched/` and applies the patches to those copies.
3. The compiler picks up the patched copies in place of the originals. Every other upstream file still comes straight from `firmware/upstream/`.

This happens on every `task build`; there is nothing to run by hand. If a patch no longer fits the upstream file (after an upstream update), the build stops and says which patch.

## The rules, in short

- A patch only adds a hook. It does not change what the upstream code does: with modd's settings left at their defaults, the firmware is byte for byte the same as without the patch.
- Each patch is approved by the project owner, by upstream file name. The approval, and whether the change was offered to the upstream author, are written at the top of the patch file.
- Every patch is listed in [docs/UPSTREAM_ORIGINS.md](../docs/UPSTREAM_ORIGINS.md).
- `task vendor:patches:check` verifies that every patch still applies to the pinned upstream versions. CI runs it.

## More

- Why modd includes upstream code this way, and what to try before a patch: [docs/PORTING.md](../docs/PORTING.md#what-to-try-in-order).
- Licences of the upstream firmwares: [THIRD_PARTY.md](../THIRD_PARTY.md).

# Working with patches

The rest of this file is for whoever adds or refreshes a patch, person or coding agent. The owner's rule: "Never copy or rewrite upstream code unless duplication / forking is approved by the user."

## When a patch is allowed

1. The adapter or wrapper cannot do it. Try, in order: the plain include, renames around the include, `#define` before the include, glue in the wrapper, the upstream's own helper file included with renames. Write down why each failed.
2. The patch is a hook only: an `#ifndef` / `#endif` around a constant, or the like. The value comes from Kconfig and is set in the wrapper. With no value set, the image is byte-identical to the unpatched build (check it with a fixed `FIRMWARE_VERSION`: the default version string holds the build time).
3. The user approved the patch by upstream file name and by what it changes. Ask first; do not write the patch and ask afterwards. A change of behaviour (audio code, control flow) is a new approval, not a patch.
4. The patch has a PATCHED row in [docs/UPSTREAM_ORIGINS.md](../docs/UPSTREAM_ORIGINS.md). `tests/build/upstream_origin_test.py` fails without the row or without the header fields below.

Where possible, offer the hook to the upstream author and put the link in `Forwarded:`.

## Header

DEP-3 style, before the first `diff --git` line:

```
Description: one line, then continuation lines starting with a space
Approved-by: Dmitry Kireev, 2026-11-23
Forwarded: no | <URL> | not-needed
Applies-to: hugelton/Felucca v1.8.2 (<the full 40-hex commit from vendir.lock.yml>)
---
diff --git a/firmware/src/fx.c b/firmware/src/fx.c
...
```

Required: `Description`, `Approved-by`, `Forwarded`, `Applies-to`. `Approved-by` names the person who approved and the date; never fill it in without that approval. The diff is a git diff with paths relative to the upstream root (`a/firmware/src/fx.c`); a patch edits existing files only (no new, deleted or renamed files). The series is applied in file-name order.

## Write or refresh a patch

The vendored tree is not touched; work on copies outside it (the container has no `diff` or `patch`; `git diff --no-index` does the job):

```sh
mkdir -p /tmp/p/a /tmp/p/b
cp -r firmware/upstream/<name>/firmware /tmp/p/a/            # or only the files to change
cp -r /tmp/p/a/. /tmp/p/b/ && $EDITOR /tmp/p/b/firmware/src/<file>.c
(cd /tmp/p && git diff --no-index a b) | sed -e 's#a/a/#a/#' -e 's#b/b/#b/#' > body.diff   # paths relative to the upstream root: a/firmware/src/<file>.c
```

Put the header above `body.diff` and save it as `patches/<name>/NNNN-<what>.patch`. Then:

1. Kconfig symbols for the values, defaults equal to upstream's; limits upstream relies on in `range` and the help text.
2. In the wrapper, define the constants from Kconfig before the include and `_Static_assert` what upstream assumes about them.
3. The PATCHED row in `docs/UPSTREAM_ORIGINS.md`.
4. `task vendor:patches:check`, the byte-identity build, one build with a changed value and the routine test, `task test:unit`.

The worked example with every step: [docs/PORTING.md](../docs/PORTING.md#add-a-patch-step-by-step).

After `task vendor:sync` moved a pin, run `task vendor:patches:check`: it fails when `Applies-to` names another commit than `vendir.lock.yml` or a hunk no longer applies. Re-read the patch against the new commit, regenerate the diff the same way and update `Applies-to`. Do not edit hunks by hand to make them fit.

## How it is applied

- `task build` (and `task modd:build:<defconfig>`) runs `task vendor:patch` after `task vendor:ensure`. It runs `tools/build/patches.py apply --out build/<defconfig>[-open]/gen/patched`: for each upstream with patches, copy the touched files to `<out>/upstream/<name>/<path>` from the pristine tree, then `git apply` the series there (strict, no fuzz; it fails and names the patch and hunk). Always from the pristine files, so there is no "already applied" state.
- `<out>/stamp.json` records the pin, each patch's hash and each pristine file's hash. `build.py` stops when the stamp is missing or stale, and puts `<out>` first on the include path, so `#include "upstream/felucca/firmware/src/fx.c"` finds the copy. Upstream files include their neighbours by bare name (`fx.c`: `#include "perform.c"`); the copy has only the patched files, so the vendored source directory of each patched file is searched last.
- `vendor:patch` reruns only when `patches/**` or `vendir.lock.yml` changed, or its output is missing (one cache entry per defconfig and toolchain). `task vendor:patches:check` also runs in CI.
- `python tools/build/patches.py apply|verify|check` is the tool; run it through `uv run`, outside a build only for experiments.

## Do not

- Do not edit `firmware/upstream/`, also not "to try something".
- Do not add a patch for a file nobody approved, or widen an approved patch.
- Do not copy the patched file into `firmware/src/`.
- Do not skip the check after a pin moved.
