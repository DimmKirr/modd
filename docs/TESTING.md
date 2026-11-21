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

The browser is needed only by `tools/codegen/gen_ui_screenshots.py`, which
captures the Felucca theme's screenshots in the WASM emulator. Use an installed
`chromium`, set `CHROMIUM_PATH`, or install Playwright's browser:

```sh
mise exec -- uv run playwright install chromium
```

On Ubuntu, add `--with-deps` to install required system libraries too.

## Run tests

```sh
task test           # both suites (unit, then API)
task test:unit      # Python unit tests only
task test:api       # builds modd_emu, modd and felucca, runs the API tests in fm1-live
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

Run one API test with:

```sh
task test:api -- -k test_firmware_boots
```

Run a specific file outside the Taskfile (the firmware must be built; `task test:api` builds it):

```sh
uv run python -m pytest tests/boot/api_boot_test.py -v
```

## Test architecture

### Unit tests (`tests/test_*.py`)

Host-only, no hardware or emulator. Cover configuration generation, package
checksums/compression, defconfig validation, the build pipeline (`tests/build/`)
and tooling.

### Layout and naming

`tests/<area>/<subject>_test.py`, one area per firmware module (`audio`, `ble`, `boot`, `build`, `debug`, `dock`, `net`, `rpc`, `ui`, `usb`, `wifi`). The file name says how it runs:

| Name | How it runs |
|---|---|
| `api_<subject>_test.py` (also `*/api/`) | fm1-live over HTTP, real firmware behaviour |
| `<subject>_test.py` + `<subject>_test.c` | the C is compiled on the host with `cc`; the `.py` runs it |
| `<subject>_test.py` | host Python: a tool, a generator, Kconfig, or a fake device |
| `wifi/wifi_source_guards_test.py` | source-level guards for bugs found on hardware (the emulator has no WiFi MAC) |
| `bench/<subject>_bench.py` | not a test: scripted fm1-live runs that write frames |
| `build_fixtures.py`, `api_fixtures.py` | shared helpers |

### Traces and timings (every test)

`tests/conftest.py` and `tests/io_log.py` trace every pytest run, with no code in the tests:

| Output | What is in it |
|---|---|
| `tests/results/<RUN_STAMP>-<module>/debug.log` | Every event of the module's tests, timestamped (UTC, ms): each test's start, setup / call / teardown with outcome and seconds, end with its IO totals; every `subprocess.run` (command, exit code, seconds); each HTTP request of the API client (seconds, the wait for fm1-emu's connection budget); every line fm1-live prints; the tests' own events (`io_log.log()`). Tab-separated: `time_utc test kind seconds throttle_wait detail` |
| a line per test of 0.5 s or more | `[start +seconds] N io S s, throttled W s  <test>` |
| the end of the run, and `tests/results/<RUN_STAMP>-timings.tsv` | Every test of 0.5 s or more, slowest first, with its share of the run |

One run has one `RUN_STAMP`: a module's screenshots, fm1-live logs and `debug.log` share its
`<RUN_STAMP>-<module>/` folder (`io_log.results_dir()`; `ApiTestCase.results_dir()` uses it).
A test that logs its own events calls `io_log.log(kind, detail, seconds, count=False)`;
`count=True` (the default) adds it to the test's IO totals.

## Direct-API tests (`tests/**/api_*_test.py`, `tests/ui/themes/lvgl/api/`)

Python drives the firmware over HTTP; no browser. `ApiTestCase`
(`tests/api_fixtures.py`) boots one fm1-live per test class with the
`modd_emu` build (`--usb ethernet --net internet --forward <port>:9222`),
waits for `/api/info`, and stops the process afterwards. Every step after that
is a GET to the firmware's own REST API. The full reference is the OpenAPI 3.1
document each build generates from its C code (see
[Writing a REST handler](#writing-a-rest-handler)): `build/<defconfig>/openapi.json`,
also served at `GET /openapi.json` (`CONFIG_RPC_OPENAPI`), with only the routes
and fields that build has. A summary:

| Need | Route | Kconfig |
|------|-------|---------|
| page list, current page, jump | `/api/pages`, `/api/page`, `/modd/<page>[/<sub>]` | `RPC` |
| buttons, encoders, note keys, master pot | `/api/input/button/<name>/tap\|hold/<ms>\|down\|up`, `/api/input/enc/<name>/<clicks>`, `/api/input/key/<midi>[,<midi>…]/down\|up\|tap/<ms>`, `/api/input/pot/master/<0..1023>\|release`, state in `/api/input` | `RPC_INPUT` |
| track engine | `/api/track/<n\|sel>[/engine/<name>]` | `RPC_INPUT` |
| audio level, last 8192 output samples | `/api/audio`, `/api/audio/capture` | `RPC_AUDIO_TAP` (EMU) |
| memory read, byte search | `/api/mem/<hex addr>/<len>`, `/api/mem/find/<hex>` | `RPC_MEM` |
| LCD, log | `/api/screenshot`, `/api/log` | `DEBUG_FRAMEBUFFER`, `DEBUG_LOG` |
| this list, as OpenAPI 3.1 | `/openapi.json` | `RPC_OPENAPI` (default y) |


Input is ORed into the panel scan (`fm1_input.h`), so debounce, press and
release edges, hold times and LEDs follow the path a finger takes; timed
releases (`tap`, `hold`) are done by the firmware in firmware time. A list of
note keys is pressed in one store, so a chord lands on one scan frame.

```
task test:api                          # = task modd:test:modd_emu:api: build, then every API test
RPC_HOST=10.0.2.15:9222 uv run python -m pytest tests/boot/api_boot_test.py   # a running target
```

| File | What it tests |
|------|--------------|
| `boot/api_boot_test.py` | Boot (API, IP, HOME, LCD, boot log); every button, encoder, note key and the pot reach the firmware |
| `audio/api_audio_test.py` | The capture follows the output; Analog plays C4..G4 at pitch |
| `audio/api_audio_engines_test.py` | Noise/Analog/FM6 sound, master volume, 12 dB balance, round trip, no bleed, LCD follows engine and track |
| `usb/usb_otg_presence_safety_test.py` | Every defconfig enables USB; the UBOOT SysEx key (the `task flash` path) is in the modd, modd_emu and felucca binaries that `task test:api` builds first in parallel; melodee and sloop config-only. Live (fm1-live, about 3 s): production `modd` with a computer on the port enumerates as USB-MIDI, stays on the bus (the OTG port keeps device mode while a computer drives it), and reboots into the updater on the UBOOT key. Also runs in `test:unit` (a binary not built skips) |
| `net/api_weather_test.py` | Weather from open-meteo, PLAY refresh, revisit |
| `wifi/api_wifi_need_test.py` | The radio follows the WIFI setting and the network need: WIFI off then on with WiFi as the only link (the panel is pressed through `fm1-live --cdp`, because the API travels over WiFi), WiFi stepping aside while USB ethernet has an address, and a boot with WIFI saved as off (fm1-live restarted on the same `--flash` image; needs `xdotool`). Off means powered down: the log shows `wifi: deinit done`, on again a second `wifi: init done`, and the WiFi task stack keeps 512 bytes free. Each class boots fm1-live with its own flags; `task modd:test:modd_emu:api:wifi` runs the API tests with WiFi as the only link |
| `ui/themes/lvgl/api/lvgl_api_test.py`, `lvgl_ui_walkthrough_test.py` | Pages, page data, every control, palettes, hints, DEMO, audio; OCR and colours on every page |

**Connection budget.** Each request is one TCP connection (`Connection:
close`), and fm1-emu's NAT holds at most 32 connections, each for 30 s after
it closes (`MAX_TCP`, `TIME_WAIT_MS` in `crates/fm1-soc/src/nat/mod.rs`).
`FirmwareApi` keeps to 24 per 31 s against fm1-live and reserves the
connections of a timed sequence (press, wait, capture, release) before it
starts. Waits are in firmware time, so a throttled emulator costs time, not
correctness; a capture that still lands late is retried.

**Offline states** are not tested: fm1-live forwards a port only with its NAT
(`--net internet`), so a firmware under API test always has a network.

## Writing a REST handler

The routes live in one table, `API_ROUTES[]` at the end of
`firmware/src/rpc/api_ctl.c`. `tools/codegen/gen_openapi.py` reads that file and
`api_pages.c` on every `task build` (when `CONFIG_RPC` is on) and writes the
OpenAPI document; prose comes from `docs/api/openapi.overlay.yaml`. Nothing
else describes the API, so the document cannot drift from the code. The
generator reads C with a small parser, not a compiler, and stops the build on
anything it cannot place, with the file, the line, the reason and the fix.

**A route.** One line in `API_ROUTES[]`, under the `#ifdef` of the symbols it
needs (the `#ifdef` becomes `x-kconfig` in the document and decides which
builds list it):

```c
#ifdef CONFIG_RPC_INPUT
    { "/api/input/button/{button}/tap/{ms}",      api_btn_tap },
#endif
```

`{name}` matches one path segment; literal segments match case-insensitively;
the first match wins, so put `.../release` before `.../{value}`. An unknown
path under `/api` or `/modd` is a 404.

**Parameters.** The handler gets the `{params}` as `a[0]`, `a[1]`, ... (NULL past
the route's last one) and checks each with an `api_arg_*()` call before using
it. The call is where the document takes the parameter's schema from, so keep
the name a string literal (the same as in the route) and the bounds literals or
`#define`s:

| Call | Schema | Bad value |
|------|--------|-----------|
| `api_arg_str(a[0], "page")` | string | — |
| `api_arg_int(a[1], "ms", 10, 60000, &ms)` | integer 10..60000 | 400 |
| `api_arg_int_or(a[0], "track", 0, NTRK - 1, "sel", song.sel, &k)` | integer, or the word | 404 |
| `api_arg_enum(a[0], "button", API_BTN, 14u, &b)` | the names of the table, or their index | 404 |
| `api_arg_hex32(a[0], "addr", &addr)` | hex string | 400 |
| `api_arg_hexbytes(a[0], "hex", buf, API_NEEDLE_MAX, &n)` | pairs of hex digits | 400 |
| `api_arg_notes(a[0], "notes", &mask)` | MIDI notes, comma-separated | 404 |

A failed check has replied already: `if (!api_arg_int(...)) return;`.

**The reply.** Write the JSON with the writers the generator knows; it replays
them, through any helper the handler calls, and reads the result as JSON:

- buffered: `rpc_http_json_ok(tx)`, `rpc_tx_key_str|int|uint|bool(tx, "key", value)`,
  `rpc_tx_char(tx, ',')`, `rpc_tx_raw(tx, "\"list\":[")` (literals only),
  `api_tx_jstr(tx, s)` for a string alone, `rpc_http_json_end(tx)`;
- streamed (no size limit): `api_stream_begin()`, `api_stream_str("{\"count\":")`
  (literals only), `api_stream_uint|int(v)`, `api_stream_jstr(s)`,
  `api_stream_b64(p, n)` inside a string, `api_stream_end()`;
- errors: `rpc_http_error(404, "...")` with a literal code (it becomes a response).

What the generator makes of it: a key written by `rpc_tx_key_uint` is an integer
≥ 0, `_int` an integer, `_bool` a boolean, a literal value a `const`. A key
inside `#ifdef X` gets `x-kconfig: [X]`. A key every reply has is `required`; one
inside an `if`, or after an early `return` that is not an error, is optional.
A loop writes the items of an array. A page's data comes from its serializer in
`api_page_json_table[]` (api_pages.c), one schema per page name.

**Prose.** Summaries, descriptions, units and examples go in
`docs/api/openapi.overlay.yaml`, keyed by route, parameter, field
(`tracks[].engine`) or page name. A key there that the C no longer has stops the
build too.

**When the build stops.** Every problem is listed at once, each with what to do:

| Message says | Do |
|--------------|----|
| `{x} ... never checks a[N]` | check the parameter with an `api_arg_*()` call (the message has the line) |
| `writes text that is not a literal` | use `rpc_tx_key_*` for a keyed value, `api_tx_jstr` / `api_stream_jstr` for a string alone |
| `the key of rpc_tx_key_*() is not a string literal` | write the key as a literal |
| `which no route reaches` | call the function from its route's handler, add the route to `API_ROUTES[]`, or list it in `Config.ignore` |
| `is not a Kconfig symbol` | say what the macro stands for in `Config.macros` (gen_openapi.py), or guard with the `CONFIG_*` symbol |
| `is a function pointer` | name its `{"NAME", function}` table in `Config.dispatch` |
| `is not a number or a #define of one` | make the bound a literal or a `#define` |
| `rpc_path_match` | move the route into `API_ROUTES[]` |
| `does not read as JSON` | open and close objects and arrays in the same function or helper |
| `the overlay describes ...` | rename the overlay key to the new name, or delete it |

`gen_openapi.py --check` runs the same reading without a build;
`gen_openapi.py --full FILE` writes the document for every build with the
`x-kconfig` markers kept. `tests/build/openapi_test.py` (in `task test:unit`)
covers the reading and, for each kind of problem, that its fix works.

## Coverage summary

| Area | Covered by |
|------|-----------|
| Boot, panel input, pages | `boot/api_boot_test.py`, `ui/themes/lvgl/api/` |
| Engine audio output | `api_audio_engines_test.py` |
| Master volume | `api_audio_engines_test.py::TestMasterVolume` |
| Cross-engine level balance | `api_audio_engines_test.py::TestAudioLevelNormalization` |
| Engine select volume stability | `api_audio_engines_test.py::TestEngineSelectVolumeConsistency` |

## Audio testing

Audio tests record the firmware's own output, not the host's: with
`CONFIG_RPC_AUDIO_TAP` (emulator builds) the audio ISR keeps the last 8192
samples it hands the DAC (mono, after the master volume) and
`GET /api/audio/capture` returns them; `/api/audio` adds their RMS and peak.
Notes are played on the note keys through `/api/input`, so they take the same
path as the panel.

### Infrastructure

`FirmwareApi` in `tests/api_fixtures.py`:

| Helper | What it does |
|---------|-------------|
| `record_chord(notes, hold_ms)` / `record_note(note)` | Press the keys in one request, wait `hold_ms` of firmware time, capture, release; checks the keys were still down at the capture and that it was not late |
| `capture()` | The last 186 ms of output as `array('h')` |
| `pot(v)` / `pot_release()` | Hold the master pot at 0..1023 / back to the ADC |
| `set_engine(name)` | Switch the selected track's engine (`modd_engine_select`) |

`rms(samples)` and `tone_power(samples, hz)` (one DFT bin) do the analysis;
`ApiTestCase.save_wav()` keeps each recording in the results directory.

### Writing an audio test

```python
class TestMyAudio(ApiTestCase):
    def test_something_sounds_right(self):
        self.go_home()
        self.api.set_engine('ANALOG')
        self.api.pot(512)
        self.api.wait_ms(400)                       # the main loop smooths the pot
        s = self.api.record_note('C4')
        self.save_wav('my-test.wav', s)
        self.assertGreater(rms(s), 0.001, 'No audio')
        self.assertGreater(tone_power(s, note_hz('C4')), 4 * tone_power(s, note_hz('D4')))
```

### Effective audio test patterns

**Set master volume explicitly.** The emulator boots with pot=900 (~88%).
Always call `api.pot(value)` and wait ~400 ms of firmware time for the firmware's
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

**Capture inside the note.** A capture is the last 186 ms; take it while the
keys are held (`record_chord` does). Decaying sounds (FM6's piano) lose level
fast, so a capture that lands late measures the tail: `record_chord` rejects
captures more than 250 ms late and retries.

**RMS tolerance guidelines:**

| Comparison | Tolerance | Rationale |
|-----------|-----------|-----------|
| Same engine, same params, round-trip | 3 dB (0.707x–1.414x) | State should be identical |
| Different engines, same volume | 12 dB (0.25x–4x) | Synthesis methods have inherent level differences |
| Volume scaling (half vs full) | Directional only | `rms_full > rms_half` is sufficient |

### Audio test inventory

| Test class | What it verifies |
|-----------|-----------------|
| `AudioCaptureTest` (`api_audio_test.py`) | The capture follows the output: silent at rest, sound while held, silent after release |
| `AnalogProducesAudioTest` | C4..G4 on Analog, each at its pitch |
| `TestNoiseEngine` | Noise engine produces audio |
| `TestAnalogEngine` | Analog engine produces audio |
| `TestFM6Engine` | FM6 engine produces audio |
| `TestMasterVolume` | Volume at pot 0/10/512/1023 scales monotonically |
| `TestAudioLevelNormalization` | All engines audible, cross-engine levels within 12 dB, master volume scales output |
| `TestEngineSelectVolumeConsistency` | Engine round trip (analog→noise→analog) keeps the level at 30% volume; no sound after a switch with no key held |
| `TestEngineSelectDisplay` | The LCD and HOME data follow an engine switch and SELECT |

### Known gaps

- **OCT+/-, PLAY, REC, SAVE** have no firmware handler. Tests verify they don't
  crash but can't verify function.
- **PRESET encoder (ENC 6)** is tested for delivery to the UI (`api_boot_test`:
  its detents are taken) but has no page-level handler to verify.

## Panel controls in tests

Tests press controls through the REST API (`FirmwareApi` in `tests/api_fixtures.py`
wraps these routes):

```
GET /api/input/enc/knob1/3                 # encoder name, clicks (negative = CCW)
GET /api/input/button/home/tap             # tap
GET /api/input/button/home/hold/600        # long press
GET /api/input/key/61,64/tap/500           # note keys, ms
GET /api/input/pot/master/512              # master volume 0..1023
```

### Physical controls map

| Encoder ID | Name | API name | What it does |
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

| ID | Name | API name | Tap action | Hold action (500ms) |
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

## Screenshots and logs

Each run writes under `tests/results/<UTC-timestamp>-<test-module>/`:

- `*.png`: LCD screenshots the tests save (`/api/screenshot`).
- `debug.log` and the fm1-live logs: see "Traces and timings" above.

Results are gitignored.

## CI results

GitHub Actions (`.github/workflows/ci.yml`) has two jobs. Setup in both uses `mise install`
and `uv sync --locked`; `dimmkirr/setup-fm1-toolchain@v1` installs the SDK and compiler.

The `test` job runs on pull requests, pushes to `main` and manual runs. It runs
`task test:unit`, then builds the production firmware with `task build` (defconfig `modd`,
JieLi compiler, so `build/modd/modd.fwsc`). It saves `tests/results/` in the `test-results`
Actions artifact for 14 days, including when a step fails. Tests that need a build, the
toolchain or fm1-live skip on the runner when those are missing.

The `api` job runs `task test:api`. It is skipped unless the repository variable `FM1_EMU_REF`
is set to a ref of `DimmKirr/fm1-emu` that contains the `fm1-live` port forward and the NAT
fix; those commits are not pushed or released yet, and `setup-fm1-emulator` does not ship
`fm1-live`. The job checks out that ref, builds `fm1-live` with `cargo build --release --locked
-p fm1-live` (cargo cache), installs Xvfb and FriBiDi (the `felucca` build renders fonts with
Pillow and libraqm), starts Xvfb on `:99`, and runs the suite with `FM1_LIVE` pointing at the
binary. It is non-blocking (`continue-on-error`) and does not run for release tags. Its
screenshots, logs and JSON are the `api-results` artifact for 14 days. `main` push/manual runs
also attach the PNGs to the rolling `ci-screenshot` prerelease and embed them in the job
summary.

Pull requests never publish releases.

## Tagged releases

Pushing a tag matching `v[0-9]*` starts `release.yml`. It calls the same CI
workflow with `release_bundle` set (the `test` job only; the `api` job does not run for tags),
then publishes only if that workflow succeeds. The release uses the exact firmware built and
unit-tested there; it does not rebuild it.

Release assets: `modd.fwsc`, `modd.config` (`build/modd/gen/.config`, the resolved configuration
of that build, written before WiFi and API-key values from the environment are applied, so it
holds no secrets), tagged source archive, license notices and `SHA256SUMS`. Screenshots are
added only when the results artifact contains any, which it does not for tags. `dev.yml`
publishes the same assets to the rolling `dev` prerelease on every push to `main`.

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
real network behind it. `task modd:test:modd_emu:api:wifi` runs the API tests
with WiFi as the only link (`fm1-live --wifi oss --net internet`), including
the weather test.

## RPC remote control

The firmware includes a JSON-RPC server that accepts commands over WebSocket
on port 9222, plus a REST API over plain HTTP. It runs inside the firmware's
lwIP TCP/IP stack, enabled by `CONFIG_RPC=y` (the `modd_emu` defconfig
has it on).

### Architecture

```
 client (pytest, a script, a WebSocket tool)
       |
       |  fm1-live: --forward 9222:9222 (inbound TCP NAT)
       |  real hardware: the WiFi IP address
       |
       v
  FM-1 firmware (RPC server on port 9222, on lwIP)
```

The server needs an IP address reachable from the host: on real hardware via
WiFi, in fm1-live via `--forward 9222:9222`. The WASM build cannot bind TCP
listeners, so it does not serve this API.

### Page navigation over the WebSocket

`modd://` is not a browser protocol. The firmware's RPC server is a WebSocket
endpoint (it also serves REST endpoints at `/api/*` and `/modd/*`). Page
navigation over WebSocket is a JSON-RPC message on an open connection:

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

### Running the RPC server by hand

Build the firmware with RPC enabled, run fm1-live with port forwarding and
connect to the WebSocket:

```sh
# 1. Build firmware with RPC enabled:
task build -- --defconfig modd_emu

# 2. Run fm1-live with port forwarding:
fm1-live --fwsc build/modd_emu-open/modd.fwsc \
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

The automated tests use the REST routes instead (see
Direct-API tests above);
`tests/rpc/rpc_kconfig_test.py` only checks the RPC Kconfig symbols on the host.

### RPC defconfig

`configs/modd_emu_defconfig` enables the RPC server and auto-connecting
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
