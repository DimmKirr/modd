# Porting Components

How upstream code gets ported to modd, module by module.

## The rule: upstream code, through an adapter, into modd

**Never copy or rewrite upstream code unless duplication / forking is approved by the user.**

**The work of porting is writing the adapter, and nothing else.** If a port step produces anything other than adapter code, a thin wrapper, a Kconfig symbol and an `#include`, it is not a port step: stop and ask.

A component is *ported* when the upstream source file is compiled as it is, from `firmware/upstream/<name>/`, and everything modd-specific lives in an adapter (`firmware/adapters/modd_*.h`) or in a thin wrapper file next to the include.

```
firmware/upstream/felucca/firmware/src/eng_adpcm.c     upstream, never edited (vendir)
        |  #include
firmware/src/audio/engines/eng_adpcm_modd.c            wrapper: renames, one signature, the vtable
        |  conforms to
firmware/adapters/modd_engine.h                        adapter contract (engine_t)
```

### Classes

Every layer of a build is in one of six classes. Say the class in the ticket, the commit message and the report.

| Class | Meaning | Counts as ported |
|-------|---------|------------------|
| INCLUDED | the upstream file is `#include`d unchanged; glue is in an adapter or wrapper | yes |
| PATCHED | the upstream file is `#include`d through a build-time copy with an approved patch from `patches/<upstream>/` applied (a hook only); `firmware/upstream/` is unchanged | yes, with the patch named |
| COPIED | a copy of the upstream file lives in `firmware/src/` and is edited there | no: needs the user's approval |
| REDUCED | a copy with features removed to fit modd's types | no: needs the user's approval |
| REWRITTEN | the behaviour is re-created by hand in new modd code | no: needs the user's approval |
| OTHER UPSTREAM | the slot is filled by another firmware's code (Melodee storage in a Felucca build) | no |

State on 2026-11-24 for the Felucca build (`modd_felucca_emu`, `CONFIG_AUDIO_FELUCCA_STACK_INCLUDED`): the ten engines are INCLUDED; the audio stack (core, params, mod matrix, slicer, effects, mixer) is INCLUDED, with `fx.c` and `slicer.c` PATCHED for their buffer sizes; the sequencer is REDUCED; the 52 shell pages are REWRITTEN; storage and FM6 are OTHER UPSTREAM. Hardware `modd_felucca` still builds the older copies (effects and mod matrix COPIED). Whether the modd shell's pages may stay a rewrite is an open decision of the user's (`.scratch/tasks/todo/2026-11-23T13-10-felucca-port-upstream-through-adapter-not-rewrite.md`).

### Do

The model is `firmware/src/audio/engines/eng_adpcm_modd.c`. The whole wrapper is the difference between upstream and modd, and nothing else:

```c
/* Origin: hugelton/Felucca firmware/src/eng_adpcm.c @ v1.6.1 (a53ecf42)
 * Wrapper: adapts adp_post signature (Felucca: 3-arg uint32_t return;
 * modd engine_t: 4-arg void) and rebuilds the ENG_ADPCM vtable. */
#define adp_post  _felucca_adp_post          /* rename what clashes */
#define ENG_ADPCM _FELUCCA_ENG_ADPCM
#include "upstream/felucca/firmware/src/eng_adpcm.c"   /* upstream, unchanged */
#undef adp_post
#undef ENG_ADPCM

static void adp_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr)
{
    (void)nr;
    _felucca_adp_post(t, out, n);            /* adapt one signature */
}

static const engine_t ENG_ADPCM = { .name = "ADPCM", /* ... modd's vtable ... */ };
```

1. `#include` the upstream file from `firmware/upstream/`.
2. Put renames, signature shims and the modd vtable in `<name>_modd.c`, with an `Origin:` line pinned to the upstream commit.
3. When upstream needs a type, field or function modd lacks, add it to the adapter (`modd_*.h`), so the next upstream file gets it too.
4. Guard the include with its `CONFIG_*` symbol.

### Do not

- Do not copy an upstream `.c` into `firmware/src/` and edit it there.
- Do not trim an upstream file to the fields modd has today ("reduced to modd's `track_t`"): extend the adapter types.
- Do not re-create upstream behaviour in new code (a page that "replicates" a layout, a sequencer that "implements the interface").
- Do not carry a private copy of an upstream table or constant in a wrapper (`eng_slice_modd.c` has its own `ENV_EXP`, built for the wrong control tick: a defect of exactly this kind).
- Do not edit anything under `firmware/upstream/` or `firmware/vendor/`.

### What to try, in order

Go down this list and stop at the first step that works. Each step is tried only after the one above it has failed, and the failure is written in the ticket (the symbol and the error).

| # | Step | Where the code goes | Class | Example in the tree |
|---|------|---------------------|-------|---------------------|
| 1 | `#include` the upstream file as it is | wrapper `<name>_modd.c` | INCLUDED | `firmware/src/audio/core/felucca/stack_modd.c` |
| 2 | Rename what clashes, for the span of the include (`#define x up_x` ... `#undef x`) | wrapper | INCLUDED | `eng_adpcm_modd.c` above |
| 3 | Supply what the file expects from its includer: a macro, a switch, a type with the fields it reads, a function it calls | wrapper, or `firmware/adapters/modd_*.h` when a second file will need it | INCLUDED | `fx_unit_sloop_insert_modd.c` (the parameter record, the ids, the dry-copy arrays) |
| 4 | The file needs a helper from its own upstream that clashes with one modd already has: include that upstream's helper file too, every shared name renamed for the span of the include | a wrapper of its own | INCLUDED | `firmware/src/audio/effects/host/dsp_optimist_modd.c` (Optimist's `dsp.c` beside Felucca-Engines' `dsp.c`, 17 names renamed) |
| 5 | A constant or a hook the file fixes with a plain `#define`: a patch | `patches/<upstream>/` | PATCHED | `patches/felucca/0001-buffer-sizes-from-config.patch` |
| 6 | Stop and report the blocker; ask for a fork by file name | the ticket | none | see the stop rule below |

Never between 4 and 6: a few lines copied "because it is only a helper". A 3-line copy of Optimist's `glide_next` was made that way on 2026-11-23 and removed again with step 4.

### Patches: a hook in an upstream file

A patch is for what no adapter can reach: a size or a switch that upstream writes as a plain `#define` (no `#ifndef`), so that a `#define` before the include does nothing. The user approved the mechanism on 2026-11-23 as a fallback, and approves each patch by upstream file name. Class PATCHED: the file is still included from its upstream path, through a patched copy.

How it works: `firmware/upstream/` is never edited. `task build` runs `task vendor:patch` after `task vendor:ensure`. It copies the files a patch touches to `build/<defconfig>[-open]/gen/patched/upstream/<name>/`, applies `patches/<name>/NNNN-*.patch` there in file-name order with `git apply` (strict: a hunk that does not fit stops the build), and the build puts that directory first on the include path. The wrapper's `#include "upstream/felucca/firmware/src/fx.c"` does not change. The series is applied to pristine files on every run; there is no "already applied" state.

What a patch may contain:

- Allowed: `#ifndef` / `#endif` around a constant, or a hook of that kind. The value comes from Kconfig and is set in the wrapper. With no value set, the image is byte-identical to the unpatched build.
- Not allowed without a new approval by file name: a change of behaviour (audio code, control flow), a file nobody approved, a new or deleted file.

#### Add a patch, step by step

The first patch is the worked example: Felucca's delay, chorus and slicer buffer sizes.

1. Show that steps 1 to 4 above cannot do it. Quote the upstream line (`#define DLY_LEN 65536u`, `fx.c:5`) and why a `#define` before the include fails (upstream redefines it).
2. Ask the user for the patch by file name and by what it changes: "may I patch Felucca `firmware/src/fx.c` and `slicer.c`: `#ifndef` around `DLY_LEN`, `CHO_LEN`, `SL_LEN`, nothing else?". No approval, no patch.
3. Write the diff on a copy outside the vendored tree and add the header (`Description`, `Approved-by`, `Forwarded`, `Applies-to` with the commit from `vendir.lock.yml`). Commands and the header format: [patches/README.md](../patches/README.md). Save as `patches/<upstream>/NNNN-<what>.patch`; the number is the order.
4. Add the Kconfig symbols that carry the values, defaults equal to upstream's (`FELUCCA_DLY_LEN`, `FELUCCA_CHO_LEN`, `FELUCCA_SL_LEN` in `kconf/audio.kconfig`). Put limits upstream relies on into `range` and the help text.
5. In the wrapper, define the constants from Kconfig before the include, and assert what upstream assumes about them (`stack_modd.c`: `#define DLY_LEN (CONFIG_FELUCCA_DLY_LEN + 0u)` and a `_Static_assert` that it is a power of two, because upstream masks with `DLY_LEN - 1u`).
6. Add the PATCHED row to `docs/UPSTREAM_ORIGINS.md` (the patch file, the upstream files, who approved, when).
7. Check it:
   - `task vendor:patches:check`: header complete, commit matches the lock, the series applies.
   - Byte identity: build once with the patch and default values and once with `patches/` moved away, both with a fixed `FIRMWARE_VERSION` (the default version string holds the build time); the two `body.bin` must be equal.
   - The hook: one build with a changed value, read the memory report (`DLY_LEN` 32768: `dly_buf` 128.0K to 64.0K), run the routine test, then put the defconfig back.
   - `task test:unit` (`tests/build/patches_test.py`, `tests/build/upstream_origin_test.py`).
8. Report the class as PATCHED and paste the patch text into the ticket.

#### Keeping patches working

- After `task vendor:sync` moved a pin, run `task vendor:patches:check`. It fails when `Applies-to` names another commit than `vendir.lock.yml`, or when a hunk no longer fits. Read the patch against the new upstream file, regenerate the diff, update `Applies-to`. Do not edit hunks by hand to make them fit.
- If upstream took the hook, delete the patch, its row and nothing else: the Kconfig symbols and the wrapper lines keep working.
- CI runs the check in the `test` job. The build stops with "the patched upstream tree is not current" when the copy is missing or stale; `task build` repairs it, `python tools/build/patches.py` by hand is for experiments only.
- `vendor:patch` reruns when `patches/**/*.patch` or `vendir.lock.yml` changed (one cache entry per defconfig and toolchain). Every build applies every upstream's series, also for upstreams it does not include.

### When the include does not compile: stop rule

1. Name the blocker exactly: the symbol that clashes, the field `track_t` lacks, the function upstream calls that modd does not have.
2. Fix it in the adapter or the wrapper (rename, shim, new field, a small glue function).
3. If that is not possible, **stop and report** the blocker and what the adapter would need. Do not fall back to a copy or a rewrite: that needs the user's approval, asked for by name ("may I fork `seq.c` into `firmware/src/seq/`?").

An approved fork is recorded where it lives: the `Origin:` line, one sentence saying who approved it and why, and the class in the ticket.

### Done

A port step is done when the layer is INCLUDED, the routine test below passes on its checks, and the ticket names the class. "The test passes" on a COPIED, REDUCED or REWRITTEN layer is not "ported": say which it is.

### Wording for tasks and prompts

"Port" alone is ambiguous: it has been read as include, as copy and as rewrite. Write the class into the request.

| Instead of | Write |
|------------|-------|
| "Port the Felucca sequencer" | "Make upstream `firmware/upstream/felucca/firmware/src/seq.c` compile unchanged behind `modd_seq.h`. Add what it needs to the adapter. If it cannot be included, stop and name the blocker; do not copy or cut it down." |
| "Implement SPREAD per-voice pan" | "SPREAD is upstream's, in its voice render. Find what stops that file from being included and report it. Do not write a new pan." |
| "Add the MENU page" | "The modd shell pages are REWRITTEN (approved or pending: see the ticket). Take the MENU rows and their order from upstream `ui_menu.c`; do not invent rows." |
| "Wire the DRUM engine" | "Include upstream `eng_drum.c` behind a wrapper `eng_drum_modd.c` like `eng_adpcm_modd.c`. Class must be INCLUDED." |

Every such request ends with: "Report the class of what you delivered (INCLUDED / COPIED / REDUCED / REWRITTEN / OTHER UPSTREAM) and any upstream line you changed or left out."

### Paste this into any porting task

```
Porting rule (the owner's words):
  "Never copy or rewrite upstream code unless duplication / forking is approved by the user."
  "The work of porting is writing the adapter, and nothing else."

A port step may produce: adapter code (firmware/adapters/modd_*.h), a thin
<name>_modd.c wrapper, a Kconfig symbol, and an #include of the upstream file.
Nothing else.

Stop rule: if the include does not compile, name the blocker (the exact symbol,
field or function), fix it in the adapter or wrapper, or stop and report. A fork
needs the user's approval, asked for by file name.

Do not copy an upstream file into firmware/src. Do not trim it. Do not re-create
its behaviour in new code.
Order of attempts: include as is; rename what clashes around the include; supply
what the file expects in the wrapper or adapter; include the upstream's own helper
file renamed beside modd's; only then a patch (patches/<upstream>/NNNN-*.patch: a
hook such as #ifndef around a constant, value from Kconfig, approved by the user by
file name, class PATCHED; steps in docs/PORTING.md "Add a patch, step by step").
Never edit firmware/upstream/. Never copy "just a helper".
List the file in docs/UPSTREAM_ORIGINS.md (tests/build/upstream_origin_test.py
checks it).
Test only modd_<upstream>_emu with the routine command from docs/PORTING.md.
End your report with the class of what you delivered (INCLUDED / PATCHED / COPIED /
REDUCED / REWRITTEN / OTHER UPSTREAM) and every upstream line you changed or left out.
```

### Before you start a port step

1. Which upstream file is it (`firmware/upstream/<name>/...`)?
2. Which adapter contract does it plug into (`firmware/adapters/modd_*.h`)?
3. What does the include fail on? Compile it and quote the first errors.
4. Has the user approved a fork of this file? The default answer is no.
5. Does `patches/<upstream>/` already hold a series for this upstream? Read it: the hook you need may exist, and your include gets the patched copy.

### Forks in the tree today

`docs/UPSTREAM_ORIGINS.md` lists every file with an `Origin:` line and its class. On 2026-11-23 it has:

- WRAPPER: 2 (`eng_adpcm_modd.c`, `eng_slice_modd.c`), included unchanged.
- HEADER-DERIVED: 39 adapter and HAL headers derived from upstream headers.
- FORK: 33, none approved (`not approved: to be replaced by an include`).

## Bringing a shell up without modules: mock first, wire later

This is how the shell is developed before the real modules are in, not a way to port. Mock data stands in for a module; it never replaces upstream code.

Every module follows the same three-phase approach:

1. **Adapter contracts** - define the types and globals the component reads/writes (`modd_*.h`)
2. **Mock data behind adapters** - populate those types with realistic defaults so dependent code compiles and renders without real hardware modules
3. **Wire real components** - enable the Kconfig symbol, mock stubs get skipped, real implementations populate the same globals

Code ported in phase 2 never changes in phase 3. It only reads from adapter-defined types.

## Testing a port: one emulator defconfig

Porting an upstream builds and tests one firmware: that upstream's emulator defconfig, `modd_<upstream>_emu`. No other defconfig is built or tested while the port is in progress.

| Porting | Build and test only | Task |
|---------|---------------------|------|
| Felucca | `modd_felucca_emu` | `task modd:test:modd_felucca_emu:api` |
| Melodee | `modd_melodee_emu` | `task modd:test:modd_melodee_emu:api` (defconfig and task not created yet) |
| SLOOP | `modd_sloop_emu` | `task modd:test:modd_sloop_emu:api` (defconfig and task not created yet) |
| any other upstream | `modd_<upstream>_emu` | `task modd:test:modd_<upstream>_emu:api` |

The first step of a port whose row says "not created yet" is to add the two: `configs/modd_<upstream>_emu_defconfig`, and `build:modd_<upstream>_emu` plus `test:modd_<upstream>_emu:api` in `configs/Taskfile.yml`, copied from the `modd_felucca_emu` pair.

The routine test is one command, about five minutes. For Felucca:

```
task modd:test:modd_felucca_emu:api -- tests/boot/api_boot_test.py tests/audio/api_audio_test.py tests/audio/api_audio_engines_test.py tests/audio/api_preset_encoder_test.py
```

For another upstream, the same command with its name in the task.

It builds `modd_<upstream>_emu` once (open toolchain, `build/modd_<upstream>_emu-open/`; skipped when nothing changed) and runs against that package:

| Tests | What they check |
|-------|-----------------|
| `tests/ui/shells/modd/api/` (the task's own list) | the modd shell: pages, page data, panel controls, palettes, the walkthrough with OCR |
| `tests/boot/api_boot_test.py` | boot, and every panel control reaches the firmware |
| `tests/audio/api_audio_test.py` | capture follows the output; Analog plays at pitch through the effects chain |
| `tests/audio/api_audio_engines_test.py` | each engine sounds, master volume, balance, engine round trips, and the Felucca sample engines |
| `tests/audio/api_preset_encoder_test.py` | PRESET encoder steps presets, crosses engine boundaries, loads preset params, and produces the new sound |

Rules:

- Do not run `task test:api`, `task build`, another upstream's emulator defconfig or a hardware defconfig as part of the routine. `task test:api` tests `modd_emu` and then builds `modd` and `felucca` for the USB safety scan. Run the wider set once, before committing: `task test:unit`, then the builds of the defconfigs that share the changed files (for Felucca: `modd_emu`, `modd_felucca`, `modd`).
- The paths after `--` are added to the task's test list. To run one file or one class, call pytest with the same environment: `USE_OPEN_TOOLCHAIN=1 MODD_API_DEFCONFIG=modd_<upstream>_emu uv run --group ui-test pytest <path> -k <class>`. Without `USE_OPEN_TOOLCHAIN=1` pytest looks in `build/modd_<upstream>_emu/`, the JieLi build.
- A class that names its own `DEFCONFIG` (`TestFeluccaSampleEngines`) boots that build whatever the task says. A class without one boots the task's defconfig.
- The shell tests are behind the shell (2026-11-23). They expect 17 pages (`PAGES` in `modd_api_test.py`) with FILES, DISK, ABOUT and LOG, a THEME row in Settings, and the CLAY palette. The shell now has about 50 pages, none of those four, and a PALETTE row, so the page-list, page-hold and walkthrough tests fail on `modd_emu` as well. `modd_felucca_emu` also has no DEMO page and starts on the GREY palette, which fails the colour tests: 22 shell tests fail on it in all, none of them a reset. The pages the list does not name (STEP, DETAIL, AUTO and the rest) are not visited. Until the page list comes from `/api/pages`, read a shell failure against this list before treating it as a defect of the port.

## Adapter layer

Adapters live in `firmware/adapters/modd_*.h`. Each one defines types, globals, constants, and mock data (when the real module is off).

```
modd_audio.h    - track/song/engine types, P_* enum, mock engine + init
modd_engine.h   - engine_t vtable, param_desc_t (MIT licensed)
modd_seq.h      - seq_steps(), seq_counting() stubs
modd_page.h     - modd_page_t vtable for page abstraction
modd_fx.h       - effects chain stubs
modd_midi.h     - MIDI routing stubs
modd_storage.h  - project storage stubs
modd_mod.h      - modulation matrix stubs
```

## What the UI reads (and where it comes from)

```
UI page code           reads from          defined in
-----------           ----------          ----------
trk[song.sel]         modd_audio.h        mock: modd_mock_init()
                                          real: audio core fills trk[]
ENGINES[t->eng_req]   modd_audio.h        mock: _eng_mock (ANALOG)
                                          real: engines.c registry
song.bpm, .playing    modd_audio.h        mock: modd_mock_init()
                                          real: transport / sequencer
eng->edit[k].label    modd_engine.h       engine vtable (MIT)
t->step[i].note[0]    modd_audio.h        mock: pre-filled pattern
                                          real: sequencer
```

The UI never calls audio functions, never allocates voices, never touches hardware. It reads structs.

---

## UI

Screen rendering, event dispatch, menu system, splash screen.

Kconfig: `CONFIG_UI_SHELL_MELODEE`, `CONFIG_UI_SHELL_SLOOP`, `CONFIG_UI_SHELL_MODD`.

### Components

| Component | File(s) | Description |
|-----------|---------|-------------|
| Shell | `shell.c` | Page lifecycle, navigation, splash dispatch |
| Pages | `page_*.c` | Individual screens (HOME, ENV, SEQ, ABOUT, etc.) |
| Boot logo | `ui/boot_logo.c` | RLE-compressed logo decoder, shared across all UI upstreams |
| Splash | Kconfig `SPLASH_LOGO` / `SPLASH_NONE` | Selectable splash screen style |

### Boot logo component

`boot_logo.c` decodes the RLE-compressed RGB565 logo from `build/gen/boot_logo.h` (generated by `tools/logo_convert.py` from `assets/logo-dark.png`).

Functions:
- `logo_draw(x, y)` - draws full 240x240 canvas (logo centered with padding rows)
- `logo_draw_content(x, y)` - draws only the content rows (64px tall), skipping padding

The early boot screen (`fm1_main`) calls `logo_draw(0, 0)` right after `lcd_init()`. This is the earliest possible display point: the boot stub and loader run before LCD initialization.

### Splash screen (Kconfig choice)

The splash is a Kconfig choice under UI:
- `CONFIG_SPLASH_LOGO` (default) - centered logo + version string below
- `CONFIG_SPLASH_NONE` - skip splash, go straight to home page

The Felucca splash renders the logo via `logo_draw_content()` centered vertically, with the version in `AF_S` font below it. Duration: 1s (10 main loop frames at 100ms each).

New splash styles can be added as Kconfig options (e.g. animated splash).

### Felucca UI port (worked example)

This example describes the upstream-renderer Felucca shell (`firmware/src/ui/shells/felucca/`), written before the rule above. By class: step 1 is INCLUDED; steps 2 to 5 are COPIED or REWRITTEN ("extracted", "replicate", pages written for modd) and are not a pattern to repeat without the user's approval. The modd shell (`CONFIG_UI_SHELL_MODD`, the shell of `modd_felucca_emu`) has its own pages in `firmware/src/ui/shells/modd/page_*.c`, all REWRITTEN.

The Felucca visual style is available as an LVGL skin (`CONFIG_UI_SKIN_FELUCCA`) on the modd shell. Felucca palettes and controls are in `firmware/src/ui/shells/felucca/lvgl_*.c`.

### Porting a different UI upstream

1. Include their renderer (gfx equivalent) unchanged
2. Create adapter stubs with realistic mock data
3. Include their pages and input code behind the page adapter (`modd_page.h`). Writing new pages is a rewrite: ask the user first (see [the rule](#the-rule-upstream-code-through-an-adapter-into-modd))
4. Build with the upstream's UI Kconfig + mocked modules
5. Wire real modules one at a time

The adapter contract is the same across all upstreams.

---

## Audio

Voice allocator, render pipeline, DMA half-buffer, mixer.

Kconfig: `CONFIG_AUDIO`. Optionally uses `CONFIG_DUAL_CORE`.

### Audio Core (pick one upstream)

- **Felucca**: 8+6 voices, fixed allocation
- **SLOOP**: voice pool shared across layers
- **Melodee**: 18 budget units, FPU-optimized render

### Engines (pick any)

Engines implement the `engine_t` vtable (defined in `modd_engine.h`, MIT licensed). Required fields: `note_on`, `render`. Optional: `amp`, `block`, `done`, `desc`, `keys`, `alloc`, `legato`, `mono_key`, `post`, `cap`, `units`, `preset_make`.

Engine cost is measured in VBUDGET units (Prophet: 2-3, FM6: 2, CZ-1: 1).

Engines compile independently of the full audio core via `CONFIG_ENGINES` (auto-selected). Build-time table generator (`tools/codegen/gen_engine_tables.py`) produces `fe_tables.h` with SINE, PITCH_OCT, SVF_G, and TANH_Q15 lookup tables. DSP helpers (`dsp.c`) provide fixed-point math.

Porting an engine (class INCLUDED):
1. Leave the source where vendir put it (`firmware/upstream/felucca-engines/src/`, `firmware/upstream/felucca/firmware/src/`, ...). Do not copy it.
2. If it compiles against `modd_engine.h` as it is, `#include` it from `engines.c` under its `CONFIG_ENGINE_<NAME>` guard (`eng_analog.c`, `eng_noise.c` are included this way).
3. If a name or a signature differs, add a wrapper `firmware/src/audio/engines/eng_<name>_modd.c` that renames, includes and adapts (`eng_adpcm_modd.c`), and include the wrapper.
4. Add the Kconfig symbol in `kconf/engines.kconfig` with `select ENGINES`.
5. Register in the `ENGINES[]` array.
6. Shared DSP helpers come from `firmware/upstream/felucca-engines/src/dsp.c`, included unchanged by `modd.c`; a helper another upstream needs and that file lacks goes into the adapter, not into a private copy.

### NOISE engine (worked example)

The NOISE engine is Felucca's (MIT, 271 lines), class INCLUDED: `engines.c` includes `upstream/felucca-engines/src/eng_noise.c`. It compiles against `modd_engine.h` / `modd_audio.h` without modification: `voice_t`, `vmod_t`, `engine_t`, and `track_t` are type-compatible.

Dependencies: `fe_tables.h` (generated), `dsp.c` (MIT helpers: `clamp`, `mulq15`, `noise32`, `softclip`, `soft_knee`, `voice_amp`), and the SVF_G cutoff table.

Config: `CONFIG_ENGINE_NOISE=y` in the defconfig. No `CONFIG_AUDIO` required: the engine renders through the adapter types and appears in the `ENGINES[]` registry for the UI.

### ANALOG engine (worked example)

The ANALOG engine is Felucca's (MIT, ~500 lines), class INCLUDED. Same process as NOISE: `engines.c` includes `upstream/felucca-engines/src/eng_analog.c` under `CONFIG_ENGINE_ANALOG` (`kconf/engines.kconfig`) and registers it in `ENGINES[]`. There is no copy in `firmware/src/`.

Dependencies: same as NOISE (`fe_tables.h`, `dsp.c`, SVF_G table). ANALOG also uses `PITCH_OCT[]` and `SINE[]` from the generated tables for its oscillator.

The engine's `.edit[]` array defines 8 parameter descriptors (CUT, RES, CUT2, KTR, OSC, SUB, PW, BND) with `.def` defaults, `.min`/`.max` ranges, `.label`, `.unit`, and `.fmt`. The `.knob[]` array maps the 4 physical encoders to parameter indices. These are read by the UI page to display labels, values, and gauge bars without any engine-specific code in the UI.

### Engine switching

Engine switching is a **UI action**, not a module-level action. It is triggered by the SELECT encoder on the HOME page and goes through the adapter layer.

The adapter function `modd_engine_select()` lives in `voice.c` and handles IRQ-safe switching:

```c
static void modd_engine_select(track_t *t, uint32_t ei)
{
    const engine_t *e;
    uint32_t i;
    if (ei >= NENGINES) ei = 0;
    e = ENGINES[ei];
    fm1_irq_off();
    trk_all_off(t);
    t->eng_req = (uint8_t)ei;
    t->engine  = (uint8_t)ei;
    t->preset  = 0;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    fm1_irq_on();
}
```

This mirrors upstream Felucca's `set_engine_of()` (`ui.c:1385`). The IRQ guard ensures the audio ISR sees either the old engine with its parameters or the new engine with its own defaults, never a half-updated state.

The UI page (`page_home.c`) calls `modd_engine_select(t, ei)` on encoder 0 rotation:

```c
if (id == 0 && NENGINES > 1u) {
    track_t *t = &trk[song.sel];
    int32_t e = (int32_t)t->eng_req + clicks;
    if (e < 0) e = (int32_t)NENGINES - 1;
    if (e >= (int32_t)NENGINES) e = 0;
    modd_engine_select(t, (uint32_t)e);
    fsh_dirty = 1;
    return 1;
}
```

This wraps the engine index, so rotating past the last engine goes back to the first. The UI page never directly writes `t->engine` or `t->eng_req`: it always goes through the adapter function.

### End-to-end audio: what we fixed wiring NOISE

Getting the NOISE engine to produce audio in the emulator required wiring four modules beyond the engine itself. Each fix was small, but all four were needed before any sound came out.

#### 1. Voice allocator (`audio/voice.c`)

The stub voice allocator was a no-op. Replaced with a working round-robin allocator:
- `trk_note_on`: finds a free voice (or steals the oldest), sets note/vel/gate/active/age/pitch16, calls `engine->note_on(t, v)`
- `trk_note_off`: finds matching active voice, clears gate/active
- `engine_block`: iterates active voices, calls `engine->render` into a per-voice buffer, sums into L/R mix buffers

The voice allocator renders mono per-voice, then adds to both L/R channels. The `vmod_t` passed to render must have `pitch16`, `inc` (from `pitch_inc()`), and `amp0`/`amp1` set to non-zero values or the engine output is silent.

#### 2. Audio core (`audio/core/audio.c`)

The audio core implements the I2S DMA double-buffer ISR:
- `fm1_alnk0_irq()`: checks `fm1_audio_pending()`, calls `mix_block()`, interleaves L/R into DMA buffer with `<< OUT_SHIFT` (Q15 to 24-bit)
- `audio_init()`: zeros the DMA buffer, inits tracks with `params_init()`, calls `fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3)`

The ISR function must be non-static (external linkage) because the assembly wrapper in `fm1_isr.S` calls it by symbol.

#### 3. IRQ enable (the silent killer)

`fm1_irq_enable_all()` was never called in `modd.c`. Without it, the CPU ignores all interrupts: the ALNK0 DMA ISR never fires, so the audio buffer stays zero-filled. Melodee calls this in `main.c` after all modules are initialized. We added it right after `audio_init()`.

This produced zero audio output with no compiler warnings, no linker errors, and no runtime crashes. The firmware ran, the LCD displayed correctly, the engine was registered, but the DMA buffer was never filled. Always check that `fm1_irq_enable_all()` is called before the main loop.

#### 4. ISR timing globals

The assembly ISR wrapper (`fm1_isr.S`) reads/writes `fm1_audio_irq_start_ticks` and `fm1_audio_irq_elapsed_ticks` for diagnostics. These must be non-static `uint32_t` globals in the C translation unit. Without them, the linker fails with undefined references.

#### 5. Adapter ordering

`modd_midi.h` provides a stub `midi_out_event()` guarded by `#ifndef MODD_USB_MIDI`. The USB MIDI module (`midi/usb.c`) defines the real one. But `MODD_USB_MIDI` was defined _after_ the adapter include, so both definitions were emitted, causing a redefinition error. Fix: define `MODD_USB_MIDI` before the adapter includes.

#### 6. Page key handler return type

The `modd_page_t.key` field expects `int (*)(uint32_t, uint32_t)`. The `fhome_key` handler was declared `void`. JieLi clang 4.0.1 treats this as an error with `-Wincompatible-pointer-types`. Fix: change return type to `int`, return 1.

#### Signal chain (verified working)

```
main loop -> fm1_input_scan() -> note edge detection
  -> fsh_key(note, vel) [Felucca shell]
    -> page->key(note, vel) [fhome_key]
      -> trk_note_on(track, note, vel) [voice allocator]
        -> engine->note_on(track, voice) [NOISE]

ISR (fires every 128 samples @ 44.1kHz):
  isr_alnk0 [assembly wrapper, fm1_isr.S]
    -> fm1_alnk0_irq() [audio.c]
      -> mix_block()
        -> engine_block(track, buf_l, buf_r, n) [voice.c]
          -> engine->render(track, voice, buf, n, &vmod) [NOISE]
      -> interleave L/R into DMA buffer
      -> fm1_audio_ack_half()
```

#### E2E verification

`tests/audio/api_audio_engines_test.py` verifies the engines on the `modd_emu` firmware over its REST API: it switches the selected track's engine (`/api/track/sel/engine/<name>`), plays C4, E4, G4 on the note keys (`/api/input/key/...`) and records the firmware's own output (`/api/audio/capture`, `CONFIG_RPC_AUDIO_TAP`).

- **TestAnalogEngine**, **TestFM6Engine**: audio on each note, at its pitch (the bin at the note's frequency at least 4x the bin two semitones up)
- **TestNoiseEngine**: audio on each note (no pitch check)

Silence before the notes is RMS 0. WAV files and screenshots are saved to `tests/results/<timestamp>-<run>/api_audio_engines_test/`.

#### Emulator encoder timing

The fm1-emu emulator simulates quadrature encoder signals at GPIO level: `enc_state[]` cycles through GRAY code states at 2ms intervals per transition. A single encoder click produces 4 state transitions over ~8ms.

The firmware's main loop scans inputs once per frame. If the frame delay is a single `fm1_delay_ms(10)`, the firmware polls GPIO only once every ~11ms, which means it misses all intermediate quadrature states and sees only the rest position. The encoder appears to not work.

Fix: split the frame delay into 10x 1ms intervals with `fm1_input_scan()` between each:

```c
uint32_t f;
for (f = 0; f < 10u; f++) {
    fm1_delay_ms(1);
    fm1_input_scan();
}
```

This gives the quadrature decoder enough samples to detect transitions. On real hardware, the frame timing is faster and this issue does not occur.

### Effects (pick one upstream)

Each effects chain implementation is selected via Kconfig radio choice. The adapter (`modd_fx.h`) defines the chain interface.

### Modulation

Optional 4-slot modulation matrix. Adapter: `modd_mod.h`.

---

## Sequencer

Step sequencer, pattern management, recording. Depends on Audio.

Kconfig: `CONFIG_SEQ_FELUCCA`, `CONFIG_SEQ_SLOOP`, `CONFIG_SEQ_MELODEE`.

### Lineages

- **Felucca**: 64-step, chords, ties, live loop
- **SLOOP**: arranger, scenes
- **Melodee**: 64-step, 8 banks, 1024 notes, recording, undo, motion, song

### Porting

The sequencer adapter (`modd_seq.h`) defines `seq_steps()` and `seq_counting()`. The UI reads step data through the audio adapter's `step_t` type.

Porting a sequencer (class INCLUDED):
1. Include the upstream sequencer source behind `modd_seq.h`. Do not write a new sequencer against the interface.
2. Where it needs fields `track_t` / `step_t` lack (step conditions, nudge, motion locks), add them to the adapter types.
3. Add the Kconfig choice under `kconf/sequencer.kconfig` and the conditional include in `modd.c`.

The Felucca sequencer in the tree today (`firmware/src/seq/seq.c`, 323 lines from upstream's 1,500) is REDUCED: count-in, metronome, song chain, motion automation, step conditions, NUDGE timing and external MIDI clock were cut because the adapter types had no fields for them. Do not add them back by hand; make upstream's `seq.c` includable.

---

## MIDI

Routing, clock sync, transport control.

Kconfig: `CONFIG_MIDI_UART` (TRS), `CONFIG_USB` (USB).

### Routing (pick one upstream)

- **Felucca**: basic routing
- **SLOOP**: basic routing
- **Melodee**: multi-channel, MPC pads, clock sources

### Transports (enable any)

- UART (TRS MIDI)
- USB MIDI
- BLE MIDI (requires `CONFIG_BLE`)
- USB Audio (Melodee only, stereo/4-track)

### Porting

The MIDI adapter (`modd_midi.h`) defines the routing interface. Transport backends are independent modules enabled by their own Kconfig symbols.

---

## Storage

Flash storage, project save/load, presets.

Kconfig: `CONFIG_FLASH`.

### Lineages

- **Felucca**: basic project storage
- **SLOOP**: basic project storage
- **Melodee**: FBKG format, 384 presets, migration, backup/restore

### Porting

The storage adapter (`modd_storage.h`) defines the tagged-blob project format interface. Each upstream implements the concrete serialization.

---

## Scales

Scale selection, key quantization, tuning.

### Lineages

- **Felucca**: 16 scales, white-key QNT
- **Melodee**: 70 scales incl. 54 microtonal, concert pitch 400-480Hz

### Porting

Scale data is a static table. Porting a scale set means providing the table and registering it via Kconfig.

---

## Boot

Startup, loader, OTA update mechanism.

### Boot chain

```
on-chip ROM -> boot stub (crt0_stub.S) -> loader (decompresses app) -> fm1_cstart
```

The boot stub (`firmware/stub/`) is the first code from flash. It initializes the CPU minimally and jumps to the loader. The loader decompresses the main application into RAM. `fm1_cstart` initializes BSS, copies data sections, and calls `fm1_main()`.

LCD is not available until `fm1_main()` calls `lcd_init()`. The boot logo appears immediately after.

### OTA

All three upstreams share the M-UPGRADE SysEx protocol for USB-MIDI OTA updates. Single app area, no dual-bank.

### Lineages

- **Felucca**: stock DFU
- **SLOOP**: stock DFU
- **Melodee**: M-UPGRADE SysEx OTA, boot protection, safe-recovery

---

## BLE

Bluetooth Low Energy protocol stack. Depends on HAL (radio registers).

Kconfig: `CONFIG_BLE`.

### Optimist upstream

- Link Layer
- Host (L2CAP, ATT, SMP)
- GATT
- BT Classic (BR/EDR, L2CAP, LMP, AVDTP)

Not yet ported. Enables BLE MIDI transport when integrated.

---

## HAL

Registers and low-level hardware access. Header-only. No logic, no state.

### Common (always included)

cc, time, gpio, xip, adc

### Variant slots (pick one per slot via Kconfig)

| Slot | Felucca | SLOOP | Melodee |
|---|---|---|---|
| sys | yes | yes | yes |
| irq | yes | yes | yes |
| lcd | yes | shared w/ Melodee | yes |
| input | yes | yes | yes |
| guard | yes | yes | yes |
| flash | yes | yes | yes |
| uart | yes | shared w/ Felucca | yes |
| usb | yes | shared w/ Felucca | yes |
| timer | yes | shared w/ Felucca | yes |
| multicore | yes | yes | yes |

### Extensions (optional)

- **Melodee**: cache, clock, power, eq, led_pwm
- **Optimist**: ble_rf
- **Felucca**: led_anim

### Porting a HAL variant

HAL files are header-only register macros. Porting means creating `fm1_<slot>_<upstream>.h` with the same function signatures as the existing variant. Kconfig radio choice selects which header gets included.
