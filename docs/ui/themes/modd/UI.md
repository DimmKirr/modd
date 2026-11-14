# modd UI Navigation

Auto-generated from `firmware/src/ui/themes/modd/pages.c` and `firmware/adapters/modd_controls.h`.

Regenerate: `task docs:ui-map -- modd`

- **Solid arrows** (`-->`) = button tap navigation
- **Dashed arrows** (`-.->`) = button hold navigation
- **Dotted bi-arrows** (`<-..->`) = encoder cycling within a view
- **⚙ labels** = Kconfig guards (page compiled only when enabled)

<!-- ui-nav-diagram:start -->
```mermaid
%%{init: {"flowchart": {"curve": "linear"}} }%%
flowchart TD

    BOOT(["Boot Splash"]):::boot
    BOOT --> P_HOME

    P_HOME["modd"]

    P_DEBUG["Log<br/><small>⚙ DEBUG_LOG</small>"]

    subgraph sg_CONN["Connectivity"]
        P_WEATHER["Weather<br/><small>⚙ LWIP</small>"]
        P_DEMO["DEMO"]
    end

    P_SETTINGS["Settings"]

    subgraph sg_DEMO["Demo"]
        P_FILES["Files<br/><small>⚙ UDISK</small>"]
        P_DISK["Disk<br/><small>⚙ UDISK</small>"]
    end

    %% Encoder: cycle pages within a view
    P_WEATHER <-..-> P_DEMO
    P_FILES <-..-> P_DISK

    %% Buttons: tap navigates between views
    BTN_GLO[/"GLO"/] --> P_DEBUG
    BTN_HOME[/"HOME"/] --> P_HOME

    %% Buttons: hold navigates to secondary views
    BTN_FX_H[/"FX hold"/] -.-> sg_DEMO
    BTN_GLO_H[/"GLO hold"/] -.-> sg_CONN
    BTN_HOME_H[/"HOME hold"/] -.-> P_SETTINGS

    classDef boot fill:#E8E4DF,stroke:#6E675E,color:#333
```
<!-- ui-nav-diagram:end -->

<!-- ui-controls-table:start -->
## Global controls

Always active, handled by the shell or keyboard overlay:

| Control | Action |
|---------|--------|
| Presets (enc) | Cycle presets |
| Master (pot) | Master volume |
| OCT+ / OCT- | Octave shift |
| PLAY | Play/stop (page-specific on WIFI, WEATHER, BLE) |
| REC | Record toggle |
| Select (enc) | Cycle pages within current view (when no page claims it) |

## Controls per page

| Page | Select | Algo | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Buttons |
|------|--------|------|--------|--------|--------|--------|---------|
| modd |  |  |  |  |  |  |  |
| Settings |  |  |  |  |  |  | OCTUP, OCTDN |
| Weather |  |  |  |  |  |  | PLAY |
| DEMO |  |  |  |  |  |  |  |
| Files |  |  |  |  |  |  | OCTDN |
| Disk |  |  |  |  |  |  |  |
| Log |  |  |  |  |  |  |  |
<!-- ui-controls-table:end -->

## DEMO screens

SELECT cycles 6 screens, K1-K4 adjust values per screen.

| Screen | Cards | Panel area | K1 | K2 |
|--------|-------|------------|----|----|
| CARDS | FRA, SCR, K1, NOTE | Theme color swatches | Counter (0-99) | -- |
| LIST | ITEMS, SEL | Scrollable 16-item list | List selection + auto-scroll | -- |
| GRAPH | BARS, FRAME, ANIM, STYLE | Animated bar chart | Counter (0-99) | -- |
| TEXT | FONT S/M/L, DEMO | Font samples, alignment, rounded rects | Counter (0-99) | -- |
| EDIT | EDIT, LEN, CUR, MAX | Inline text editor with blinking cursor | Move cursor | Change character |
| MODALS | MODALS, SEL, -, STYLE | Mock dialog overlays (YES/NO, OK, Text Input) | Select modal type | -- |
