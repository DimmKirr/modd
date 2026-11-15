# modd UI Navigation

Auto-generated from `firmware/src/ui/themes/modd/pages.c` and `firmware/adapters/modd_controls.h`.

Regenerate: `task docs:themes:ui:diagram`

- **Solid arrows** (`-->`) = button tap navigation
- **Dashed arrows** (`-.->`) = button hold navigation
- **Dotted bi-arrows** (`<-..->`) = encoder cycling within a view
- **⚙ labels** = Kconfig guards (page compiled only when enabled)

<!-- ui-nav-diagram:start -->
```mermaid
%%{init: {"flowchart": {"curve": "linear"}} }%%
flowchart TD

    BOOT(["Boot Splash"]):::boot
    BOOT --> sg_HOME

    subgraph sg_HOME["Home"]
        P_FILES_HOME["Files<br/><small>⚙ HOME_PAGE_FILES</small>"]
        P_HOME["modd<br/><small>⚙ !HOME_PAGE_FILES</small>"]
    end

    P_DEBUG["Log<br/><small>⚙ DEBUG_LOG_BUF</small>"]

    subgraph sg_CONN["Connectivity"]
        P_WEATHER["Weather<br/><small>⚙ LWIP</small>"]
        P_DEMO["DEMO<br/><small>⚙ PAGE_DEMO</small>"]
    end

    P_SETTINGS["Settings<br/><small>⚙ SYS_MENU</small>"]

    subgraph sg_DEMO["Demo"]
        P_FILES_DEMO["Files<br/><small>⚙ UDISK · !HOME_PAGE_FILES</small>"]
        P_DISK["Disk<br/><small>⚙ UDISK</small>"]
    end

    %% Encoder: cycle pages within a view
    P_WEATHER <-..-> P_DEMO
    P_FILES_DEMO <-..-> P_DISK

    %% Buttons: tap navigates between views
    BTN_HOME[/"HOME"/] --> sg_HOME

    %% Buttons: hold navigates to secondary views
    BTN_FX_H[/"FX hold"/] -.-> sg_CONN
    BTN_GLO_H[/"GLO hold"/] -.-> P_DEBUG
    BTN_HOME_H[/"HOME hold"/] -.-> P_SETTINGS
    BTN_SAVE_H[/"SAVE hold"/] -.-> sg_DEMO

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
| Files |  |  |  |  |  |  | OCTDN |
| modd |  |  |  |  |  |  |  |
| Settings |  |  |  |  |  |  | OCTUP, OCTDN |
| Weather |  |  |  |  |  |  | PLAY |
| DEMO |  |  |  |  |  |  |  |
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
