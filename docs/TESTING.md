# Testing

## Setup

Install [mise](https://mise.jdx.dev/getting-started.html), then run from the
repository root:

```sh
mise trust
mise install
mise exec -- uv sync --locked
```

[.mise.toml](../.mise.toml) pins Python, uv, and Task. mise provides the Python
interpreter; uv creates `.venv` and installs dependencies from
[pyproject.toml](../pyproject.toml) and [uv.lock](../uv.lock).
No separate Python installation is needed.

For browser tests, use an installed `chromium`, set `CHROMIUM_PATH`, or install
Playwright's browser:

```sh
mise exec -- uv run playwright install chromium
```

On Ubuntu, add `--with-deps` to install required system libraries too.

## Run tests

```sh
task test           # both suites (unit + e2e)
task test:unit      # Python unit tests only
task test:e2e       # emulator + Playwright e2e tests (core suite)
```

With mise activated in your shell, `mise exec --` prefix is optional.
On first use, Task asks you to trust the two remote setup-fm1 Taskfiles
listed in `Taskfile.yml`. CI uses `task --yes` to accept them non-interactively.

If `task` stops with `Remote taskfiles are not enabled`, an older Task is
earlier on your `PATH` than the pinned one: run it as `mise exec -- task ...`.
The unit tests need no system libraries beyond a C compiler. The first
firmware build of a defconfig renders fonts, which needs the FriBiDi library
from the Nix dev shell. See [DEVELOPMENT.md](DEVELOPMENT.md#environment) for
what each of flake, mise and uv provides.

Run one E2E test with:

```sh
task test:e2e -- -k test_firmware_boots
```

Run a specific E2E file outside the Taskfile (useful for tests not in the core suite):

```sh
uv run python -m pytest tests/e2e_ui_controls_test.py -v
```

## Test architecture

### Unit tests (`tests/test_*.py`)

Host-only, no hardware or emulator. Cover configuration generation, package
checksums/compression, defconfig validation, and control helper validation.

### E2E tests (`tests/e2e_*_test.py`)

Browser-based tests using the fm1-emu WASM emulator and Playwright.
Each test class inherits from `E2ETestCase` (in `tests/e2e_fixtures.py`), which:

- Starts a local HTTP server serving the emulator
- Launches headless Chromium via Playwright
- Provides `controls(page)` for named encoders, buttons, and notes
- Provides helpers: `push()`, `long_push()`, `send_enc()`, `screenshot()`
- Saves screenshots and logs in `tests/results/<timestamp>-<module>/`

A typical E2E test uploads the built `.fwsc` into the emulator, waits for boot,
then interacts with the virtual panel and asserts LCD canvas state via pixel
hash comparison.

## E2E test inventory

### Core suite (run by `task test:e2e`)

| File | What it tests | Controls exercised |
|------|--------------|-------------------|
| `e2e_boot_test.py` | Firmware boots, LCD renders, panel messages reach emulator | All encoders (0-6), all 14 buttons, 3 notes, hold+encoder, master pot |
| `e2e_components_test.py` | Build manifest matches defconfig, binary manifest | None (build verification) |
| `e2e_usb_safety_test.py` | USB SysEx keys present/absent per defconfig | None (binary/manifest checks) |
| `e2e_audio_test.py` | Oscillator output, analog acid sequence | Notes (C4-G4) |
| `e2e_audio_engines_test.py` | Engine switching, audio output per engine, level normalization, master volume | SELECT encoder, HOME button, master pot, notes |

### Extended suite (run individually or via pytest glob)

| File | What it tests | Controls exercised |
|------|--------------|-------------------|
| `e2e_ui_controls_test.py` | **All Felucca UI controls on all pages** (59 tests). Button-tap navigation, long-press, encoder CW/CCW on every param page, dead buttons, hold+encoder combos, full page sweep | All buttons, all encoders, notes, hold+encoder combos |
| `e2e_page_nav_test.py` | Page-change latency via SELECT encoder | SELECT encoder CW/CCW |
| `e2e_sys_menu_test.py` | SYS page opens, tab switching, value cycling, HOME return | HOME (long-press), ALGO encoder, SELECT encoder |
| `e2e_debug_log_test.py` | DEBUG page renders boot log, scroll works | GLO button, SELECT encoder |
| `e2e_stuck_notes_test.py` | Notes don't stick after engine switch or page change | SELECT encoder, notes |
| `e2e_sched_test.py` | Scheduler stability: boot+UI, audio+nav, rapid page stress, extended run | FX, HOME, SEQ, ENV, LFO, SCL, EDIT buttons, SELECT/K1/K2 encoders, notes |
| `e2e_ble_test.py` | BLE module integration | (varies) |
| `e2e_views_test.py` | **All views with screenshots**: HOME, ENV-SEQ (buttons), MIXER/VOICE (GLO view), FILES (SAVE), SYS (long-HOME), WIFI (long-GLO), engine cycling, rapid switching | All buttons, SELECT, ALGO encoders |
| `e2e_weather_test.py` | Weather page rendering, refresh, spinner | SELECT encoder, PLAY button |

### Coverage summary

| Area | Covered by |
|------|-----------|
| Button → page navigation (all 9 nav buttons) | `e2e_ui_controls_test.py::TestButtonNavigation` |
| Long-press HOME → SYS | `e2e_ui_controls_test.py::TestLongPressNavigation`, `e2e_sys_menu_test.py` |
| SELECT encoder page nav (CW/CCW) | `e2e_ui_controls_test.py::TestSelectEncoder`, `e2e_page_nav_test.py` |
| HOME: engine select, track select, K1-K3 | `e2e_ui_controls_test.py::TestHomePage` |
| Param pages: K1-K3 on ENV/LFO/FX/SCL/EDIT/ARP | `e2e_ui_controls_test.py::Test{ENV,LFO,FX,SCL,EDIT,ARP}Page` |
| VOICE, MIXER pages | `e2e_ui_controls_test.py::TestVOICEPage`, `TestMIXERPage` |
| Dead buttons (OCT+/-, PLAY, REC, SAVE) | `e2e_ui_controls_test.py::TestDeadButtons` |
| Hold+encoder combos | `e2e_ui_controls_test.py::TestHoldEncoderCombo` |
| Full page sweep (all registered pages) | `e2e_ui_controls_test.py::TestFullPageSweep` |
| Engine audio output | `e2e_audio_engines_test.py` |
| Master volume | `e2e_audio_engines_test.py::TestMasterVolume` |
| Cross-engine level balance | `e2e_audio_engines_test.py::TestAudioLevelNormalization` |
| Engine select volume stability | `e2e_audio_engines_test.py::TestEngineSelectVolumeConsistency` |
| Scheduler multi-task stability | `e2e_sched_test.py::SchedAcceptanceTest` (boot, audio+nav, stress, extended) |
| All views with screenshots | `e2e_views_test.py::TestAllViews` (HOME, button pages, GLO/SAVE/SETTINGS/CONN views, engine cycling) |

## Audio testing

E2E audio tests capture audio from the emulator's WebAudio output, save WAV
files, and measure RMS levels. No hardware or speakers needed.

### Infrastructure

The JS hook in `e2e_audio_engines_test.py` patches `AudioContext` at
construction time to insert a `ScriptProcessor` recorder and an `AnalyserNode`
between the emulator's output and `AudioContext.destination`. Three JS snippets
drive it:

| Snippet | What it does |
|---------|-------------|
| `JS_HOOK_AUDIO_CONTEXT` | Inject before firmware boot. Patches `new AudioContext` to attach recorder + analyser |
| `JS_START_RECORDING` | Begin capturing PCM chunks (call before playing notes) |
| `JS_STOP_AND_EXPORT_WAV` | Stop recording, encode chunks as a 16-bit stereo WAV, return base64 |
| `JS_READ_RMS` | Read live RMS + peak from the analyser (no recording needed) |

The `save_wav()` and `wav_rms()` helpers decode the base64 WAV and compute
host-side RMS for assertions.

### Writing an audio test

```python
class TestMyAudio(E2ETestCase):
    DEFCONFIG = 'felucca'

    def test_something_sounds_right(self):
        page = self.new_emu_page()
        page.evaluate(JS_HOOK_AUDIO_CONTEXT)       # must be before boot
        # ... boot firmware, wait for ready ...
        ctrl = self.controls(page)
        ctrl.set_master_volume(512)                 # set a known level
        page.wait_for_timeout(500)                  # let IIR settle

        page.evaluate(JS_START_RECORDING)
        ctrl.play_note('c4', hold_ms=300)
        page.wait_for_timeout(300)
        wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

        path = TEST_RESULTS / 'my-test.wav'
        save_wav(wav['base64'], path)
        rms = wav_rms(path)
        self.assertGreater(rms, 0.001, 'No audio')
```

### Effective audio test patterns

**Set master volume explicitly.** The emulator boots with pot=900 (~88%).
Always call `ctrl.set_master_volume(value)` and wait 500ms for the firmware's
IIR filter to settle. Use a low value (e.g. 307 = 30%) when testing for
volume consistency bugs: problems that are masked at 88% become obvious at 30%.

**Record a baseline before changing state.** The strongest audio tests follow
this structure: record at state A, change something, record at state B, change
back, record at state A again. Compare A-before vs A-after. This catches
state corruption that single-measurement tests miss entirely.

**Don't re-set volume between recordings.** When testing whether an action
(engine switch, preset change) corrupts volume, set master volume once after
boot and never touch it again. Re-setting it between steps masks the very bug
you're looking for.

**Use silence recordings to isolate layers.** If a level changes, record
silence (no notes) across the same steps. If silence RMS stays at zero, the
master volume path is clean and the bug is in the engine/voice layer. This
eliminates an entire module from the investigation in one test run.

**Fresh page per engine for cross-engine comparison.** When comparing RMS
levels between engines, boot a fresh emulator page for each engine. Shared
pages accumulate state (filter coefficients, voice history) that skews
measurements.

**RMS tolerance guidelines:**

| Comparison | Tolerance | Rationale |
|-----------|-----------|-----------|
| Same engine, same params, round-trip | 3 dB (0.707x–1.414x) | State should be identical |
| Different engines, same volume | 12 dB (0.25x–4x) | Synthesis methods have inherent level differences |
| Volume scaling (half vs full) | Directional only | `rms_full > rms_half` is sufficient |

### Audio test inventory

| Test class | What it verifies |
|-----------|-----------------|
| `TestAudioLitmus` | JS audio capture works (440 Hz oscillator, no firmware) |
| `TestNoiseEngine` | Noise engine produces audio |
| `TestAnalogEngine` | Analog engine produces audio |
| `TestFM6Engine` | FM6 engine produces audio |
| `TestMasterVolume` | Volume at pot 0/10/512/1023 scales monotonically |
| `TestAudioLevelNormalization` | All engines audible, cross-engine levels within 12 dB, master volume scales output |
| `TestEngineSelectVolumeConsistency` | Engine select round-trip (analog→noise→analog) preserves audio level at 30% volume |

### Known gaps

- **OCT+/-, PLAY, REC, SAVE** have no firmware handler. Tests verify they don't
  crash but can't verify function.
- **PRESET encoder (ENC 6)** is tested for message delivery (`e2e_boot_test`)
  but has no page-level handler to verify.

## Panel controls in tests

```python
ctrl = self.controls(page)
ctrl.rotate_encoder('presets', direction=1, steps=3)
ctrl.press_button('home')
ctrl.press_button('home', hold_ms=600)     # long-press
with ctrl.hold_button('edit'):
    ctrl.rotate_encoder('knob1', direction=-1, steps=2)
ctrl.play_note('C#4', hold_ms=500)
ctrl.set_master_volume(512)
```

### Physical controls map

| Encoder ID | Name | Test name | What it does |
|-----------|------|-----------|-------------|
| 0 | SELECT | `select` | Engine select (HOME), page nav (others), scroll (DEBUG/FILES/BLE) |
| 1 | ALGO | `algorithm` | Track select (HOME), tab switch (SYS) |
| 2 | KNOB1 | `knob1` | Param knob[0] (HOME), param[0] (param pages) |
| 3 | KNOB2 | `knob2` | Param knob[1] (HOME), param[1] (param pages) |
| 4 | KNOB3 | `knob3` | Param knob[2] (HOME), param[2] (param pages) |
| 5 | KNOB4 | `knob4` | Param knob[3] (HOME), param[3] (param pages) |
| 6 | PRESET | `presets` | (no page handler yet) |
| — | MASTER | `master` | Analog volume pot (hardware ADC, not an encoder) |

### Button map

| ID | Name | Test name | Tap action | Hold action (500ms) |
|----|------|-----------|-----------|-------------------|
| 0 | OCT- | `oct-` | — | — |
| 1 | OCT+ | `oct+` | — | — |
| 2 | FX | `fx` | → FX page | — |
| 3 | SCL | `scl` | → SCL page | — |
| 4 | ENV | `env` | → ENV page | — |
| 5 | LFO | `lfo` | → LFO page | — |
| 6 | EDIT | `edit` | → EDIT page | — |
| 7 | GLO | `glo` | → DEBUG page* | — |
| 8 | HOME | `home` | → HOME page | → SYS page** |
| 9 | SAVE | `save` | — | — |
| 10 | ARP | `arp` | → ARP page | — |
| 11 | SEQ | `seq` | → SEQ page | — |
| 12 | PLAY | `play` | — | — |
| 13 | REC | `rec` | — | — |

\* Only when `CONFIG_DEBUG_LOG` is enabled, otherwise no action.
\** Only when `CONFIG_SYS_MENU` is enabled, otherwise → ABOUT page.

### Assertion patterns

E2E tests detect UI changes by comparing pixel hashes of LCD canvas regions:

```python
# Full LCD region (native canvas coords: 1280x820, LCD at 370,42 size 480x480)
LCD_REGION = (370, 42, 480, 480)

# Hash a region before and after an action
before = _lcd_region_hash(page, *LCD_REGION)
ctrl.press_button('env', delay_ms=500)
after = _lcd_region_hash(page, *LCD_REGION)
self.assertNotEqual(before, after, 'LCD did not change')
```

For finer assertions, use sub-regions (cards row, footer) to detect which
part of the screen changed. The `_canvas_hash()` function hashes the entire
canvas including the panel frame.

## Screenshots and logs

Each run writes under `tests/results/<UTC-timestamp>-<test-module>/`:

- `screenshots/*.png`: panel canvas captures at named test points.
- `*.log`: emulator status, firmware log, JS console output.

Results are gitignored. No `E2E_DEBUG` setting is required.

## CI results

GitHub Actions runs unit tests and the core E2E suite on pull requests and
pushes to `main`. Setup uses `mise install` and `uv sync --locked`. The
`dimmkirr/setup-fm1-toolchain@v1` and `dimmkirr/setup-fm1-emulator@v1` actions
install the SDK, compiler, and emulator.

Each run that reaches E2E saves screenshots, logs, and JUnit XML in the
`e2e-results` Actions artifact for 14 days, including when tests fail.
`main` push/manual runs also attach captured PNGs to the rolling
`ci-screenshot` prerelease and embed them in the job summary.

Pull requests never publish releases.

## Tagged releases

Pushing a tag matching `v[0-9]*` starts `release.yml`. It calls the same CI
build and test workflow, then publishes only if that workflow succeeds.
The release uses the exact firmware tested by E2E; it does not rebuild it.

Release assets: `modd.fwsc`, `minimal.config`, tagged source archive,
license notices, `SHA256SUMS`, and panel screenshots.

## Emulator setup fallback

`FM1_WEB_DIR` selects an existing emulator web distribution; the default is
`~/.fm1-emulator/web`. The download task's source-build fallback needs Rust
and `rustup`. On Nix with the WASM target already installed, build a local
fm1-emu v0.2.0 checkout directly:

```sh
task emulator:build EMU_SOURCE=/path/to/fm1-emu
```

## Real weather in the emulator

An fm1-emu with the WiFi chip (`--oss-wifi`, from 0.4.x) carries the
firmware's clean-room driver through scan, WPA2, DHCP, DNS and HTTP to a
virtual AP, "Home" with the password "fm1pass", and with `--internet` to the
real network behind it. In the browser build the page's `fetch()` is that
network (`?oss-wifi=1&internet=1`): HTTP only, and from an https page only the
hosts that have https (Open-Meteo does, ip-api.com's free API does not).

`FM1_E2E_WIFI=SSID:PASSWORD` makes the weather e2e build the firmware with
those credentials, boot with the two flags and check that the data card is
drawn:

```sh
FM1_E2E_WIFI=Home:fm1pass task test:e2e -- -k data_over_the_virtual_ap
```

Without the variable the weather tests run as before, with no network.

## RPC remote control

The firmware includes a JSON-RPC server that accepts commands over WebSocket
on port 9222, plus a REST API over plain HTTP. It runs inside the firmware's
lwIP TCP/IP stack, enabled by `CONFIG_RPC=y` (the `modd_dev_rpc` defconfig
has it on).

### Architecture

```
 Playwright / pytest
       |
       |  (1) WASM emulator path:
       |      Playwright drives the emulator web page.
       |      bridge.py intercepts FM1.* calls and
       |      translates them into emulator button/encoder actions.
       |
       |  (2) Firmware-direct path (native emulator, or real hardware):
       |      A WebSocket client connects to ws://HOST:9222
       |      and sends FM1.* JSON-RPC directly to the firmware's
       |      RPC server running on lwIP.
       |
       v
  FM-1 firmware (RPC server on port 9222)
```

Path (1) works today in E2E tests. Path (2) requires the firmware to have an
IP address reachable from the host: on real hardware via WiFi, or in fm1-live
via `--forward 9222:9222` (inbound TCP NAT).

### Why not `page.goto("modd://home")`?

Playwright's `page.goto()` navigates the browser to a URL. `modd://` is not a
real browser protocol: the browser would show an error page. The firmware's RPC
server is a WebSocket endpoint (it also serves REST endpoints at `/api/*` and
`/modd/*`). Page navigation happens by sending a JSON-RPC message over an open
WebSocket connection:

```python
import json, asyncio, websockets

async with websockets.connect("ws://localhost:9222") as ws:
    await ws.send(json.dumps({
        "id": 1,
        "method": "FM1.navigate",
        "params": {"url": "modd://home"}
    }))
    resp = json.loads(await ws.recv())
    # resp = {"id": 1, "result": {"page": "home", "index": 0}}
```

In the WASM emulator tests, `bridge.py` wraps this so the test code looks
the same regardless of which path is used underneath.

### RPC methods

| Method | Params | Returns | What it does |
|--------|--------|---------|-------------|
| `FM1.getInfo` | — | firmware, version, lineage, uptime_ms, wifi_ip | Firmware identity |
| `FM1.navigate` | `url` | page, index | Jump to a `modd://` URI (see below) |
| `FM1.pressButton` | `id`, `hold_ms` | ok | Simulate a button press |
| `FM1.turnEncoder` | `id`, `clicks` | ok | Simulate encoder rotation |
| `FM1.sendMidi` | `data` (hex) | ok | Inject MIDI bytes |
| `FM1.getEngineState` | — | engine, voices, ... | Current engine info |
| `FM1.getAudioMetrics` | — | peak, clip, ... | Audio pipeline stats |
| `FM1.getLog` | — | log_pos, entries[] | Debug log ring |
| `FM1.memoryRead` | `address`, `length` | base64 data | Raw memory read |
| `FM1.memoryWrite` | `address`, `data` (base64) | written | Raw memory write |

### `modd://` URI scheme

The `FM1.navigate` method accepts URIs with the `modd://` prefix. The firmware
matches the page name case-insensitively against the registered UI pages.

| URI | Page | Notes |
|-----|------|-------|
| `modd://home` | Home | Engine select, track select, knobs |
| `modd://settings/display` | Settings | Display tab |
| `modd://settings/midi` | Settings | MIDI tab |
| `modd://settings/audio` | Settings | Audio tab |
| `modd://settings/control` | Settings | Control tab |
| `modd://settings/hw` | Settings | Hardware tab |
| `modd://settings/net` | Settings | Network tab |
| `modd://settings/system` | Settings | System tab |
| `modd://weather` | Weather | Weather display |
| `modd://demo` | Demo | Audio demo |
| `modd://files` | Files | File browser |
| `modd://disk` | Disk | USB disk browser |
| `modd://log` | Log | Debug log |
| `modd://colors/3` | Colors | Apply palette 3 (0-9) |

The settings sub-path sets `mset.tab`; the colors sub-path calls
`colors_apply()`. All other pages take no sub-path.

### Running RPC tests

The RPC/bridge tests are in `tests/rpc/`:

```sh
# WASM emulator path (bridge translates navigate calls to button presses):
uv run python -m pytest tests/rpc/bridge_navigate_test.py -v

# Firmware-direct path (requires fm1-live with --forward):
# 1. Build firmware with RPC enabled:
task build -- --defconfig modd_dev_rpc

# 2. Run fm1-live with port forwarding:
fm1-live --fwsc build/modd_dev_rpc/modd.fwsc \
         --wifi oss --net internet \
         --forward 9222:9222

# 3. In another terminal, connect and navigate:
python3 -c "
import json, asyncio, websockets
async def main():
    async with websockets.connect('ws://localhost:9222') as ws:
        await ws.send(json.dumps({
            'id': 1,
            'method': 'FM1.navigate',
            'params': {'url': 'modd://home'}
        }))
        print(json.loads(await ws.recv()))
asyncio.run(main())
"
```

### RPC defconfig

`configs/modd_dev_rpc_defconfig` enables the RPC server and auto-connecting
USB ethernet (so the emulator's `--usb ethernet` link brings up lwIP without
a menu visit):

```
CONFIG_RPC=y
CONFIG_LWIP=y
CONFIG_USB_NET=y
CONFIG_USB_NET_AUTO=y
CONFIG_WIFI_LWIP=y
```

### Inbound TCP NAT (fm1-emu)

The fm1-emu emulator's NAT is outbound-only by default. To reach the
firmware's RPC server from the host, fm1-live supports `--forward H:G`:

```sh
fm1-live --fwsc modd.fwsc --wifi oss --net internet --forward 9222:9222
```

This binds `127.0.0.1:9222` on the host and forwards accepted TCP connections
to the guest's `10.0.2.15:9222` by injecting SYN packets into the virtual
network. The guest sees connections from the gateway IP (`10.0.2.2`).

Multiple forwards are supported: `--forward 9222:9222 --forward 8080:80`.
Inbound TCP is native-only (fm1-live); the WASM browser build cannot bind TCP
listeners.

## Toolchain and cross-compilation

The JieLi clang 4.0.1 compiler is an x86-64 Linux binary. The build system
(`tools/build.py`) auto-detects the host architecture and picks the right
runner:

- **x86-64 Linux**: runs the compiler natively.
- **ARM/aarch64 Linux, kernel runs x86-64 ELFs** (Rosetta on Apple-silicon
  VMs, a binfmt_misc registration): runs the compiler through an x86-64
  `ld.so`, no emulator process.
- **ARM/aarch64 Linux with `qemu-x86_64`**: user-mode QEMU.
- **macOS / Docker fallback**: compiles inside a `linux/amd64` Docker
  container (`JIELI_DOCKER=1` forces it).

The two Linux fallbacks need x86-64 glibc and libstdc++, looked up in this
order: `JIELI_X86_GLIBC` + `JIELI_X86_STDCPP`, `~/.fm1-toolchain/x86libs`
(populate once with `task toolchain:x86libs`, which copies them out of a
`linux/amd64` Debian image), the nix store.

These tests do not flash hardware.
