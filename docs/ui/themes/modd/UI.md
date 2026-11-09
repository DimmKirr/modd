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

    P_WEATHER["Weather"]

    subgraph sg_SETTINGS["Settings"]
        P_SETTINGS["Menu"]
        P_COLORS["Colors"]
        P_CONN["Connectivity"]
    end

    subgraph sg_DEMO["Demo"]
        P_FILES["FILES<br/><small>⚙ UDISK</small>"]
        P_DEMO["DEMO<br/><small>6 screens</small>"]
    end

    %% Encoder: cycle pages within a view
    P_SETTINGS <-..-> P_COLORS
    P_COLORS <-..-> P_CONN
    P_FILES <-..-> P_DEMO

    %% Buttons: tap navigates between views
    BTN_HOME[/"HOME"/] --> P_HOME

    %% Buttons: hold navigates to secondary views
    BTN_GLO_H[/"GLO hold"/] -.-> P_WEATHER
    BTN_HOME_H[/"HOME hold"/] -.-> sg_SETTINGS
    BTN_FX_H[/"FX hold"/] -.-> sg_DEMO

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
| PLAY | Play/stop |
| REC | Record toggle |
| Select (enc) | Cycle pages within current view (when no page claims it) |

## Controls per page

| Page | Select | Algo | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Buttons |
|------|--------|------|--------|--------|--------|--------|---------|
| modd |  |  |  |  |  |  |  |
| Menu | scroll cursor |  |  |  |  |  |  |
| Colors |  |  |  |  |  |  |  |
| Connectivity | toggle wifi |  |  |  |  |  |  |
| Weather |  |  |  |  |  |  |  |
| FILES | scroll entries |  |  |  |  |  | SAVE: open dir, GLO: go up |
| DEMO | switch screen |  | per-screen | per-screen |  |  |  |
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
