# LVGL on the FM-1

How to edit the `lvgl` theme, what a theme has to get right on this hardware, and where
to look for examples. The theme itself is described in
[themes/lvgl/THEME.md](themes/lvgl/THEME.md); its navigation and controls in
[themes/lvgl/UI.md](themes/lvgl/UI.md).

## Read the 9.6 manual first

modd vendors LVGL **v9.6.0** and its manual at the same commit (`vendir.yml`):

| What | Where |
|------|-------|
| Library source | `firmware/vendor/lvgl/` (never edited) |
| Public headers | `firmware/vendor/lvgl/include/lvgl/` |
| Manual | `docs/lvgl/docs/src/**/*.mdx` |
| Examples (307 C files) | `docs/lvgl/examples/` |

Both trees are fetched by `task vendor:ensure`, which every build runs.

LVGL 8 and LVGL 9 differ in names, signatures and behaviour, and most tutorials on the
web are for LVGL 8. Work from the vendored manual:

- A widget: `docs/lvgl/docs/src/widgets/<name>.mdx`, with its examples in
  `docs/lvgl/examples/widgets/<name>/`.
- Styles and themes: `common-widget-features/styles/`.
- The encoder and focus groups: `main-modules/indev/`.
- Subjects and binding: `main-modules/data_binding/`.
- Display and flushing: `main-modules/display/`.

Where the manual and a header disagree, the header is what compiles. One known case: the
manual shows `lv_theme_default_init()` with seven arguments; the 9.6 header takes five
(display, primary colour, secondary colour, dark, font).

## The UI is C: there are no XML files

LVGL's XML engine was removed from the open-source repository in v9.5.0 (the changelog
lists it as a breaking change). XML authoring, the editor and the viewer are now LVGL
Pro, a commercial product (`docs/lvgl/docs/src/xml.mdx`, `introduction/license.mdx`).
With the LVGL that modd vendors, a page is C code calling the widget API.

## Where things are

Everything is under `firmware/src/ui/themes/lvgl/`.

| To change | Edit |
|-----------|------|
| Accent colours, light or dark | `shell.c`, the `lv_theme_default_init()` call |
| The theme's own colours | `chrome.c`, the `LVT_C_*` macros |
| Styles of the header, footer, body and cards | `chrome.c`, `lvt_styles_init()` |
| How big and how far apart widgets are | `port.c`, `LVT_DPI` (see [Density](#density-this-is-not-a-touch-screen)) |
| Size of a switch or a drop-down list | `settings.c`, `lvt_row_add()` |
| An icon | the `LV_SYMBOL_*` in the string where it is used (see below) |
| Fonts | `lv_conf.h`, `LV_FONT_MONTSERRAT_*` and `LV_FONT_DEFAULT` |
| Which widgets exist | `lv_conf.h`, `LV_USE_<WIDGET>` |
| LVGL's heap size | `lv_conf.h`, `LV_MEM_SIZE` |
| A page's content | `page_<name>.c` |
| What a page can show (firmware state) | `model.c` |
| The header and footer of every page | `chrome.c`, `lvt_page_begin()` |
| Which button opens which page | `controls.c` |
| How SELECT, OCT+ and OCT- act | `shell.c` and `port.c` (`lvt_enc_read`) |
| LEDs | `leds.c` |
| Strip height, draw buffers, the LCD hook | `port.c` |
| The UI task's stack | `firmware/src/modd.c`, `ui_stack` under `CONFIG_UI_LVGL` |
| What the firmware build contains | `configs/modd_defconfig` |

A setting needs no theme change: `settings.c` builds a switch or a drop-down list for
every item of `SYS_MENU[]` (`firmware/adapters/modd_sys_menu.h`).

Changing `lv_conf.h` recompiles the whole library (483 files, about 3 minutes). Changing
the theme's `.c` files does not.

## Where widgets and icons come from

Nothing on screen is an image file.

| Element | Lives in | Edited in the theme by |
|---------|----------|------------------------|
| A widget's behaviour and drawing (switch, drop-down, arc, bar, LED, label) | `firmware/vendor/lvgl/src/widgets/<name>/`, compiled in when `LV_USE_<NAME>` is 1 in `lv_conf.h` | Not edited: the code draws it from rectangles, arcs and text at run time |
| A widget's look (colours, corners, paddings, focus outline) | The active LVGL theme, `firmware/vendor/lvgl/src/themes/default/lv_theme_default.c` | The arguments of `lv_theme_default_init()` in `shell.c`, `LVT_DPI` in `port.c`, and styles added on top (`chrome.c`, or `lv_obj_set_style_*` where the widget is created) |
| Where a widget is, and how big | The page that creates it | `settings.c` (switches, lists), `page_home.c` (cards, arcs, bar), `page_conn.c` (LED), `chrome.c` (header, footer) |
| An icon (home, WiFi, gear, play, stop, list) | A glyph of the built-in font: `LV_SYMBOL_*` in `firmware/vendor/lvgl/include/lvgl/font/lv_symbol_def.h` (63 symbols), bitmaps in `firmware/vendor/lvgl/src/font/lv_font_montserrat_<size>.c` | The string it is part of, e.g. `LV_SYMBOL_HOME " HOME"` in `page_home.c` |

An icon is text, so it takes the label's font size and colour. For an icon that is not
among the 63, either generate a font that contains it or use an `lv_image` with a C
array (`docs/lvgl/docs/src/main-modules/fonts/`, `widgets/image.mdx`); both add flash.

## Adding a page

1. Write `page_<name>.c`. Its `enter` function calls `lvt_page_begin(title, hint)` and
   adds widgets to the body that call returns. Declare a `modd_page_t` with a `VIEW_*`,
   `.enter`, and `.status` if something has to be polled.
2. Include the file in `pages.c` and register the page in `shell_register_pages()`.
3. Give a button the page's view in `controls.c`.
4. For firmware state, add a subject to `model.c`, set it in `lvt_model_poll()`, and
   bind the widget to it.
5. Regenerate the navigation diagram: `task docs:themes:ui:diagram`.

## Rules for a correct theme

Each of these comes from the hardware or from something that went wrong while the theme
was built.

### Calls and tasks

- **Call LVGL from the UI task only.** The manual: "LVGL is not thread-safe". Page
  callbacks and `shell_*` run in the UI task; an interrupt, the services task or an API
  handler must not create, change or delete widgets. Hand them a flag or a subject value
  that the UI task picks up. A page change asked for over the REST API works this way:
  `nav_request()` (`firmware/src/ui/ui_nav.c`) stores it under `UI_NAV_DEFERRED`, and
  `shell_draw()` builds the page with `nav_take_request()`.
- **Read the firmware through the shared page data.** `firmware/src/ui/page_data.c`
  (`home_read()`, `net_read()`, `wx_read()`, `sys_menu_is_conn()`) is what the modd theme
  draws and what the REST API serializes (`firmware/src/rpc/api_pages.c`). `model.c`
  copies it into subjects, so the screen and `/api/page` show the same values. A new
  page's data goes there, with its serializer, under the page's `.name`.
- **Build in `.enter`, never in a draw callback.** LVGL keeps the widgets drawn. A
  page's `.draw` is unused in this theme.
- **Use the 9.6 names.** `lv_obj_add_flag()` and `lv_obj_remove_flag()` are deprecated
  in 9.6 in favour of setters such as `lv_obj_set_hidden()` and
  `lv_obj_set_clickable()`. The build prints a deprecation warning; keep it at none.

### Memory

- **Big buffers go in `.pool`.** The `.bss` region is 96 KB and nearly full in the
  product build. LVGL's heap and the two draw buffers are placed in `.pool` already;
  do the same for anything large (`__attribute__((section(".pool")))`).
- **Watch the heap.** `LV_MEM_SIZE` is 32 KB. A page is deleted after the next one is
  built, so the peak is two pages at once: 17.8 KB measured. Every widget, every local
  style (`lv_obj_set_style_*`) and every non-static label text comes from this heap.
- **Share styles.** Initialise an `lv_style_t` once in `lvt_styles_init()` and add it
  to widgets. A local style costs heap for each widget it is set on.
- **`lv_label_set_text_static()` only for text that outlives the label**, such as a
  string literal. For anything built at run time use `lv_label_set_text()` or
  `lv_label_set_text_fmt()`, which copy.
- **Watch the stack.** LVGL lays out and draws through nested calls, so nesting
  containers deepens the stack. The UI task has 12,288 bytes; the peak measured is
  7,196. The first version of this theme crashed on a 2,560-byte stack.

After a change, open every page, open a list, then read the Debug page (hold GLO):
`heap used/size  max`, `stack peak/size`, `draw max ms`.

### Display

- **240x240, RGB565 with swapped bytes, drawn in strips of 24 rows.** `lv_conf.h` sets
  `LV_COLOR_FORMAT_RGB565_SWAPPED`, so pixels leave in the panel's byte order.
- **The SPI bus is the limit.** A whole screen is 115,200 bytes, about 77 ms at
  12 MHz. A page change measured 14 ms to build and 72 ms to draw. LVGL redraws only
  what changed, so a small change is fast.
- **Do not animate large areas continuously.** A scrolling label or a spinner keeps the
  bus busy on every frame, and while LVGL waits for a strip the UI task does not yield
  to the other tasks.
- **No shadows, blur, gradients or transforms unless measured.** They are software
  rendered, and only the colour formats listed in `lv_conf.h` are compiled in.

### Density: this is not a touch screen

LVGL's default theme sizes paddings, borders and corners in density-independent units,
`pixels = dp * dpi / 160`, and its default density of 130 gives finger-sized targets: a
drop-down list 37 pixels high and a 52 x 30 switch on a 240-pixel screen. The FM-1 is
driven by an encoder, so nothing has to be large enough to hit.

- `port.c` tells the display a density of 80 (`LVT_DPI`) before the theme is
  initialised. Lower packs closer; LVGL never goes below 1 pixel.
- A widget's own default size does not follow that setting (a switch is always
  52 x 30 unless sized): set it where the widget is created, as `settings.c` does.
- A community theme may be built for touch as well; check before adopting one.

### Input

- **One encoder.** SELECT turns it, OCT+ is its push, OCT- sends "escape". There is no
  touch, so a widget is reachable only through the focus group.
- **Widgets join the focus group when created.** Take a decorative widget out with
  `lv_group_remove_obj()` and `lv_obj_set_clickable(w, false)`, or SELECT will stop on
  it. The arcs on Home are done this way.
- **Leave room for the focus outline.** The default theme draws it outside the widget.
  A parent clips its children, so the row needs `lv_obj_set_overflow_visible(row, true)`
  and `lv_obj_set_ext_draw_size(row, 6)`, and the body needs side padding.
- **Let the body scroll one way.** `lv_obj_set_scroll_dir(body, LV_DIR_VER)`; otherwise
  focusing a widget at the edge shifts the page sideways.
- **A page that takes SELECT itself** (Home, Debug) returns 1 from `.knob`; then LVGL
  does not see the turn.

### Text and fonts

- Only the Montserrat sizes enabled in `lv_conf.h` exist (10, 14, 20). Enabling a size
  adds flash; an enabled size that nothing uses is dropped by the linker.
- The built-in fonts cover ASCII and LVGL's symbols (`LV_SYMBOL_*`). Other scripts need
  a font generated for them.
- Footer hints are one line of the 10-pixel font: about 43 characters fit.

## Changing the look

| Option | How |
|--------|-----|
| Other colours on the default theme | Two palette colours and the dark flag in `lv_theme_default_init()` |
| Light instead of dark | The `dark` argument, and `LV_THEME_DEFAULT_DARK` in `lv_conf.h` |
| Another built-in theme | LVGL ships `default`, `simple` and `mono` (`firmware/vendor/lvgl/src/themes/`); enable it in `lv_conf.h` |
| A theme of your own | Layer it over a built-in one with `lv_theme_set_parent()` (`common-widget-features/styles/themes.mdx`, example `docs/lvgl/examples/styles/lv_example_style_theme.c`) |
| A community theme | See below; vendor it through `vendir.yml` like LVGL, never copy it in |

A theme only styles widgets. Page layout stays in the `page_*.c` files.

## Build, preview, measure

```sh
task build -- --defconfig modd          # or: task modd:build:modd
```

| Preview | Command or place |
|---------|------------------|
| Screenshots | `docs/ui/themes/lvgl/screenshots/`; regenerate with `uv run python tools/codegen/gen_ui_screenshots.py lvgl` |
| Native emulator | `$FM1_EMU_ROOT/target/release/fm1-live --fwsc build/modd/modd.fwsc --wifi oss --net internet` |
| API tests, with LCD screenshots | `task test:api` (build `modd_emu`; tests in `tests/ui/themes/lvgl/api/`; pictures in `tests/results/<time>-lvgl_api_test/screenshots/lcd/`) |
| Browser emulator | serve `$FM1_EMU_ROOT/_site/` and load `build/modd/modd.fwsc` (not yet tried with this theme) |
| Hardware-like timing | add `FM1_STRICT=1`: timed SPI, faults on misaligned access |

For the emulator's WiFi the firmware must be built with its access point:
`WIFI_SSID=Home WIFI_PASSWORD=fm1pass task build -- --defconfig modd`. Without
`--wifi oss` the WiFi task scans for a chip that is not there and holds the CPU for
about 450 ms at a time; the UI is sluggish and short presses can be lost.

There is no stand-alone HTML preview of the theme. The emulators run the real firmware,
which is the preview.

Size comes from the linker map, `build/modd/modd.map`; the figures measured so far
are in [THEME.md](themes/lvgl/THEME.md#measured-cost).

## Tools and projects to learn from

Checked on 2026-11-19. None of these is used by modd today.

| Project | Licence | What it is | Notes |
|---------|---------|-----------|-------|
| [LVGL](https://github.com/lvgl/lvgl) | MIT | The library | Its `examples/` are vendored at `docs/lvgl/examples/` |
| [lv_web_emscripten](https://github.com/lvgl/lv_web_emscripten) | MIT | Compiles an LVGL UI in C to a web page | The manual's route for a browser preview (`integration/pc/browser.mdx`). The theme reads firmware state, so it would need stub data first |
| [lv_port_pc_vscode](https://github.com/lvgl/lv_port_pc_vscode) | MIT | LVGL on a PC with SDL | The usual way to try widgets without hardware |
| [sim.lvgl.io](https://sim.lvgl.io) | MIT | Online simulator for LVGL's MicroPython binding | Quick widget experiments in a browser, in Python |
| [EEZ Studio](https://github.com/eez-open/studio) | GPL-3.0 | Desktop drag-and-drop editor that generates C | Its releases name LVGL up to 9.5.0, not 9.6. It generates its own UI code and does not open hand-written pages |
| [lvgl_theme_shadcn](https://github.com/AlixANNERAUD/lvgl_theme_shadcn) | MIT | A complete theme: light and dark palettes, brand colour | Requires LVGL 9.6 or newer. A small project; no size figures published; not built for the FM-1 yet |
| [LSS](https://github.com/ByteWelder/LSS) | not checked | Generates `lv_theme_<name>.c` from a stylesheet-like file | LVGL version support not checked |
| [LVGL forum](https://forum.lvgl.io) | | Questions and project write-ups | Check which LVGL version an answer is for |

Not open source: GUI Guider (NXP, free), SquareLine Studio and LVGL Pro (commercial).

No curated library of LVGL 9 themes was found: the built-in three and single community
projects like the ones above are what exists.

## Open points

- The theme has run in the emulator only, not on an FM-1.
