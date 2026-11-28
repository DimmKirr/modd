# Firmware Metadata

A 96-byte `fw_meta_t` struct at a fixed offset in every modd binary. Tools, the REST API and hex editors can read it without symbol tables.

## Location

| Context | Address |
|---------|---------|
| Body offset | `0x80` (128 bytes from body start) |
| XIP address | `0x02001080` |
| app.bin offset | `0x0F60` (init stub `0x0EE0` + body `0x80`) |
| Flash address | `0x5080` (flash body base `0x5000` + `0x80`) |

The linker places a `.fw_meta` section at this offset in both `app.ld` and `app_dock.ld`. The CPU entry in `crt0.S` jumps to `cli` before reaching the metadata gap, so the struct is never executed.

## Struct layout (`firmware/hal/fm1_meta.h`)

```
Offset  Size  Field        Description
─────────────────────────────────────────────────────────
 0      4     magic        0x444D4F4D — "MODD" in memory (little-endian)
 4      2     meta_ver     Struct format version (currently 1)
 6      2     flags        Reserved, 0
 8      16    name         Null-terminated firmware name (CONFIG_FIRMWARE_NAME)
24      32    version      Null-terminated version string (CONFIG_FIRMWARE_VERSION)
56      4     build_time   Unix epoch seconds (UTC)
60      12    git_sha      Short hex commit SHA, null-terminated
72      24    _reserved    Zero (future use)
─────────────────────────────────────────────────────────
        96 bytes total
```

## Reading the metadata

**From firmware C code** (unity build):

```c
#include "fm1_meta.h"
extern const fw_meta_t fw_meta;  /* in .fw_meta section */
```

**From a binary / hex dump:**

Seek to body offset `0x80` and check the first 4 bytes for the magic `4D 4F 44 44` ("MODD"). If the magic matches, parse the struct. If not, the binary has no metadata (older build or a different firmware).

**From fm1-live (emulator):**

```
FM1_DUMP=02001080 fm1-live --shot 100 firmware.fwsc
```

## Build integration

`build.py` passes three compile-time defines:

| Define | Source | Example |
|--------|--------|---------|
| `CONFIG_FIRMWARE_NAME` | Kconfig | `"modd"` |
| `CONFIG_FIRMWARE_VERSION` | `FIRMWARE_VERSION` env or `dev-<datetime>-<sha>` | `"dev-20261128T120000Z-a83321996"` |
| `BUILD_TIMESTAMP` | `time.time()` at build start | `1732795200` |
| `GIT_SHA_SHORT` | `git rev-parse --short=11 HEAD` | `"a83321996ab"` |

## Compatibility

The metadata is part of the body, CRC-protected by the commit marker (`CRBM`). It does not change the `.fwsc` package format, the `CRSTUB` init signature, or the boot stub's validation. Flashers and OTA installers that do not know about `.fw_meta` work unchanged — the bytes are invisible to them.

The `magic` field lets any tool distinguish a metadata-bearing binary from one without. Older modd builds and other community firmwares will not have `"MODD"` at body + `0x80`.

## Relationship to other binary structures

```
app.bin
├─ 0x0000  crt0_init.S entry (_start)
├─ 0x0008  "CRSTUB" + u16 init_version    ← boot init identity
├─ ...     init body (validation, OTA check, LCD splash)
├─ 0x0EE0  body starts (crt0.S: goto, rts, cli, sp=, call fm1_cstart)
├─ 0x0F60  .fw_meta (96 bytes)            ← THIS
├─ 0x0FC0  .text continues (application code + rodata)
├─ ...     .ram_text, .dsp_text, .data, .audio_text (at LOADADDR offsets)
└─ mark_off CRBM commit marker (20 bytes)  ← body integrity seal
```

See also: [BOOT.md](BOOT.md) (boot sequence), [SYSEX.md](SYSEX.md) (debug protocol), `firmware/init/` (init stub), `firmware/boot/` (boot loader).
