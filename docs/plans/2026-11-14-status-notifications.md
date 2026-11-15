# Status Notifications Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add status feedback for long-running background processes (WiFi, DHCP, weather, USB) via three UI components: toast overlay, header icon row, and settings live status.

**Architecture:** A single-slot toast module (`toast.c`) provides `toast_status()`/`toast_error()`/`toast_clear()` callable from any subsystem via `modd_toast.h`. The shell's frame loop calls `toast_tick()` (timer advance) and `toast_draw()` (footer overlay). Header icons extend `mt_render_header()` with color-coded state glyphs. The settings page polls network state for live text.

**Tech Stack:** C (bare-metal, unity build, all functions `static`), Kconfig guards, Kconfiglib genconfig, Python e2e tests with Playwright + fm1-emu WASM emulator.

**Design doc:** `.scratch/tasks/todo/2026-11-14-status-notifications-design.md`

---

### Task 1: Toast adapter header (`modd_toast.h`)

**Files:**
- Create: `firmware/adapters/modd_toast.h`

**Step 1: Create the header**

```c
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Toast notification adapter: a single-slot overlay for status/error messages.
 * Subsystems call these from their own files guarded by #ifdef CONFIG_UI_MODD.
 * The implementation lives in firmware/src/ui/themes/modd/toast.c. */
#ifndef MODD_TOAST_H
#define MODD_TOAST_H

#define TOAST_NONE   0
#define TOAST_STATUS 1
#define TOAST_ERROR  2

static void toast_status(const char *msg);
static void toast_error(const char *msg);
static void toast_clear(void);
static int  toast_active(void);

#endif /* MODD_TOAST_H */
```

Note: `static` declarations are correct here. This is a unity build: every `.h` and `.c` is `#include`d into a single translation unit. The actual definitions come from `toast.c` (included later in the TU). These forward declarations let earlier files (wifi, ecm) call the toast API.

**Step 2: Commit**

```bash
git add firmware/adapters/modd_toast.h
git commit -m "feat(ui): add toast notification adapter header"
```

---

### Task 2: Toast module implementation (`toast.c`)

**Files:**
- Create: `firmware/src/ui/themes/modd/toast.c`
- Modify: `firmware/src/ui/themes/modd/ui.c:17-18` (add include)

**Step 1: Create `toast.c`**

The toast is a single-slot notification drawn over the footer (y=212, h=28). Status toasts show a 3-dot ellipsis spinner; error toasts show `!` and auto-dismiss after 5s.

```c
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Toast overlay: single-slot status/error notification drawn over the footer.
 * API: toast_status(), toast_error(), toast_clear(), toast_active().
 * Lifecycle: toast_tick() in shell_draw before dirty check, toast_draw() after footer. */

#define TOAST_TIMEOUT_MS   5000u
#define TOAST_DOT_MS       300u
#define TOAST_MSG_MAX      32

static struct {
    char     msg[TOAST_MSG_MAX];
    uint8_t  kind;           /* TOAST_NONE / TOAST_STATUS / TOAST_ERROR */
    uint32_t start_ms;       /* fm1_ms when shown */
    uint8_t  dot_phase;      /* 0..2 for 3-dot spinner */
    uint32_t dot_next_ms;    /* next dot advance */
} toast;

static void toast_status(const char *msg)
{
    uint32_t i = 0;
    while (i < TOAST_MSG_MAX - 1u && msg[i]) {
        toast.msg[i] = msg[i];
        i++;
    }
    toast.msg[i] = 0;
    toast.kind = TOAST_STATUS;
    toast.start_ms = fm1_ms;
    toast.dot_phase = 0;
    toast.dot_next_ms = fm1_ms + TOAST_DOT_MS;
    nav_dirty = 1;
}

static void toast_error(const char *msg)
{
    uint32_t i = 0;
    while (i < TOAST_MSG_MAX - 1u && msg[i]) {
        toast.msg[i] = msg[i];
        i++;
    }
    toast.msg[i] = 0;
    toast.kind = TOAST_ERROR;
    toast.start_ms = fm1_ms;
    toast.dot_phase = 0;
    toast.dot_next_ms = 0;
    nav_dirty = 1;
}

static void toast_clear(void)
{
    if (toast.kind == TOAST_NONE) return;
    toast.kind = TOAST_NONE;
    nav_dirty = 1;
}

static int toast_active(void)
{
    return toast.kind != TOAST_NONE;
}

static void toast_tick(void)
{
    if (toast.kind == TOAST_NONE) return;

    if (toast.kind == TOAST_ERROR) {
        if (fm1_ms - toast.start_ms > TOAST_TIMEOUT_MS) {
            toast_clear();
            return;
        }
    }

    if (toast.kind == TOAST_STATUS && (int32_t)(fm1_ms - toast.dot_next_ms) >= 0) {
        toast.dot_phase = (toast.dot_phase + 1u) % 3u;
        toast.dot_next_ms = fm1_ms + TOAST_DOT_MS;
    }

    nav_dirty = 1;
}

static void toast_draw(void)
{
    if (toast.kind == TOAST_NONE) return;

    int32_t tx = 10;
    cv_begin(UI_W, UI_FOOT_H, T_SURF);
    cv_rrect(2, 2, UI_W - 4, UI_FOOT_H - 4, 6, T_SURF, T_BG);

    if (toast.kind == TOAST_STATUS) {
        /* 3-dot ellipsis spinner: the active dot is T_TEXT, others T_DIM */
        uint8_t d;
        for (d = 0; d < 3u; d++) {
            uint16_t c = (d == toast.dot_phase) ? T_TEXT : T_DIM;
            cv_circle(tx + 4 + (int32_t)d * 8, UI_FOOT_H / 2, 2, c);
        }
        tx += 32;
    } else {
        /* error: ! glyph in T_REC */
        cv_text(tx, ty_y(UI_FOOT_H / 2, TY_LB), TY_LB, "!", T_REC);
        tx += 14;
    }

    uint16_t tc = (toast.kind == TOAST_ERROR) ? T_TEXT : T_TEXT;
    cv_text_fit(tx, ty_y(UI_FOOT_H / 2, TY_LB), TY_LB,
                toast.msg, tc, T_SURF, UI_W - tx - 8);

    cv_blit(0, UI_Y_FOOT);
}
```

**Step 2: Wire into `ui.c` include order**

In `firmware/src/ui/themes/modd/ui.c`, add the toast include between `tooltip.c` and `shell.c`:

```c
#include "ui/themes/modd/tooltip.c"
#include "ui/themes/modd/toast.c"      /* <-- new */
#include "ui/themes/modd/shell.c"
```

This ensures `toast.c` definitions are available when `shell.c` calls `toast_tick()` and `toast_draw()`.

**Step 3: Wire into `shell.c`**

In `firmware/src/ui/themes/modd/shell.c`, modify `shell_draw()` (currently at line 46):

Add `toast_tick();` after `p->status()` (line 53) and before the dirty check (line 55):

```c
    if (p->status)
        p->status();

    toast_tick();

    if (!nav_dirty)
        return;
```

Add `toast_draw();` after the `gfx_frame(0)` / before `nav_dirty = 0`:

Wait, looking at the current code more carefully: `gfx_frame(0)` is the LCD flush. The toast must draw *before* `gfx_frame(0)`. Insert `toast_draw();` just before `gfx_frame(0)` at line 75:

```c
    if (nav_modal && nav_modal->draw)
        nav_modal->draw();
    toast_draw();
    gfx_frame(0);
    nav_dirty = 0;
```

**Step 4: Build and verify**

Run: `task build`

Expected: clean build with no errors. The toast module compiles but is never called yet, so no visible change.

**Step 5: Commit**

```bash
git add firmware/src/ui/themes/modd/toast.c firmware/src/ui/themes/modd/ui.c firmware/src/ui/themes/modd/shell.c
git commit -m "feat(ui): add toast notification module with spinner and error overlays"
```

---

### Task 3: Include `modd_toast.h` in `modd.c`

**Files:**
- Modify: `firmware/src/modd.c` (add `#include "modd_toast.h"` near the other adapter headers)

**Step 1: Add the include**

Find the block of `#include "modd_*.h"` adapter headers in `modd.c` and add `modd_toast.h` after `modd_page.h` (or any other adapter). The exact location depends on what's in the file; it must come *before* the WiFi/ECM/weather source includes so those files can call `toast_status()` etc.

Look for the adapter include block and add:

```c
#include "modd_toast.h"
```

This must be *before* the subsystem includes (wifi_sta.c, wifi_api.c, wifi_lwip.c, usbnet_ecm.c, wifi_weather.c) but *after* basic types. It must also be *before* the UI includes (since toast.c provides the actual definitions).

Actually, since toast.c is included inside ui.c (which is included in modd.c), and the subsystem files are included *before* ui.c, we need the forward declarations from `modd_toast.h` to appear before the subsystem files. The adapter headers are included early in modd.c, before the subsystem files, so this works.

**Step 2: Build**

Run: `task build`

Expected: clean build.

**Step 3: Commit**

```bash
git add firmware/src/modd.c
git commit -m "feat: include modd_toast.h adapter for subsystem toast calls"
```

---

### Task 4: Wire toast calls into WiFi subsystem

**Files:**
- Modify: `firmware/src/wifi/wifi_sta.c` (state transition toast calls)
- Modify: `firmware/src/wifi/wifi_api.c` (connect/disconnect toast calls)
- Modify: `firmware/src/wifi/wifi_lwip.c` (DHCP bound toast clear)

All toast calls are guarded by `#ifdef CONFIG_UI_MODD` so the subsystem still compiles without the modd theme.

**Step 1: Add toast to `wifi_sta_set_state()` (wifi_sta.c:169)**

After `wifi_sta.state = s;` add toast calls based on the new state:

```c
static void wifi_sta_set_state(wifi_state_t s)
{
#ifdef CONFIG_WIFI_DEBUG_AUTOSCAN
    modd_log_int("wifi: sta state=", (int32_t)s);
    if (wifi_sta.resp_log_n > WIFI_STA_RESP_LOG_MAX)
        modd_log_int("wifi: resp frames unlogged=",
                     (int32_t)(wifi_sta.resp_log_n - WIFI_STA_RESP_LOG_MAX));
#endif
    wifi_sta.resp_log_n = 0;
    wifi_sta.state = s;
    wifi_sta.state_enter_ms = wifi_sta_now_ms();
    if (s == WIFI_STATE_ASSOC)
        wifi_sta.assoc_sent = 0;

#ifdef CONFIG_UI_MODD
    if (s == WIFI_STATE_SCANNING)
        toast_status("Scanning WiFi");
    else if (s == WIFI_STATE_AUTH || s == WIFI_STATE_ASSOC || s == WIFI_STATE_WPA)
        toast_status("Connecting WiFi");
#endif
}
```

**Step 2: Add toast on auth/assoc/wpa timeout (wifi_sta.c)**

In `wifi_sta_poll()`, after each timeout sets state to IDLE, add a toast error. There are three timeout sites (lines ~895, ~910, ~920):

After `wifi_sta_set_state(WIFI_STATE_IDLE)` for AUTH timeout (~897):
```c
        if (wifi_sta_timed_out()) {
            wifi_sta_log_tx_fate("wifi: auth timeout");
            wifi_sta_set_state(WIFI_STATE_IDLE);
#ifdef CONFIG_UI_MODD
            toast_error("WiFi auth failed");
#endif
        }
```

Same pattern for ASSOC timeout (~912) and WPA timeout (~922):
```c
            // after wifi_sta_set_state(WIFI_STATE_IDLE) in ASSOC:
#ifdef CONFIG_UI_MODD
            toast_error("WiFi connect failed");
#endif

            // after wifi_sta_set_state(WIFI_STATE_IDLE) in WPA:
#ifdef CONFIG_UI_MODD
            toast_error("WiFi auth failed");
#endif
```

**Step 3: Add toast to `wifi_api.c`**

In `wifi_on_connected()` (line 156), after `wifi_lwip_start_dhcp()`:
```c
static void wifi_on_connected(void)
{
    modd_log("wifi: associated, starting DHCP");
    wifi_mac_tighten_filter();
    wifi_lwip_start_dhcp();
#ifdef CONFIG_UI_MODD
    toast_status("Acquiring IP");
#endif
}
```

In `wifi_on_disconnected()` (line 164):
```c
static void wifi_on_disconnected(void)
{
    modd_log("wifi: disconnected");
    wifi_lwip_stop_dhcp();
#ifdef CONFIG_UI_MODD
    toast_error("WiFi disconnected");
#endif
}
```

**Step 4: Add toast_clear to `wifi_lwip.c`**

In `wifi_lwip_status_cb()` (~line 106), after `net_on_up(nif)`:
```c
static void wifi_lwip_status_cb(struct netif *nif)
{
    if (nif->ip_addr.addr) {
        modd_log("wifi: dhcp bound");
        net_on_up(nif);
#ifdef CONFIG_UI_MODD
        toast_clear();
#endif
    }
}
```

**Step 5: Build**

Run: `task build`

Expected: clean build. Toast calls compile when CONFIG_UI_MODD is set (the default modd defconfig).

**Step 6: Commit**

```bash
git add firmware/src/wifi/wifi_sta.c firmware/src/wifi/wifi_api.c firmware/src/wifi/wifi_lwip.c
git commit -m "feat(wifi): wire toast notifications for connect/DHCP/error states"
```

---

### Task 5: Wire toast calls into ECM (USB ethernet)

**Files:**
- Modify: `firmware/src/net/usbnet_ecm.c`

**Step 1: Add toast to `ecm_attach()` (line ~179)**

After `modd_log("ecm: attached");`:
```c
    modd_log("ecm: attached");
#ifdef CONFIG_UI_MODD
    toast_status("USB ethernet");
#endif
    return 0;
```

**Step 2: Add toast_clear to `ecm_status_cb()` (line ~95)**

After `net_on_up(nif);`:
```c
static void ecm_status_cb(struct netif *nif)
{
    if (nif->ip_addr.addr) {
        modd_log("ecm: dhcp bound");
        net_on_up(nif);
#ifdef CONFIG_UI_MODD
        toast_clear();
#endif
    }
}
```

**Step 3: Add toast_error to `ecm_detach()` (line ~183)**

```c
static void ecm_detach(void)
{
    if (!ecm_up) return;
    dhcp_stop(&ecm_netif);
    netif_set_link_down(&ecm_netif);
    netif_set_down(&ecm_netif);
    net_on_down(&ecm_netif);
    ecm_rx_armed = 0;
    modd_log("ecm: detached");
#ifdef CONFIG_UI_MODD
    toast_error("USB ethernet lost");
#endif
}
```

**Step 4: Build**

Run: `task build`

**Step 5: Commit**

```bash
git add firmware/src/net/usbnet_ecm.c
git commit -m "feat(ecm): wire toast notifications for USB ethernet attach/detach/IP"
```

---

### Task 6: Wire toast calls into weather

**Files:**
- Modify: `firmware/src/wifi/wifi_weather.c`

**Step 1: Add toast to `weather_fetch_start()` (line ~315)**

At the top of the function, after the early return:
```c
static void weather_fetch_start(void)
{
    if (weather_state != WEATHER_IDLE && weather_state != WEATHER_DONE)
        return;
    modd_log("wifi: weather fetch start");
#ifdef CONFIG_UI_MODD
    toast_status("Fetching weather");
#endif
    weather_retries = 0;
```

**Step 2: Add toast_clear to the success path**

In `weather_wx_cb()`, where `weather_state = WEATHER_DONE` is set (line ~259):
```c
    weather_state = WEATHER_DONE;
    weather_last_ms = fm1_ms;
    weather_retries = 0;
#ifdef CONFIG_UI_MODD
    toast_clear();
#endif
```

**Step 3: Add toast_error on final failure**

The weather module retries up to `WEATHER_MAX_RETRIES`. When retries are exhausted (in the WEATHER_IDLE case of `weather_poll()`, line ~393), the state stays IDLE and retries stop. Add a toast error when retries increment *and* have reached the max.

The simplest approach: add toast_error in the geo and wx callbacks when they fail. Since retries happen automatically, show the error only on the last retry. Find each `weather_retries++;` line and add:

```c
#ifdef CONFIG_UI_MODD
    if (weather_retries >= WEATHER_MAX_RETRIES)
        toast_error("Weather unavailable");
#endif
```

There are 5 such sites in `wifi_weather.c` (lines ~156, ~166, ~222, ~340, ~377). Add the guard after each `weather_retries++;`.

**Step 4: Build**

Run: `task build`

**Step 5: Commit**

```bash
git add firmware/src/wifi/wifi_weather.c
git commit -m "feat(weather): wire toast notifications for fetch progress and errors"
```

---

### Task 7: Header icon row

**Files:**
- Modify: `firmware/src/ui/themes/modd/theme.c` (add `mt_render_icons()`, refactor `mt_render_header()`)

**Step 1: Add `mt_render_icons()` before `mt_render_header()`**

This function draws right-aligned status icons in the header. Each icon is 9px wide with 2px gap. Returns the x position to the left of all icons (so the page counter can be placed there).

Insert after `mt_wifi_bars()` (line ~403) and before `mt_render_header()` (line ~407):

```c
static int32_t mt_render_icons(int32_t rx)
{
    /* icons render right-to-left from rx */

#ifdef CONFIG_USB_HOST
    {
        uint16_t c = T_DIM;
        /* USB drive icon: small rectangle with a bump */
        /* only show when host mode is active */
        if (otg.mode == OTG_HOST) {
            if (uhost.state == UHOST_MOUNTED) c = T_TEXT;
            rx -= 2;
            cv_rect(rx - 7, UI_HEAD_H - 12, 7, 8, c);
            cv_rect(rx - 5, UI_HEAD_H - 14, 3, 2, c);
            rx -= 9;
        }
    }
#endif

#ifdef CONFIG_BLE
    {
        uint16_t c = T_DIM;
        if (ble_is_connected()) c = T_TEXT;
        else if (!ble_is_on()) c = 0; /* hidden */
        if (c) {
            rx -= 2;
            cv_text(rx - 8, ty_y(UI_HEAD_H / 2, TY_LB), TY_LB, "B", c);
            rx -= 10;
        }
    }
#endif

#ifdef CONFIG_USB_NET
    if (ecm_is_up()) {
        uint16_t c = net_get_ip() ? T_TEXT : T_DIM;
        rx -= 2;
        cv_text(rx - 8, ty_y(UI_HEAD_H / 2, TY_LB), TY_LB, "E", c);
        rx -= 10;
    }
#endif

#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
    {
        uint32_t wb = mt_wifi_bars();
        rx -= 2;
        mt_wifi_icon(rx - 14, UI_HEAD_H - 6, wb, wb == 3u ? T_THEME : T_MID, T_LINE);
        rx -= 16;
    }
#endif

    return rx;
}
```

Note: The BLE and USB_HOST checks use functions (`ble_is_connected()`, `ble_is_on()`) that must already exist in the codebase under their respective CONFIG guards. Verify they exist before adding those blocks. If they don't exist, omit those icon blocks and add them later.

**Step 2: Refactor `mt_render_header()` to use `mt_render_icons()`**

Replace the inline WiFi icon drawing in `mt_render_header()` with a call to `mt_render_icons()`. Current code (lines ~428-433):

```c
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
    {
        uint32_t wb = mt_wifi_bars();
        mt_wifi_icon(rx - 16, UI_HEAD_H - 6, wb, wb == 3u ? T_THEME : T_MID, T_LINE);
    }
#endif
```

Replace with:

```c
    rx = mt_render_icons(rx);
```

Remove the `#if defined(CONFIG_WIFI)` block entirely since `mt_render_icons()` handles it.

**Step 3: Build**

Run: `task build`

Expected: clean build. The header now shows all compiled-in icons.

**Step 4: Commit**

```bash
git add firmware/src/ui/themes/modd/theme.c
git commit -m "feat(ui): add header icon row for WiFi/ETH/BLE/USB status"
```

---

### Task 8: Settings page live network status

**Files:**
- Modify: `firmware/src/ui/themes/modd/page_settings.c`

**Step 1: Add a network status formatter**

Replace `mset_link_name()` (line ~144) and `mset_draw_network()` (line ~156) with live-status versions.

Replace `mset_link_name()` with `mset_wifi_status()` and `mset_eth_status()`:

```c
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
static void mset_wifi_status(char *buf, uint32_t bufsz, uint16_t *color)
{
    wifi_state_t st = wifi_get_state();
    *color = T_DIM;
    if (st == WIFI_STATE_OFF || st == WIFI_STATE_IDLE) {
        buf[0] = 'O'; buf[1] = 'f'; buf[2] = 'f'; buf[3] = 0;
        return;
    }
    if (st == WIFI_STATE_SCANNING) {
        const char *s = "Scanning...";
        uint32_t i = 0;
        while (s[i] && i < bufsz - 1) { buf[i] = s[i]; i++; }
        buf[i] = 0;
        return;
    }
    if (st == WIFI_STATE_AUTH || st == WIFI_STATE_ASSOC || st == WIFI_STATE_WPA) {
        const char *s = "Connecting...";
        uint32_t i = 0;
        while (s[i] && i < bufsz - 1) { buf[i] = s[i]; i++; }
        buf[i] = 0;
        return;
    }
    if (st == WIFI_STATE_CONNECTED) {
        uint32_t ip = wifi_get_ip();
        if (ip) {
            mset_fmt_ip(buf, ip);
            *color = T_TEXT;
        } else {
            const char *s = "DHCP...";
            uint32_t i = 0;
            while (s[i] && i < bufsz - 1) { buf[i] = s[i]; i++; }
            buf[i] = 0;
        }
        return;
    }
    buf[0] = '-'; buf[1] = 0;
}
#endif

#ifdef CONFIG_USB_NET
static void mset_eth_status(char *buf, uint32_t bufsz, uint16_t *color)
{
    *color = T_DIM;
    if (!ecm_is_up()) {
        buf[0] = '-'; buf[1] = 0;
        return;
    }
    uint32_t ip = net_get_ip();
    if (ip) {
        mset_fmt_ip(buf, ip);
        *color = T_TEXT;
    } else {
        const char *s = "DHCP...";
        uint32_t i = 0;
        while (s[i] && i < bufsz - 1) { buf[i] = s[i]; i++; }
        buf[i] = 0;
    }
}
#endif
```

**Step 2: Rewrite `mset_draw_network()`**

Replace the current static rows with live-status rows:

```c
static void mset_draw_network(int32_t y0)
{
    char buf[20];
    uint16_t color;
    int32_t y = y0;

#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
    mset_wifi_status(buf, sizeof buf, &color);
    ui_row(y, "WIFI", buf, color, 0);
    y += UI_ROW_H;
#endif

#ifdef CONFIG_USB_NET
    if (ecm_is_up()) {
        mset_eth_status(buf, sizeof buf, &color);
        ui_row(y, "ETH", buf, color, 0);
        y += UI_ROW_H;
    }
#endif

    /* gateway + DNS only when we have an IP */
    {
        uint32_t ip = net_get_ip();
        if (ip) {
            mset_fmt_ip(buf, net_get_gw());
            ui_row(y, "GATEWAY", buf, T_DIM, 0);
            y += UI_ROW_H;
            mset_fmt_ip(buf, net_get_dns());
            ui_row(y, "DNS", buf, T_DIM, 0);
        }
    }
}
```

**Step 3: Add network polling to `settings_status()`**

The existing `settings_status()` only polls dock list state. Add network state caching so the page redraws when network state changes.

Add a static cache and polling logic. Before the `settings_status()` function (line ~102), add:

```c
#ifdef CONFIG_SYS_MENU_NETWORK
static uint32_t mset_net_cache_ip;
static uint8_t  mset_net_cache_wifi_st;
static uint8_t  mset_net_cache_ecm_up;
#endif
```

Then modify `settings_status()`:

```c
static void settings_status(void)
{
#ifdef CONFIG_DOCK
    if (mset.tab == SMTAB_INSTR)
        ui_list_poll(&minstr_list);
#endif
#ifdef CONFIG_SYS_MENU_NETWORK
    if (mset.tab == SMTAB_NET) {
        uint32_t ip = net_get_ip();
        uint8_t wst = 0, eup = 0;
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
        wst = (uint8_t)wifi_get_state();
#endif
#ifdef CONFIG_USB_NET
        eup = (uint8_t)ecm_is_up();
#endif
        if (ip != mset_net_cache_ip || wst != mset_net_cache_wifi_st || eup != mset_net_cache_ecm_up) {
            mset_net_cache_ip = ip;
            mset_net_cache_wifi_st = wst;
            mset_net_cache_ecm_up = eup;
            nav_dirty = 1;
        }
    }
#endif
}
```

Note: You need to check what `SMTAB_NET` is called in the actual code. Look for the tab enum in page_settings.c and use the correct name for the network tab.

**Step 4: Remove `mset_link_name()`**

Delete the old `mset_link_name()` function (line ~144-154). If it's referenced elsewhere, grep for it first and remove those references. It was only used by the old `mset_draw_network()`.

**Step 5: Build**

Run: `task build`

Expected: clean build.

**Step 6: Commit**

```bash
git add firmware/src/ui/themes/modd/page_settings.c
git commit -m "feat(ui): live network status on settings page with state polling"
```

---

### Task 9: E2E test for toast visibility

**Files:**
- Create: `tests/boot/e2e_toast_test.py`

**Step 1: Write the test**

This test boots the firmware, waits for the WiFi scan to start (which triggers a toast), and screenshots the toast overlay. Since the emulator simulates WiFi scanning, a "Scanning WiFi" toast should appear briefly during boot.

```python
"""E2E: verify toast notification appears during WiFi init."""
import unittest
from tests.e2e_fixtures import E2ETestCase, screenshot


class ToastTest(E2ETestCase):
    defconfig = "modd"

    def test_toast_appears_on_boot(self):
        """After boot, the WiFi scan should trigger a toast overlay."""
        page = self.page
        # boot and wait for emulator ready
        self.boot()
        # take a screenshot shortly after boot when WiFi scanning starts
        screenshot(page, "toast_boot", "toast overlay during wifi scan")


if __name__ == "__main__":
    unittest.main()
```

Note: This is a visual regression test. The screenshot captures the toast state. Since WiFi scanning in the emulator may or may not show the toast (depends on timing), this test primarily ensures the toast module doesn't crash the firmware. A more deterministic test would require the emulator to expose WiFi state control.

**Step 2: Run the test**

Run: `task test:e2e`

Or for just this test (if the runner supports it):

Run: `cd /DIMM/Users/dmitry/dev/dimmkirr/modd && uv run pytest tests/boot/e2e_toast_test.py -v`

Expected: PASS (screenshot captured to `tests/results/`)

**Step 3: Commit**

```bash
git add tests/boot/e2e_toast_test.py
git commit -m "test(e2e): add toast notification visibility test"
```

---

### Task 10: Final integration build and test

**Step 1: Full build with default defconfig**

Run: `task build`

Expected: clean build, no warnings from toast/icon code.

**Step 2: Build with wifi_debug defconfig**

Run: `task build -- --defconfig modd_wifi_debug`

Expected: clean build (CONFIG_WIFI + CONFIG_UI_MODD both set).

**Step 3: Build with network defconfig**

Run: `task build -- --defconfig modd_dev_network`

Expected: clean build (CONFIG_USB_NET + CONFIG_WIFI + CONFIG_UI_MODD).

**Step 4: Run full e2e suite**

Run: `task test:e2e`

Expected: all tests pass.

**Step 5: Commit any fixups**

If any defconfig builds failed, fix and commit.

---

## Summary of files changed

| File | Action |
|------|--------|
| `firmware/adapters/modd_toast.h` | Create: toast API forward declarations |
| `firmware/src/ui/themes/modd/toast.c` | Create: toast module (tick, draw, status, error, clear) |
| `firmware/src/ui/themes/modd/ui.c` | Modify: add `#include "toast.c"` |
| `firmware/src/ui/themes/modd/shell.c` | Modify: wire `toast_tick()` + `toast_draw()` |
| `firmware/src/ui/themes/modd/theme.c` | Modify: add `mt_render_icons()`, refactor header |
| `firmware/src/ui/themes/modd/page_settings.c` | Modify: live network rows + status polling |
| `firmware/src/modd.c` | Modify: add `#include "modd_toast.h"` |
| `firmware/src/wifi/wifi_sta.c` | Modify: toast on scan/connect/timeout |
| `firmware/src/wifi/wifi_api.c` | Modify: toast on connected/disconnected |
| `firmware/src/wifi/wifi_lwip.c` | Modify: toast_clear on DHCP bound |
| `firmware/src/net/usbnet_ecm.c` | Modify: toast on attach/detach/IP |
| `firmware/src/wifi/wifi_weather.c` | Modify: toast on fetch/error |
| `tests/boot/e2e_toast_test.py` | Create: e2e toast visibility test |
