# modd UI Navigation

The diagram is generated from `firmware/src/ui/shells/modd/pages.c` and
`firmware/src/ui/shells/modd/controls.c`. Regenerate: `task docs:themes:ui:diagram`

- **Solid arrows** (`-->`) = button tap navigation
- **Dashed arrows** (`-.->`) = button hold navigation

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

## Controls

Written by hand: this shell hands its input to LVGL, so there are no knob maps for the
generator to read. The code is `shell.c` (`shell_knob`, `shell_button`) and `port.c`
(`lvt_enc_read`).

LVGL sees one input device, an encoder. SELECT turns it and OCT+ is its push button.
The widgets of the page on screen form the focus group.

| Control | Action |
|---------|--------|
| Select (enc) | Move the focus to the next or previous widget. In an open list: move the choice |
| OCT+ | The encoder's push: flip the focused switch, open the focused list, take the choice |
| OCT- | Back: close an open list and keep the old value |
| HOME | Home (hold: Config) |
| FX (hold) | Connectivity |
| GLO (hold) | Debug; hold again to return to the page before |

A page may take a control before LVGL does:

| Page | Select |
|------|--------|
| Home | Pick the track (T1..T4) |
| Connectivity | Focus |
| Config | Focus |
| Debug | Scroll the log back and forth |

The other encoders, PLAY and REC are not used by this shell. Its LEDs (`leds.c`): the
button of the page on screen, PLAY while playing, REC while armed, held note keys.
