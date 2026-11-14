#!/usr/bin/env python3
#  SPDX-License-Identifier: GPL-3.0-only
#  Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Generate a Mermaid navigation graph for a UI theme.

Parses pages.c for screen registrations (grouped by view, with #ifdef guards),
modd_controls.h for button-to-view mappings, and ui_view.h for VIEW_* ids.
Emits a Mermaid flowchart showing boot -> splash -> views -> pages, with button
edges between views.

Usage:
  # Print Mermaid to stdout
  python3 tools/codegen/gen_ui_map.py felucca

  # Inject into a markdown file (replaces content between markers)
  python3 tools/codegen/gen_ui_map.py inject docs/ui/themes/felucca/UI.md felucca

  # Check mode (for CI: exit 1 if file is stale)
  python3 tools/codegen/gen_ui_map.py inject --check docs/ui/themes/felucca/UI.md felucca
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def load_defconfig(name: str, root: Path | None = None) -> set[str]:
    """Read a defconfig and return the set of enabled CONFIG_* symbols."""
    root = root or ROOT
    path = root / 'configs' / f'{name}_defconfig'
    enabled: set[str] = set()
    if not path.is_file():
        return enabled
    for line in path.read_text().splitlines():
        line = line.strip()
        if line.startswith('#') or '=' not in line:
            continue
        key, val = line.split('=', 1)
        if val == 'y':
            enabled.add(key)
    return enabled


def _guards_met(guards: list[str], config: set[str]) -> bool:
    """Check whether all Kconfig guards are satisfied by the active config."""
    for g in guards:
        g = g.strip()
        if g.startswith('defined(') and g.endswith(')'):
            g = g[len('defined('):-1]
        if g not in config:
            return False
    return True


MARKER_START = "<!-- ui-nav-diagram:start -->"
MARKER_END = "<!-- ui-nav-diagram:end -->"
CTRL_MARKER_START = "<!-- ui-controls-table:start -->"
CTRL_MARKER_END = "<!-- ui-controls-table:end -->"
SCREEN_MARKER_START = "<!-- ui-screen-schematic:start -->"
SCREEN_MARKER_END = "<!-- ui-screen-schematic:end -->"
HW_CTRL_MARKER_START = "<!-- ui-hw-controls:start -->"
HW_CTRL_MARKER_END = "<!-- ui-hw-controls:end -->"

# ── Parse ui_view.h ─────────────────────────────────────────────────────────

def parse_views(path: Path) -> dict[int, str]:
    """Return {id: name} for VIEW_* defines (excluding VIEW_COUNT, VIEW_NONE)."""
    views: dict[int, str] = {}
    pat = re.compile(r"#define\s+(VIEW_\w+)\s+(\d+)")
    for line in path.read_text().splitlines():
        m = pat.match(line.strip())
        if m and m.group(1) not in ("VIEW_COUNT", "VIEW_NONE"):
            views[int(m.group(2))] = m.group(1)
    return views


# ── Parse modd_controls.h ──────────────────────────────────────────────────

def parse_controls(path: Path, controls_name: str) -> tuple[dict[str, str], dict[str, str]]:
    """Return (tap_map, hold_map): {BTN_name: VIEW_name} for the given control set."""
    text = path.read_text()

    block_pat = {
        "felucca": r"Felucca control map",
        "melodee": r"Melodee control map",
        "sloop":   r"SLOOP control map",
    }
    header = block_pat.get(controls_name, block_pat["felucca"])
    start = text.find(header)
    if start < 0:
        return {}, {}

    end_markers = list(block_pat.values())
    end_markers.remove(header)
    end = len(text)
    for em in end_markers:
        idx = text.find(em, start + len(header))
        if idx >= 0:
            end = min(end, idx)
    block = text[start:end]

    def extract_map(label: str) -> dict[str, str]:
        result: dict[str, str] = {}
        in_block = False
        for line in block.splitlines():
            if label in line and "{" in line:
                in_block = True
                continue
            if in_block and "}" in line:
                break
            if in_block:
                m = re.search(r"\[(BTN_\w+)\]\s*=\s*(VIEW_\w+)", line)
                if m and m.group(2) != "VIEW_NONE":
                    result[m.group(1)] = m.group(2)
        return result

    return extract_map(".btn_tap"), extract_map(".btn_hold")


# ── Parse pages.c ──────────────────────────────────────────────────────────

def parse_pages(path: Path) -> list[dict]:
    """Return list of {name, view_comment, guards} in registration order."""
    pages: list[dict] = []
    guards: list[str] = []
    view_comment = ""
    for line in path.read_text().splitlines():
        stripped = line.strip()
        # Track #ifdef / #if / #endif
        if stripped.startswith("#ifdef ") or stripped.startswith("#if "):
            cond = stripped.split("//")[0].strip()
            cond = cond.removeprefix("#ifdef ").removeprefix("#if ")
            guards.append(cond)
        elif stripped.startswith("#endif"):
            if guards:
                guards.pop()
        # Track view comments
        m = re.match(r"/\*\s*(VIEW_\w+)", stripped)
        if m:
            view_comment = m.group(1)
        # Track registrations
        m = re.match(r"nav_register\(&(\w+)\);", stripped)
        if m:
            pages.append({
                "symbol": m.group(1),
                "view": view_comment,
                "guards": list(guards),
            })
    return pages


# ── Parse page_*.c for .name and .view fields ─────────────────────────────

def parse_page_details(theme_dir: Path) -> dict[str, dict]:
    """Return {SYMBOL: {name, title, view}} from modd_page_t structs in page_*.c."""
    details: dict[str, dict] = {}
    pat_ml = re.compile(r"static\s+const\s+modd_page_t\s+(\w+)\s*=\s*\{")
    name_pat = re.compile(r'\.name\s*=\s*"([^"]+)"')
    title_pat = re.compile(r'\.title\s*=\s*"([^"]+)"')
    view_pat = re.compile(r'\.view\s*=\s*(VIEW_\w+)')

    for f in sorted(theme_dir.glob("page_*.c")):
        text = f.read_text()
        for m in pat_ml.finditer(text):
            symbol = m.group(1)
            rest = text[m.end():]
            brace = 0
            for i, ch in enumerate(rest):
                if ch == "{":
                    brace += 1
                elif ch == "}":
                    if brace == 0:
                        body = rest[:i]
                        nm = name_pat.search(body)
                        tt = title_pat.search(body)
                        vw = view_pat.search(body)
                        details[symbol] = {
                            "name": nm.group(1) if nm else symbol,
                            "title": tt.group(1) if tt else "",
                            "view": vw.group(1) if vw else "",
                        }
                        break
                    brace -= 1
    return details


# ── Parse knob/button bindings per page ─────────────────────────────────────

# Encoder names by ID (from modd_controls.h)
ENC_NAMES = {0: "select", 1: "algorithm", 2: "knob1", 3: "knob2", 4: "knob3", 5: "knob4", 6: "presets"}

def parse_page_controls(theme_dir: Path) -> dict[str, dict]:
    """Return {SYMBOL: {knobs: {enc_id: label}, buttons: [BTN_*]}} per page.

    Parses fparams_def_t for knob1-4 labels, and knob handler `id == N` for
    select/algorithm bindings. Also collects BTN_* used in button handlers.
    """
    controls: dict[str, dict] = {}

    # fparams_def_t: {"short", "long", {P_...}, {"K1","K2","K3","K4"}, {"u1","u2","u3","u4"}, {icons}}
    # The label array is the second {...} containing strings (after the param-id array).
    fparam_pat = re.compile(
        r'static\s+const\s+fparams_def_t\s+(\w+)\s*=\s*\{.*?'
        r'\{P_\w+.*?\}.*?'     # skip the param-id array {P_ATK, ...}
        r'\{"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\}',
        re.DOTALL,
    )
    # Map fparams_def_t variable -> modd_page_t symbol via `.knob = <func>_knob`
    # and `<func>_knob(...) { return fparams_knob(&<def_var>, ...); }`
    knob_delegate_pat = re.compile(
        r'static\s+int\s+(\w+)\(.*?\)\s*\{\s*return\s+fparams_knob\(&(\w+)',
    )
    page_knob_assign = re.compile(r'\.knob\s*=\s*(\w+)')

    # param_slot_t arrays: {ENC_K1, P_ATK, 0, 127, 4, 0, "ATK"}
    slot_array_pat = re.compile(
        r'static\s+const\s+param_slot_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};',
        re.DOTALL,
    )
    slot_entry_pat = re.compile(
        r'\{ENC_(\w+),\s*P_(\w+),[^}]*"([^"]+)"\}',
    )
    page_knob_map_assign = re.compile(r'\.knob_map\s*=\s*(\w+)')

    for f in sorted(theme_dir.glob("page_*.c")):
        text = f.read_text()

        # Build def_name -> knob labels
        def_labels: dict[str, list[str]] = {}
        for m in fparam_pat.finditer(text):
            def_labels[m.group(1)] = [m.group(2), m.group(3), m.group(4), m.group(5)]

        # Build knob_func -> def_name
        func_to_def: dict[str, str] = {}
        for m in knob_delegate_pat.finditer(text):
            func_to_def[m.group(1)] = m.group(2)

        # Build slot_var -> [(enc_id, label)]
        slot_arrays: dict[str, list[tuple[int, str]]] = {}
        enc_name_to_id = {"SELECT": 0, "ALGO": 1, "K1": 2, "K2": 3, "K3": 4, "K4": 5, "PRESET": 6}
        for sm in slot_array_pat.finditer(text):
            var_name = sm.group(1)
            entries = []
            for em in slot_entry_pat.finditer(sm.group(2)):
                enc_id = enc_name_to_id.get(em.group(1), -1)
                entries.append((enc_id, em.group(3)))
            slot_arrays[var_name] = entries

        # Find modd_page_t structs and their .knob / .knob_map assignment
        page_pat = re.compile(r"static\s+const\s+modd_page_t\s+(\w+)\s*=\s*\{")
        for pm in page_pat.finditer(text):
            symbol = pm.group(1)
            rest = text[pm.end():]
            brace = 0
            body = ""
            for i, ch in enumerate(rest):
                if ch == "{":
                    brace += 1
                elif ch == "}":
                    if brace == 0:
                        body = rest[:i]
                        break
                    brace -= 1

            knobs: dict[int, str] = {}

            # Declarative knob_map (param_slot_t arrays)
            km_map = page_knob_map_assign.search(body)
            if km_map:
                slot_var = km_map.group(1)
                if slot_var in slot_arrays:
                    for enc_id, label in slot_arrays[slot_var]:
                        if enc_id >= 0:
                            knobs[enc_id] = label

            # Legacy .knob = <func> handler
            km = page_knob_assign.search(body)
            if km:
                knob_func = km.group(1)
                # Check if it delegates to fparams_knob
                if knob_func in func_to_def:
                    def_name = func_to_def[knob_func]
                    if def_name in def_labels:
                        labels = def_labels[def_name]
                        for j, lbl in enumerate(labels):
                            knobs.setdefault(j + 2, lbl)  # knob1=enc2, knob2=enc3, etc.

                # Parse direct `if (id == N)` in the knob handler body
                func_pat = re.compile(
                    rf'static\s+int\s+{re.escape(knob_func)}\s*\([^)]*\)\s*\{{',
                )
                fm = func_pat.search(text)
                if fm:
                    fbody = text[fm.end():]
                    depth = 1
                    for i, ch in enumerate(fbody):
                        if ch == "{": depth += 1
                        elif ch == "}":
                            depth -= 1
                            if depth == 0:
                                fbody = fbody[:i]
                                break
                    for im in re.finditer(r'if\s*\(id\s*==\s*(\d+)', fbody):
                        enc_id = int(im.group(1))
                        if enc_id not in knobs:
                            knobs[enc_id] = ENC_NAMES.get(enc_id, f"enc{enc_id}")
                    # Range: `id >= N && id <= M` (e.g. edit)
                    for rm in re.finditer(r'id\s*>=\s*(\d+)\s*&&\s*id\s*<=\s*(\d+)', fbody):
                        lo, hi = int(rm.group(1)), int(rm.group(2))
                        for enc_id in range(lo, hi + 1):
                            if enc_id not in knobs:
                                knobs[enc_id] = ENC_NAMES.get(enc_id, f"enc{enc_id}")

            # Parse button handler for BTN_* references
            buttons: list[str] = []
            btn_assign = re.search(r'\.button\s*=\s*(\w+)', body)
            if btn_assign:
                btn_func = btn_assign.group(1)
                bfm = re.search(
                    rf'static\s+(?:int|uint32_t)\s+{re.escape(btn_func)}\s*\([^)]*\)\s*\{{',
                    text,
                )
                if bfm:
                    bbody = text[bfm.end():]
                    depth = 1
                    for i, ch in enumerate(bbody):
                        if ch == "{": depth += 1
                        elif ch == "}":
                            depth -= 1
                            if depth == 0:
                                bbody = bbody[:i]
                                break
                    for bm in re.finditer(r'BTN_(\w+)', bbody):
                        btn_name = bm.group(1)
                        if btn_name not in ("COUNT",) and btn_name not in buttons:
                            buttons.append(btn_name)

            if knobs or buttons:
                controls[symbol] = {"knobs": knobs, "buttons": buttons}

    return controls


# ── Parse screen layout from theme.c / ui.c ──────────────────────────────

def parse_screen_layout(theme_dir: Path) -> dict[str, int]:
    """Return {zone: pixels} from #define lines in theme source files."""
    layout: dict[str, int] = {}
    define_pat = re.compile(r"#define\s+\w+\s+(\d+)")

    name_map = {
        "screen_w": re.compile(r"#define\s+(?:MT_W|SCREEN_W)\s+(\d+)"),
        "screen_h": re.compile(r"#define\s+(?:MT_H|SCREEN_H)\s+(\d+)"),
        "header_h": re.compile(r"#define\s+(?:MT_H_HEAD|H_HEAD)\s+(\d+)"),
        "footer_h": re.compile(r"#define\s+(?:MT_H_FOOT|H_FOOT)\s+(\d+)"),
        "panel_h": re.compile(r"#define\s+(?:MT_H_PANEL|H_PANEL)\s+(\d+)"),
        "card_w": re.compile(r"#define\s+(?:MT_CARD_W|CARD_W)\s+(\d+)"),
        "card_h": re.compile(r"#define\s+(?:MT_CARD_H|CARD_H)\s+(\d+)"),
    }

    # Also try the ui_theme_t struct for .screen_w, .header_h etc.
    struct_pat = {
        "screen_w": re.compile(r"\.screen_w\s*=\s*(\d+)"),
        "screen_h": re.compile(r"\.screen_h\s*=\s*(\d+)"),
    }

    for f in sorted(theme_dir.glob("*.c")):
        text = f.read_text()
        for key, pat in name_map.items():
            for m in pat.finditer(text):
                layout[key] = int(m.group(1))
        for key, pat in struct_pat.items():
            if key not in layout:
                for m in pat.finditer(text):
                    layout[key] = int(m.group(1))

    return layout


def generate_screen_schematic(theme: str, layout: dict[str, int]) -> str:
    """Generate an ASCII screen schematic from parsed layout dimensions."""
    w = layout.get("screen_w", 240)
    h = layout.get("screen_h", 240)
    hdr = layout.get("header_h", 0)
    ftr = layout.get("footer_h", 0)
    panel = layout.get("panel_h", 0)
    card_w = layout.get("card_w", 0)
    card_h = layout.get("card_h", 0)

    mid = h - hdr - ftr
    if panel and panel < mid:
        cards_h = mid - panel
    else:
        cards_h = 0
        panel = mid

    box_w = 34
    lines = [
        f"## Screen layout",
        "",
        f"{w} x {h} LCD, zones top to bottom:",
        "",
        "```",
        "+" + "-" * box_w + "+",
    ]

    def zone(name: str, px: int) -> None:
        label = f" {name} ({px}px)"
        lines.append("|" + label.ljust(box_w) + "|")

    if hdr:
        zone("Header", hdr)
        lines.append("|" + "-" * box_w + "|")
    if cards_h and card_h:
        zone(f"Cards ({card_w}x{card_h} x4)", cards_h)
        lines.append("|" + "-" * box_w + "|")
    if panel:
        zone("Panel (page content)", panel)
        lines.append("|" + "-" * box_w + "|")
    if ftr:
        zone("Footer", ftr)

    lines.append("+" + "-" * box_w + "+")
    lines.append("```")
    return "\n".join(lines)


# ── Parse physical controls from modd_controls.h ────────────────────────

def parse_hw_controls(path: Path) -> tuple[list[tuple[str, int]], list[tuple[str, int]]]:
    """Return (buttons, encoders): [(name, id)] from BTN_* and ENC_* defines."""
    buttons: list[tuple[str, int]] = []
    encoders: list[tuple[str, int]] = []
    btn_pat = re.compile(r"#define\s+(BTN_(\w+))\s+(\d+)")
    enc_pat = re.compile(r"#define\s+(ENC_(\w+))\s+(\d+)")

    for line in path.read_text().splitlines():
        m = btn_pat.match(line.strip())
        if m and m.group(2) != "COUNT":
            buttons.append((m.group(2), int(m.group(3))))
            continue
        m = enc_pat.match(line.strip())
        if m:
            encoders.append((m.group(2), int(m.group(3))))

    return buttons, encoders


# Friendly descriptions for hardware controls (not derivable from source)
BTN_DESC = {
    "OCTDN": "Octave down",
    "OCTUP": "Octave up",
    "FX": "Effects view (hold: Demo)",
    "SCL": "Scale view",
    "ENV": "Envelope view",
    "LFO": "LFO view",
    "EDIT": "Engine edit view",
    "GLO": "Global view (hold: Connectivity)",
    "HOME": "Home view (hold: Settings)",
    "SAVE": "Save/Files view",
    "ARP": "Arpeggiator view",
    "SEQ": "Sequencer view",
    "PLAY": "Play/stop",
    "REC": "Record toggle",
}

ENC_DESC = {
    "SELECT": "Cycle pages within current view",
    "ALGO": "Context-dependent (engine, track, etc.)",
    "K1": "Knob 1: card slot 0 parameter",
    "K2": "Knob 2: card slot 1 parameter",
    "K3": "Knob 3: card slot 2 parameter",
    "K4": "Knob 4: card slot 3 parameter",
    "PRESET": "Cycle presets",
}


def generate_hw_controls(path: Path) -> str:
    """Generate markdown tables for physical buttons and encoders."""
    buttons, encoders = parse_hw_controls(path)
    lines = [
        "## Physical controls",
        "",
        "The FM-1 has 14 buttons, 7 rotary encoders, and 1 potentiometer (master volume).",
        "",
        "### Buttons",
        "",
        "| ID | Name | Default action |",
        "|---:|------|----------------|",
    ]
    for name, bid in sorted(buttons, key=lambda x: x[1]):
        desc = BTN_DESC.get(name, "")
        lines.append(f"| {bid} | {name} | {desc} |")

    lines += [
        "",
        "### Encoders",
        "",
        "| ID | Name | Default action |",
        "|---:|------|----------------|",
    ]
    for name, eid in sorted(encoders, key=lambda x: x[1]):
        desc = ENC_DESC.get(name, "")
        lines.append(f"| {eid} | {name} | {desc} |")

    lines += [
        "",
        "### Potentiometer",
        "",
        "| Name | Action |",
        "|------|--------|",
        "| Master | Master volume (hardware, always active) |",
    ]

    return "\n".join(lines)


def generate_controls_table(page_controls: dict, page_details: dict, pages: list[dict]) -> str:
    """Generate a markdown table of per-page encoder and button bindings."""
    lines = [
        "## Global controls",
        "",
        "Always active, handled by the shell or keyboard overlay:",
        "",
        "| Control | Action |",
        "|---------|--------|",
        "| Presets (enc) | Cycle presets |",
        "| Master (pot) | Master volume |",
        "| OCT+ / OCT- | Octave shift |",
        "| PLAY | Play/stop (page-specific on WIFI, WEATHER, BLE) |",
        "| REC | Record toggle |",
        "| Select (enc) | Cycle pages within current view (when no page claims it) |",
        "",
        "## Controls per page",
        "",
        "| Page | Select | Algo | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Buttons |",
        "|------|--------|------|--------|--------|--------|--------|---------|",
    ]
    for p in pages:
        sym = p["symbol"]
        d = page_details.get(sym, {})
        title = d.get("title") or d.get("name") or sym
        c = page_controls.get(sym, {"knobs": {}, "buttons": []})
        knobs = c["knobs"]
        cols = []
        for enc_id in range(6):  # select(0), algo(1), knob1-4(2-5)
            cols.append(knobs.get(enc_id, ""))
        btns = ", ".join(c["buttons"]) if c["buttons"] else ""
        line = f"| {title} | {cols[0]} | {cols[1]} | {cols[2]} | {cols[3]} | {cols[4]} | {cols[5]} | {btns} |"
        lines.append(line)

    return "\n".join(lines)


# ── Structured nav map (for tests) ──────────────────────────────────────────

def load_nav_map(theme: str, root: Path | None = None) -> dict:
    """Return the full navigation map as structured data.

    Returns:
        {
            "theme": str,
            "pages": [{symbol, name, view, guards}],
            "view_groups": {VIEW_*: [{symbol, name, view, guards}]},
            "tap_map": {BTN_*: VIEW_*},
            "hold_map": {BTN_*: VIEW_*},
            "btn_for_view": {VIEW_*: btn_name},  # lowercase button for press_button()
        }
    """
    root = root or ROOT
    theme_dir = root / "firmware" / "src" / "ui" / "themes" / theme
    if not theme_dir.is_dir():
        raise SystemExit(f"Theme directory not found: {theme_dir}")

    controls_name = theme if theme in ("felucca", "melodee", "sloop") else "felucca"
    tap_map, hold_map = parse_controls(
        root / "firmware" / "adapters" / "modd_controls.h", controls_name
    )
    pages = parse_pages(theme_dir / "pages.c")
    page_details = parse_page_details(theme_dir)

    for p in pages:
        if not p["view"] and p["symbol"] in page_details:
            p["view"] = page_details[p["symbol"]].get("view", "")
        p["name"] = page_details.get(p["symbol"], {}).get("name", p["symbol"])

    view_groups: dict[str, list[dict]] = {}
    for p in pages:
        vw = p["view"] or "VIEW_HOME"
        view_groups.setdefault(vw, []).append(p)

    # Reverse map: VIEW_* -> button name (lowercase, for FM1Controls.press_button)
    btn_for_view: dict[str, str] = {}
    for btn, view in tap_map.items():
        btn_for_view.setdefault(view, btn.replace("BTN_", "").lower())
    for btn, view in hold_map.items():
        key = btn.replace("BTN_", "").lower() + "_hold"
        btn_for_view.setdefault(view, key)

    return {
        "theme": theme,
        "pages": pages,
        "view_groups": view_groups,
        "tap_map": tap_map,
        "hold_map": hold_map,
        "btn_for_view": btn_for_view,
    }


# ── Control path: nav map -> ordered action sequence ──────────────────────

def control_path(nav_map: dict, config: set[str] | None = None) -> list[dict]:
    """Convert a nav map into an ordered list of actions to visit every page.

    Each action is a dict:
      {"action": "press", "button": "home", "page": "HOME", "view": "VIEW_HOME"}
      {"action": "hold",  "button": "home", "page": "WiFi", "view": "VIEW_SETTINGS"}
      {"action": "cycle", "encoder": "select", "direction": 1,
       "page": "Colors", "view": "VIEW_SETTINGS"}

    The sequence visits every view in registration order, cycling through
    multi-page views with the select encoder.  Each action includes the
    page name expected after the action completes, enabling callers to
    screenshot, assert, or log at each step.

    When *config* is provided (a set of enabled CONFIG_* symbols), pages
    whose Kconfig guards are not satisfied are skipped.
    """
    steps: list[dict] = []
    btn_for_view = nav_map["btn_for_view"]

    for view_name, pages_in_view in nav_map["view_groups"].items():
        btn = btn_for_view.get(view_name)
        if not btn:
            continue

        if config is not None:
            pages_in_view = [p for p in pages_in_view
                             if _guards_met(p.get("guards", []), config)]
        if not pages_in_view:
            continue

        is_hold = btn.endswith("_hold")
        btn_name = btn.removesuffix("_hold") if is_hold else btn

        first_page = pages_in_view[0]
        steps.append({
            "action": "hold" if is_hold else "press",
            "button": btn_name,
            "page": first_page["name"],
            "view": view_name,
        })

        for pg in pages_in_view[1:]:
            steps.append({
                "action": "cycle",
                "encoder": "select",
                "direction": 1,
                "page": pg["name"],
                "view": view_name,
            })

    return steps


# ── Generate Mermaid ────────────────────────────────────────────────────────

def generate_mermaid(theme: str) -> str:
    theme_dir = ROOT / "firmware" / "src" / "ui" / "themes" / theme
    if not theme_dir.is_dir():
        # Try the renamed variant (felucca -> reluca in git status, but source is felucca)
        raise SystemExit(f"Theme directory not found: {theme_dir}")

    views = parse_views(ROOT / "firmware" / "src" / "ui" / "ui_view.h")
    controls_name = theme if theme in ("felucca", "melodee", "sloop") else "felucca"
    tap_map, hold_map = parse_controls(
        ROOT / "firmware" / "adapters" / "modd_controls.h", controls_name
    )
    pages = parse_pages(theme_dir / "pages.c")
    page_details = parse_page_details(theme_dir)

    # Resolve view: prefer comment from pages.c, fall back to .view in source
    for p in pages:
        if not p["view"] and p["symbol"] in page_details:
            p["view"] = page_details[p["symbol"]].get("view", "")

    # Group pages by view
    view_groups: dict[str, list[dict]] = {}
    for p in pages:
        vw = p["view"] or "VIEW_HOME"
        view_groups.setdefault(vw, []).append(p)

    # Friendly view labels
    view_label = {
        "VIEW_HOME": "Home",
        "VIEW_ENV": "Envelope",
        "VIEW_LFO": "LFO",
        "VIEW_FX": "Effects",
        "VIEW_SCL": "Scale",
        "VIEW_EDIT": "Edit",
        "VIEW_GLO": "Global",
        "VIEW_SAVE": "Save",
        "VIEW_ARP": "Arpeggio",
        "VIEW_SEQ": "Sequencer",
        "VIEW_CONN": "Connectivity",
        "VIEW_SETTINGS": "Settings",
        "VIEW_DEMO": "Demo",
    }

    def _node_id(sym: str) -> str:
        return sym.replace("FPAGE_", "F_").replace("PAGE_", "P_")

    def _display(sym: str) -> str:
        d = page_details.get(sym, {})
        return d.get("title") or d.get("name") or sym

    def _guard_note(guards: list[str]) -> str:
        if not guards:
            return ""
        short = []
        for g in guards:
            g = re.sub(r"defined\(CONFIG_(\w+)\)", r"\1", g)
            g = re.sub(r"CONFIG_(\w+)", r"\1", g)
            g = g.replace(" || ", " | ").replace(" && ", " & ")
            short.append(g.strip())
        return "<br/><small>⚙ " + " · ".join(short) + "</small>"

    # Target id for edges: subgraph id for multi-page views, node id for single
    def _view_target(vw: str) -> str:
        group = view_groups.get(vw, [])
        if len(group) == 1:
            return _node_id(group[0]["symbol"])
        return vw.replace("VIEW_", "sg_")

    # Boot -> HOME target
    home_target = _view_target("VIEW_HOME")

    lines_final = [
        '%%{init: {"flowchart": {"curve": "linear"}} }%%',
        "flowchart TD",
        "",
        '    BOOT(["Boot Splash"]):::boot',
        f'    BOOT --> {home_target}',
    ]

    # Views: subgraph for multi-page, flat node for single-page
    for vw in sorted(view_groups.keys(), key=lambda v: list(view_label.keys()).index(v) if v in view_label else 99):
        group = view_groups[vw]
        lines_final.append("")

        if len(group) == 1:
            p = group[0]
            nid = _node_id(p["symbol"])
            label = _display(p["symbol"])
            lines_final.append(f'    {nid}["{label}{_guard_note(p["guards"])}"]')
        else:
            sg_label = view_label.get(vw, vw.replace("VIEW_", "").title())
            sg_id = vw.replace("VIEW_", "sg_")
            lines_final.append(f'    subgraph {sg_id}["{sg_label}"]')
            for p in group:
                nid = _node_id(p["symbol"])
                label = _display(p["symbol"])
                lines_final.append(f'        {nid}["{label}{_guard_note(p["guards"])}"]')
            lines_final.append("    end")

    # Encoder cycling: bidirectional within multi-page views
    lines_final.append("")
    lines_final.append("    %% Encoder: cycle pages within a view")
    for vw, group in view_groups.items():
        if len(group) < 2:
            continue
        ids = [_node_id(p["symbol"]) for p in group]
        for i in range(len(ids) - 1):
            lines_final.append(f"    {ids[i]} <-..-> {ids[i+1]}")

    # Button tap navigation
    lines_final.append("")
    lines_final.append("    %% Buttons: tap navigates between views")
    for btn, target_view in sorted(tap_map.items()):
        if target_view not in view_groups:
            continue
        btn_label = btn.replace("BTN_", "")
        target = _view_target(target_view)
        lines_final.append(f'    BTN_{btn_label}[/"{btn_label}"/] --> {target}')

    # Button hold navigation
    lines_final.append("")
    lines_final.append("    %% Buttons: hold navigates to secondary views")
    for btn, target_view in sorted(hold_map.items()):
        if target_view not in view_groups:
            continue
        btn_label = btn.replace("BTN_", "")
        target = _view_target(target_view)
        lines_final.append(f'    BTN_{btn_label}_H[/"{btn_label} hold"/] -.-> {target}')

    # Styles
    lines_final.append("")
    lines_final.append("    classDef boot fill:#E8E4DF,stroke:#6E675E,color:#333")

    return "\n".join(lines_final)


def generate_full(theme: str) -> tuple[str, str]:
    """Return (mermaid_diagram, controls_table) for a theme."""
    theme_dir = ROOT / "firmware" / "src" / "ui" / "themes" / theme
    if not theme_dir.is_dir():
        raise SystemExit(f"Theme directory not found: {theme_dir}")

    pages = parse_pages(theme_dir / "pages.c")
    page_details = parse_page_details(theme_dir)
    page_controls = parse_page_controls(theme_dir)

    for p in pages:
        if not p["view"] and p["symbol"] in page_details:
            p["view"] = page_details[p["symbol"]].get("view", "")

    mermaid = generate_mermaid(theme)
    table = generate_controls_table(page_controls, page_details, pages)
    return mermaid, table


# ── Inject into markdown ────────────────────────────────────────────────────

def _inject_section(text: str, content: str, start_marker: str, end_marker: str) -> str:
    """Replace content between markers in text."""
    if start_marker in text and end_marker in text:
        before = text[: text.index(start_marker)]
        after = text[text.index(end_marker) + len(end_marker) :]
        return before + content + after
    return text.rstrip() + "\n\n" + content + "\n"


def inject(md_path: Path, mermaid: str, check: bool = False,
           controls_table: str | None = None) -> bool:
    """Replace content between markers. Returns True if content changed."""
    diagram_block = f"{MARKER_START}\n```mermaid\n{mermaid}\n```\n{MARKER_END}"
    ctrl_block = (f"{CTRL_MARKER_START}\n{controls_table}\n{CTRL_MARKER_END}"
                  if controls_table else None)

    if not md_path.exists():
        md_path.parent.mkdir(parents=True, exist_ok=True)
        parts = [diagram_block]
        if ctrl_block:
            parts.append(ctrl_block)
        md_path.write_text("\n\n".join(parts) + "\n")
        return True

    text = md_path.read_text()
    new_text = _inject_section(text, diagram_block, MARKER_START, MARKER_END)
    if ctrl_block:
        new_text = _inject_section(new_text, ctrl_block, CTRL_MARKER_START, CTRL_MARKER_END)

    if new_text == text:
        return False

    if check:
        return True

    md_path.write_text(new_text)
    return True


def inject_hw_controls(md_path: Path, check: bool = False) -> bool:
    """Inject hardware controls table into a markdown file."""
    controls_h = ROOT / "firmware" / "adapters" / "modd_controls.h"
    content = generate_hw_controls(controls_h)
    block = f"{HW_CTRL_MARKER_START}\n{content}\n{HW_CTRL_MARKER_END}"

    if not md_path.exists():
        md_path.parent.mkdir(parents=True, exist_ok=True)
        md_path.write_text(block + "\n")
        return True

    text = md_path.read_text()
    new_text = _inject_section(text, block, HW_CTRL_MARKER_START, HW_CTRL_MARKER_END)

    if new_text == text:
        return False
    if check:
        return True

    md_path.write_text(new_text)
    return True


# ── CLI ─────────────────────────────────────────────────────────────────────

def discover_themes(root: Path | None = None) -> list[str]:
    """Return sorted list of theme names that have a pages.c."""
    root = root or ROOT
    themes_dir = root / "firmware" / "src" / "ui" / "themes"
    return sorted(
        d.name for d in themes_dir.iterdir()
        if d.is_dir() and (d / "pages.c").exists()
    )


def main() -> None:
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)

    if args[0] == "list-themes":
        for t in discover_themes():
            print(t)
        return

    if args[0] == "hw-controls":
        check = "--check" in args
        rest = [a for a in args[1:] if a != "--check"]
        if len(rest) != 1:
            print("Usage: gen_ui_map.py hw-controls [--check] <file.md>", file=sys.stderr)
            sys.exit(1)
        md_path = Path(rest[0])
        changed = inject_hw_controls(md_path, check=check)
        if check and changed:
            print(f"STALE: {md_path} needs regeneration", file=sys.stderr)
            sys.exit(1)
        elif not check:
            status = "updated" if changed else "up to date"
            print(f"{md_path}: {status}")
        return

    if args[0] == "inject":
        check = "--check" in args
        rest = [a for a in args[1:] if a != "--check"]
        if len(rest) != 2:
            print("Usage: gen_ui_map.py inject [--check] <file.md> <theme>", file=sys.stderr)
            sys.exit(1)
        md_path, theme = Path(rest[0]), rest[1]
        mermaid, table = generate_full(theme)
        changed = inject(md_path, mermaid, check=check, controls_table=table)
        if check and changed:
            print(f"STALE: {md_path} needs regeneration", file=sys.stderr)
            sys.exit(1)
        elif not check:
            status = "updated" if changed else "up to date"
            print(f"{md_path}: {status}")
    else:
        theme = args[0]
        mermaid, table = generate_full(theme)
        print(mermaid)
        print()
        print(table)


if __name__ == "__main__":
    main()
