#!/usr/bin/env python3
#  SPDX-License-Identifier: GPL-3.0-only
#  Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Capture per-page screenshots for a UI theme.

The modd theme uses fm1-live (native CLI emulator) with USB ethernet and
real network access for weather data.  Other themes fall back to the WASM
emulator via Playwright.

Extended captures for the modd theme:
  - All settings tabs (DISPLAY, MIDI, HW, NET)
  - All color palettes (10 palettes, each shown on the HOME page)
  - Weather, Demo, Files, Disk, Log pages

Usage:
  python3 tools/codegen/gen_ui_screenshots.py modd
  python3 tools/codegen/gen_ui_screenshots.py felucca
  python3 tools/codegen/gen_ui_screenshots.py --all
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'tools' / 'codegen'))

from gen_ui_map import discover_themes

THEME_DEFCONFIG = {
    'felucca': 'felucca',
    'modd': 'modd_dev_network',
}

EMU_WIFI_SSID = 'Home'
EMU_WIFI_PASSWORD = 'fm1pass'

SETTINGS_TABS = ['DISPLAY', 'MIDI', 'HW', 'NET']
PALETTE_COUNT = 10

README_MARKER_START = '<!-- theme-screenshots:start -->'
README_MARKER_END = '<!-- theme-screenshots:end -->'

GALLERY_MARKER_START = '<!-- theme-gallery:start -->'
GALLERY_MARKER_END = '<!-- theme-gallery:end -->'

HERO_SHOTS = ['home', 'files', 'disk', 'settings_net_wifi', 'wx', 'demo']
HERO_ALT = {
    'settings_net_wifi': 'wifi config',
    'wx': 'weather',
}
PAGE_SHOTS = [
    ('home', 'Home'), ('wx', 'Weather'), ('demo', 'Demo'),
    ('files', 'Files'), ('disk', 'Disk'), ('log', 'Log'),
    ('settings_net_wifi', 'WiFi'),
]
SETTINGS_SHOT_TABS = ['display', 'midi', 'hw', 'net']


def _fwsc_path(defconfig: str) -> Path:
    return ROOT / 'build' / defconfig / 'modd.fwsc'


def _find_fm1_live() -> Path | None:
    for env_key in ('FM1_EMU_ROOT', 'FM1_EMU_DIR'):
        root = os.environ.get(env_key, '')
        if root:
            p = Path(root) / 'target' / 'release' / 'fm1-live'
            if p.is_file():
                return p
    for candidate in [
        Path('/Users/dmitry/dev/modd-fm/fm1-emu/.worktrees/master/target/release/fm1-live'),
        Path('/Users/dmitry/dev/modd-fm/fm1-emu/target/release/fm1-live'),
    ]:
        if candidate.is_file():
            return candidate
    return None


def _build_firmware_defconfig(defconfig: str) -> Path:
    """Build a specific defconfig and return the .fwsc path."""
    fwsc = _fwsc_path(defconfig)
    if fwsc.is_file():
        return fwsc
    env = {**os.environ}
    tc = Path.home() / '.fm1-toolchain' / 'toolchain'
    if 'JIELI_TOOLCHAIN' not in env and tc.is_dir():
        env['JIELI_TOOLCHAIN'] = str(tc)
    sdk = Path.home() / '.fm1-toolchain' / 'AC79_SDK'
    if 'AC79_SDK' not in env and sdk.is_dir():
        env['AC79_SDK'] = str(sdk)
    print(f'  Building firmware (defconfig={defconfig})...')
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'build' / 'build.py'),
         '--defconfig', defconfig],
        capture_output=True, text=True, cwd=str(ROOT), timeout=300, env=env)
    if r.returncode != 0:
        print(f'  Build failed:\n{r.stderr}', file=sys.stderr)
        raise SystemExit(1)
    if not fwsc.is_file():
        print(f'  Build succeeded but {fwsc} not found', file=sys.stderr)
        raise SystemExit(1)
    print(f'  Built {fwsc}')
    return fwsc


def _build_firmware(theme: str) -> Path:
    defconfig = THEME_DEFCONFIG.get(theme, theme)
    env = {**os.environ}
    tc = Path.home() / '.fm1-toolchain' / 'toolchain'
    if 'JIELI_TOOLCHAIN' not in env and tc.is_dir():
        env['JIELI_TOOLCHAIN'] = str(tc)
    sdk = Path.home() / '.fm1-toolchain' / 'AC79_SDK'
    if 'AC79_SDK' not in env and sdk.is_dir():
        env['AC79_SDK'] = str(sdk)
    env['WIFI_SSID'] = EMU_WIFI_SSID
    env['WIFI_PASSWORD'] = EMU_WIFI_PASSWORD
    print(f'  Building firmware (defconfig={defconfig})...')
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'build' / 'build.py'),
         '--defconfig', defconfig],
        capture_output=True, text=True, cwd=str(ROOT), timeout=300, env=env)
    if r.returncode != 0:
        print(f'  Build failed:\n{r.stderr}', file=sys.stderr)
        raise SystemExit(1)
    fwsc = _fwsc_path(defconfig)
    if not fwsc.is_file():
        print(f'  Build succeeded but {fwsc} not found', file=sys.stderr)
        raise SystemExit(1)
    print(f'  Built {fwsc}')
    return fwsc


# ---------------------------------------------------------------------------
# fm1-live backend (modd theme): headless --shot with USB ethernet
# ---------------------------------------------------------------------------

SCREENSHOT_DISK_IMG = Path(__file__).resolve().parent / 'screenshot_disk.img'


def _shot(fm1_live: Path, fwsc: Path, keys: str, ms: int,
          work_dir: Path, net: bool = True, wifi: bool = False,
          disk_image: Path | None = None) -> Path:
    """Run fm1-live --shot in a fresh subdirectory and return the PPM path."""
    env = {**os.environ, 'FM1_NO_PLAY': '1'}
    if keys:
        env['FM1_BENCH_KEYS'] = keys
    if net or wifi:
        env['FM1_NO_JIT'] = '1'
        env['FM1_NO_LOOPSKIP'] = '1'
    run_dir = Path(tempfile.mkdtemp(dir=work_dir))
    (run_dir / 'work').mkdir()
    cmd = [str(fm1_live), '--fwsc', str(fwsc.resolve()), '--lcd-only', '--shot', str(ms)]
    usb_parts = []
    if net and not wifi:
        usb_parts.append('ethernet')
    if disk_image:
        usb_parts.append('disk')
    if usb_parts:
        cmd += ['--usb', ','.join(usb_parts)]
    if wifi:
        cmd += ['--wifi', 'oss']
    if net or wifi:
        cmd += ['--net', 'internet']
    if disk_image:
        cmd += ['--disk-image', str(disk_image.resolve())]
    r = subprocess.run(cmd, capture_output=True, text=True,
                       cwd=str(run_dir), timeout=600, env=env)
    ppm = run_dir / 'work' / 'lcd.ppm'
    if not ppm.is_file():
        print(f'  fm1-live failed (exit {r.returncode}):\n{r.stdout}\n{r.stderr}',
              file=sys.stderr)
        raise SystemExit(1)
    if r.returncode != 0:
        print(f'  fm1-live warning (exit {r.returncode}): {r.stderr[:200]}',
              file=sys.stderr)
    return ppm


def _ppm_to_png(ppm: Path, png: Path) -> None:
    from PIL import Image
    Image.open(str(ppm)).save(str(png))


def _hold_keys(button: str, start_ms: int, hold_ms: int = 700) -> str:
    return f'{start_ms}:{button},{start_ms + hold_ms}:{button}^'


def _capture_modd_live(fm1_live: Path, fwsc: Path, out_dir: Path) -> int:
    """Capture all modd theme screenshots using fm1-live."""
    idx = 0

    with tempfile.TemporaryDirectory() as td:
        work = Path(td)

        # --- HOME (boot, no network needed for fast capture) ---
        print('    HOME...')
        ppm = _shot(fm1_live, fwsc, '', 2000, work, net=False)
        _ppm_to_png(ppm, out_dir / f'{idx:02d}_home.png')
        idx += 1

        # --- SETTINGS: HOME hold -> settings, then cycle tabs ---
        # HOME hold at 500ms, release at 1200ms
        # Then e0 clicks for each tab
        # NET tab uses network so it shows LINK/IP/GATEWAY/DNS values
        for tab_i, tab_name in enumerate(SETTINGS_TABS):
            use_net = tab_name == 'NET'
            boot_wait = 15000 if use_net else 0
            print(f'    settings {tab_name}{"  (with network)" if use_net else ""}...')
            keys = _hold_keys('HOME', 500 + boot_wait)
            enc_clicks = ','.join(
                f'{1500 + boot_wait + i * 300}:e0:+1' for i in range(tab_i))
            if enc_clicks:
                keys += ',' + enc_clicks
            shot_ms = 1500 + boot_wait + tab_i * 300 + 500
            ppm = _shot(fm1_live, fwsc, keys, shot_ms, work, net=use_net)
            _ppm_to_png(ppm, out_dir / f'{idx:02d}_settings_{tab_name.lower()}.png')
            idx += 1

        # --- SETTINGS NET over WiFi (oss driver, no USB ethernet) ---
        print('    settings NET (WiFi)...')
        net_tab_i = SETTINGS_TABS.index('NET')
        keys = _hold_keys('HOME', 25000)
        enc_clicks = ','.join(
            f'{26500 + i * 300}:e0:+1' for i in range(net_tab_i))
        if enc_clicks:
            keys += ',' + enc_clicks
        shot_ms = 26500 + net_tab_i * 300 + 500
        ppm = _shot(fm1_live, fwsc, keys, shot_ms, work, wifi=True)
        _ppm_to_png(ppm, out_dir / f'{idx:02d}_settings_net_wifi.png')
        idx += 1

        # --- PALETTES: settings DISPLAY tab with each palette selected ---
        # OCT+ cycles the THEME value; screenshot the settings page to show
        # the palette name and colour swatches (HOME page barely changes).
        for pal_i in range(PALETTE_COUNT):
            print(f'    palette {pal_i}/{PALETTE_COUNT}...')
            keys = _hold_keys('HOME', 500)
            t = 1500
            for _ in range(pal_i):
                keys += f',{t}:OCTUP,{t + 100}:OCTUP^'
                t += 300
            shot_ms = t + 500
            ppm = _shot(fm1_live, fwsc, keys, shot_ms, work, net=False)
            _ppm_to_png(ppm, out_dir / f'{idx:02d}_palette_{pal_i:02d}.png')
            idx += 1

        # --- WEATHER (needs network): wait for USB ethernet + weather fetch ---
        # FX hold -> VIEW_CONN page 1 (weather page)
        # 20s boot with network, then 10s for weather fetch to complete
        print('    weather (waiting for network + fetch)...')
        keys = _hold_keys('FX', 20000)
        ppm = _shot(fm1_live, fwsc, keys, 30000, work, net=True)
        _ppm_to_png(ppm, out_dir / f'{idx:02d}_wx.png')
        idx += 1

        # --- DEMO (FX hold -> VIEW_CONN, e0+1 -> page 2 = DEMO) ---
        print('    demo...')
        keys = _hold_keys('FX', 500) + ',1500:e0:+1'
        ppm = _shot(fm1_live, fwsc, keys, 2500, work, net=False)
        _ppm_to_png(ppm, out_dir / f'{idx:02d}_demo.png')
        idx += 1

        # --- FILES (SAVE hold -> VIEW_DEMO page 1 = FILES) ---
        # With a USB disk image so the browser shows actual .dock files.
        # The emulator needs both disk and ethernet on the hub for the host
        # controller to enumerate the disk.  modd_dev_network's USB_NET_AUTO
        # steals the host, so we build the base modd defconfig (no auto-
        # ethernet) for these two shots.
        disk_img = SCREENSHOT_DISK_IMG if SCREENSHOT_DISK_IMG.is_file() else None
        if disk_img:
            print('    files (with USB disk, building modd defconfig)...')
            disk_fwsc = _build_firmware_defconfig('modd')
        else:
            print('    files (no disk image, will show empty state)...')
            disk_fwsc = fwsc
        keys = _hold_keys('SAVE', 2000)
        ppm = _shot(fm1_live, disk_fwsc, keys, 20000, work, net=True,
                     disk_image=disk_img)
        _ppm_to_png(ppm, out_dir / f'{idx:02d}_files.png')
        idx += 1

        # --- DISK (SAVE hold -> VIEW_DEMO, e0+1 -> page 2 = DISK) ---
        print('    disk (with USB disk)...' if disk_img else '    disk...')
        keys = _hold_keys('SAVE', 2000) + ',3000:e0:+1'
        ppm = _shot(fm1_live, disk_fwsc, keys, 20000, work, net=True,
                     disk_image=disk_img)
        _ppm_to_png(ppm, out_dir / f'{idx:02d}_disk.png')
        idx += 1

        # --- DEBUG LOG (GLO hold toggles debug page) ---
        print('    log...')
        keys = _hold_keys('GLO', 500)
        ppm = _shot(fm1_live, fwsc, keys, 2000, work, net=False)
        _ppm_to_png(ppm, out_dir / f'{idx:02d}_log.png')
        idx += 1

    return idx


def capture_modd(build: bool = True) -> Path:
    """Capture modd theme screenshots using fm1-live."""
    fm1_live = _find_fm1_live()
    if fm1_live is None:
        print('fm1-live not found. Set FM1_EMU_DIR or build fm1-live from '
              'fm1-emu master.', file=sys.stderr)
        raise SystemExit(1)
    print(f'  Using fm1-live: {fm1_live}')

    defconfig = THEME_DEFCONFIG['modd']
    if build:
        fwsc = _build_firmware('modd')
    else:
        fwsc = _fwsc_path(defconfig)
        if not fwsc.is_file():
            print(f'Firmware not built ({fwsc}).', file=sys.stderr)
            raise SystemExit(1)

    out_dir = ROOT / 'docs' / 'ui' / 'themes' / 'modd' / 'screenshots'
    out_dir.mkdir(parents=True, exist_ok=True)
    for f in out_dir.glob('*.png'):
        f.unlink()

    idx = _capture_modd_live(fm1_live, fwsc, out_dir)

    _copy_stable_names(out_dir)
    _inject_readme_screenshots(out_dir)
    _inject_theme_gallery(out_dir)
    print(f'  {idx} screenshots saved to {out_dir}')
    return out_dir


# ---------------------------------------------------------------------------
# Playwright backend (felucca, other themes without network features)
# ---------------------------------------------------------------------------

def _default_emu_dir() -> Path:
    root = os.environ.get('FM1_EMU_ROOT',
                          '/Users/dmitry/dev/modd-fm/fm1-emu/.worktrees/master')
    return Path(os.environ.get('FM1_WEB_DIR', Path(root) / '_site'))


def capture_theme_playwright(theme: str, build: bool = True) -> Path:
    """Capture screenshots for a non-modd theme via the WASM emulator."""
    import http.server
    import socket
    import threading

    from fm1_controls import FM1Controls
    from gen_ui_map import control_path, load_defconfig, load_nav_map

    emu_dir = Path(os.environ.get('FM1_WEB_DIR', _default_emu_dir()))
    if not (emu_dir / 'fm1_web.wasm').is_file():
        print(f'Emulator not found at {emu_dir}. '
              'Set FM1_WEB_DIR or run "task fm1-emulator:download".', file=sys.stderr)
        raise SystemExit(1)

    nav_map = load_nav_map(theme, root=ROOT)
    defconfig = THEME_DEFCONFIG.get(theme, theme)
    config = load_defconfig(defconfig, root=ROOT)
    steps = control_path(nav_map, config=config if config else None)
    out_dir = ROOT / 'docs' / 'ui' / 'themes' / theme / 'screenshots'

    if build:
        fwsc = _build_firmware(theme)
    else:
        fwsc = _fwsc_path(defconfig)
        if not fwsc.is_file():
            print(f'Firmware not built ({fwsc}).', file=sys.stderr)
            raise SystemExit(1)

    def _make_handler(directory):
        class _Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *a, **kw):
                super().__init__(*a, directory=str(directory), **kw)
            def end_headers(self):
                self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
                self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
                super().end_headers()
            def log_message(self, fmt, *args):
                pass
        return _Handler

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]

    server = http.server.HTTPServer(('127.0.0.1', port), _make_handler(emu_dir))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    out_dir.mkdir(parents=True, exist_ok=True)
    for f in out_dir.glob('*.png'):
        f.unlink()

    from playwright.sync_api import sync_playwright
    pw = sync_playwright().start()
    chromium_path = os.environ.get('CHROMIUM_PATH') or shutil.which('chromium')
    launch_kw = dict(headless=True, args=['--autoplay-policy=no-user-gesture-required'])
    if chromium_path and os.path.isfile(chromium_path):
        launch_kw['executable_path'] = chromium_path
    browser = pw.chromium.launch(**launch_kw)

    try:
        url = f'http://127.0.0.1:{port}/?fw=local&mhz=10&debug=1'
        page = browser.new_page()
        page.goto(url)
        page.evaluate("async () => { await import('./main.js'); }")

        print(f'  Booting emulator...')
        _boot_emulator_pw(page, fwsc)
        panel = FM1Controls(page)

        idx = 0
        if steps and steps[0]['view'] == 'VIEW_HOME' and steps[0]['action'] == 'press':
            page.locator('canvas#panel').screenshot(
                path=str(out_dir / '00_home_panel.png'))
            _screenshot_lcd_pw(page, str(out_dir / '00_home.png'))
            print(f'    HOME -> 00_home.png + 00_home_panel.png')
            steps = steps[1:]
            idx = 1

        for step in steps:
            _execute_step_pw(panel, page, step)
            name = step['page'].lower().replace(' ', '_')
            filename = f'{idx:02d}_{name}.png'
            _screenshot_lcd_pw(page, str(out_dir / filename))
            print(f'    {step["page"]} -> {filename}')
            idx += 1

        page.close()
    finally:
        browser.close()
        pw.stop()
        server.shutdown()

    _copy_stable_names(out_dir)
    _inject_theme_gallery(out_dir)
    print(f'  {idx} screenshots saved to {out_dir}')
    return out_dir


def _boot_emulator_pw(page, fwsc: Path) -> None:
    page.locator('#open').set_input_files(str(fwsc))
    page.wait_for_function(
        'document.getElementById("status").textContent.includes("ready")')
    page.locator('#start').click()
    page.wait_for_function('''() => {
        const t = document.getElementById("status")?.textContent || "";
        return t.includes("instructions/s") || t.includes("real time");
    }''', timeout=30_000)
    page.wait_for_function('''() => {
        const c = document.getElementById('panel');
        const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
        let n = 0;
        for (let i = 0; i < p.length; i += 4)
            if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
        return n > 500;
    }''', timeout=120_000)
    page.wait_for_timeout(5_000)


def _screenshot_lcd_pw(page, path: str) -> None:
    import base64
    data_url = page.evaluate('''() => {
        const src = document.getElementById('panel');
        const W = src.width, H = src.height;
        const lx = Math.round(370 * W / 1280);
        const ly = Math.round(44 * H / 820);
        const lw = Math.round(480 * W / 1280);
        const lh = Math.round(480 * H / 820);
        const dst = document.createElement('canvas');
        dst.width = lw; dst.height = lh;
        dst.getContext('2d').drawImage(src, lx, ly, lw, lh, 0, 0, lw, lh);
        return dst.toDataURL('image/png');
    }''')
    png = base64.b64decode(data_url.split(',', 1)[1])
    Path(path).write_bytes(png)


def _execute_step_pw(panel, page, step: dict) -> None:
    if step['action'] == 'hold':
        panel.press_button(step['button'], hold_ms=900, delay_ms=500)
        page.wait_for_timeout(5_000)
    elif step['action'] == 'press':
        panel.press_button(step['button'], hold_ms=40, delay_ms=500)
        page.wait_for_timeout(3_000)
    elif step['action'] == 'cycle':
        panel.rotate_encoder(step['encoder'], direction=step['direction'],
                             delay_ms=500)
        page.wait_for_timeout(5_000)


# ---------------------------------------------------------------------------
# Shared: stable names, README injection
# ---------------------------------------------------------------------------

def _copy_stable_names(out_dir: Path) -> None:
    for f in sorted(out_dir.glob('[0-9][0-9]_*.png')):
        stem = f.stem
        stable = stem.split('_', 1)[1] if '_' in stem else stem
        dst = out_dir / f'{stable}.png'
        if f != dst:
            shutil.copy2(f, dst)


def _inject_readme_screenshots(out_dir: Path) -> None:
    readme = ROOT / 'README.md'
    text = readme.read_text()
    if README_MARKER_START not in text:
        return

    rel = out_dir.relative_to(ROOT)
    lines = [README_MARKER_START, '<p>']
    for name in HERO_SHOTS:
        f = out_dir / f'{name}.png'
        if f.is_file():
            alt = HERO_ALT.get(name, name)
            lines.append(f'  <img src="{rel}/{name}.png" alt="modd {alt}" width="240">')
    lines.append('</p>')
    lines.append('')

    settings_files = [f for t in SETTINGS_SHOT_TABS
                      if (f := out_dir / f'settings_{t}.png').is_file()]
    if settings_files:
        lines.append('<details><summary>Settings tabs</summary>')
        lines.append('<p>')
        for t in SETTINGS_SHOT_TABS:
            f = out_dir / f'settings_{t}.png'
            if f.is_file():
                lines.append(f'  <img src="{rel}/settings_{t}.png" alt="settings {t}" width="240">')
        lines.append('</p>')
        lines.append('</details>')
        lines.append('')

    palette_files = sorted(out_dir.glob('palette_*.png'))
    if palette_files:
        lines.append('<details><summary>Color palettes</summary>')
        lines.append('<p>')
        for f in palette_files:
            lines.append(f'  <img src="{rel}/{f.name}" alt="{f.stem}" width="160">')
        lines.append('</p>')
        lines.append('</details>')

    lines.append(README_MARKER_END)
    block = '\n'.join(lines)

    before = text[:text.index(README_MARKER_START)]
    after = text[text.index(README_MARKER_END) + len(README_MARKER_END):]
    readme.write_text(before + block + after)
    print(f'  Injected screenshots into {readme}')


def _inject_theme_gallery(out_dir: Path) -> None:
    theme_md = out_dir.parent / 'THEME.md'
    if not theme_md.is_file():
        return
    text = theme_md.read_text()
    if GALLERY_MARKER_START not in text:
        return

    lines = [GALLERY_MARKER_START]

    pages = [(f, label) for f, label in PAGE_SHOTS
             if (out_dir / f'{f}.png').is_file()]
    if pages:
        lines.append('## Pages')
        lines.append('')
        lines.append('| ' + ' | '.join(label for _, label in pages) + ' |')
        lines.append('|' + '|'.join(':----:' for _ in pages) + '|')
        lines.append('| ' + ' | '.join(
            f'![{label}](screenshots/{f}.png)' for f, label in pages) + ' |')

    tabs = [t for t in SETTINGS_SHOT_TABS
            if (out_dir / f'settings_{t}.png').is_file()]
    if tabs:
        lines.append('')
        lines.append('## Settings')
        lines.append('')
        lines.append('| ' + ' | '.join(t.upper() for t in tabs) + ' |')
        lines.append('|' + '|'.join(':----:' for _ in tabs) + '|')
        lines.append('| ' + ' | '.join(
            f'![settings {t}](screenshots/settings_{t}.png)' for t in tabs) + ' |')

    palettes = sorted(out_dir.glob('palette_*.png'))
    if palettes:
        lines.append('')
        lines.append('## Color Palettes')
        lines.append('')
        row_size = 5
        for i in range(0, len(palettes), row_size):
            chunk = palettes[i:i + row_size]
            nums = [str(i + j) for j in range(len(chunk))]
            lines.append('| ' + ' | '.join(nums) + ' |')
            lines.append('|' + '|'.join(':--:' for _ in chunk) + '|')
            lines.append('| ' + ' | '.join(
                f'![palette {i+j}](screenshots/{c.name})' for j, c in enumerate(chunk)) + ' |')
            if i + row_size < len(palettes):
                lines.append('')

    lines.append(GALLERY_MARKER_END)
    block = '\n'.join(lines)

    before = text[:text.index(GALLERY_MARKER_START)]
    after = text[text.index(GALLERY_MARKER_END) + len(GALLERY_MARKER_END):]
    theme_md.write_text(before + block + after)
    print(f'  Injected gallery into {theme_md.name}')


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    args = sys.argv[1:]
    if not args or args[0] in ('-h', '--help'):
        print(__doc__)
        sys.exit(0)

    build = '--no-build' not in args
    args = [a for a in args if a != '--no-build']

    if args[0] == '--all':
        themes = discover_themes()
    else:
        themes = args

    for theme in themes:
        print(f'[{theme}]')
        if theme == 'modd':
            capture_modd(build=build)
        else:
            capture_theme_playwright(theme, build=build)


if __name__ == '__main__':
    main()
