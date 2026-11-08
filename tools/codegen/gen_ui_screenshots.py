#!/usr/bin/env python3
#  SPDX-License-Identifier: GPL-3.0-only
#  Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Capture per-page screenshots for a UI theme via the fm1-emu WASM emulator.

Builds firmware for the theme's defconfig, boots it in the emulator via
Playwright, walks every page using control_path(), and saves a screenshot
of each page to docs/ui/themes/<theme>/screenshots/.

Usage:
  python3 tools/codegen/gen_ui_screenshots.py modd
  python3 tools/codegen/gen_ui_screenshots.py felucca
  python3 tools/codegen/gen_ui_screenshots.py --all
"""
from __future__ import annotations

import http.server
import os
import shutil
import socket
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'tools' / 'codegen'))

from fm1_controls import FM1Controls
from gen_ui_map import control_path, discover_themes, load_nav_map

EMU_DIR = Path(os.environ.get('FM1_WEB_DIR', Path.home() / '.fm1-emulator' / 'web'))
FWSC = ROOT / 'build' / 'modd.fwsc'

THEME_DEFCONFIG = {
    'felucca': 'felucca',
    'modd': 'modd',
}


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


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


def _build_firmware(theme: str) -> Path:
    """Build firmware for the theme's defconfig. Returns path to .fwsc."""
    import subprocess
    defconfig = THEME_DEFCONFIG.get(theme, theme)
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
    if not FWSC.is_file():
        print(f'  Build succeeded but {FWSC} not found', file=sys.stderr)
        raise SystemExit(1)
    print(f'  Built {FWSC}')
    return FWSC


def _boot_emulator(page, fwsc: Path) -> None:
    """Upload firmware and wait for boot."""
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


def _screenshot_lcd(page, path: str) -> None:
    """Extract just the LCD square from the panel canvas and save it."""
    import base64
    data_url = page.evaluate('''() => {
        const src = document.getElementById('panel');
        const W = src.width, H = src.height;
        // LCD is 240x240 firmware pixels drawn at 2x in the center-top area.
        // Native position: ~(370, 44) size ~(480, 476) on a 1280x820 canvas.
        // Scale to actual canvas dimensions.
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


_LCD_HASH_BODY = (
    'const c = document.getElementById("panel");'
    'const W = c.width, H = c.height;'
    'const lx = Math.round(370 * W / 1280);'
    'const ly = Math.round(44 * H / 820);'
    'const lw = Math.round(480 * W / 1280);'
    'const lh = Math.round(480 * H / 820);'
    'const d = c.getContext("2d").getImageData(lx, ly, lw, lh).data;'
    'let h = 0;'
    'for (let i = 0; i < d.length; i += 16) h = ((h << 5) - h + d[i]) | 0;'
)


def _lcd_hash(page) -> int:
    return page.evaluate(f'() => {{ {_LCD_HASH_BODY} return h; }}')


def _wait_lcd_change(page, prev_hash: int, timeout_ms: int = 15_000) -> int:
    """Poll until the LCD content differs from prev_hash. Returns new hash."""
    page.wait_for_function(
        f'() => {{ {_LCD_HASH_BODY} return h !== {prev_hash}; }}',
        timeout=timeout_ms)
    page.wait_for_timeout(500)
    return _lcd_hash(page)


def _execute_step(panel: FM1Controls, page, step: dict) -> None:
    """Execute a single control_path step and wait for the LCD to settle."""
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


def capture_theme(theme: str, build: bool = True) -> Path:
    """Capture screenshots for all pages in a theme. Returns output dir."""
    from playwright.sync_api import sync_playwright

    if not (EMU_DIR / 'fm1_web.wasm').is_file():
        print(f'Emulator not found at {EMU_DIR}. '
              'Set FM1_WEB_DIR or run "task fm1-emulator:download".', file=sys.stderr)
        raise SystemExit(1)

    nav_map = load_nav_map(theme, root=ROOT)
    steps = control_path(nav_map)
    out_dir = ROOT / 'docs' / 'ui' / 'themes' / theme / 'screenshots'

    if build:
        fwsc = _build_firmware(theme)
    else:
        fwsc = FWSC
        if not fwsc.is_file():
            print(f'Firmware not built ({fwsc}). Pass --build or run "task build".', file=sys.stderr)
            raise SystemExit(1)

    port = _find_free_port()
    server = http.server.HTTPServer(('127.0.0.1', port), _make_handler(EMU_DIR))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    out_dir.mkdir(parents=True, exist_ok=True)
    for f in out_dir.glob('*.png'):
        f.unlink()

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
        _boot_emulator(page, fwsc)
        panel = FM1Controls(page)

        # Boot always lands on HOME: capture both panel and LCD
        idx = 0
        if steps and steps[0]['view'] == 'VIEW_HOME' and steps[0]['action'] == 'press':
            page.locator('canvas#panel').screenshot(
                path=str(out_dir / '00_home_panel.png'))
            _screenshot_lcd(page, str(out_dir / '00_home.png'))
            print(f'    [1/{len(steps)}] {steps[0]["page"]} -> 00_home.png + 00_home_panel.png (boot)')
            steps = steps[1:]
            idx = 1

        print(f'  Walking {len(steps)} remaining steps...')
        for i, step in enumerate(steps):
            _execute_step(panel, page, step)
            name = step['page'].lower().replace(' ', '_')
            filename = f'{idx + i:02d}_{name}.png'
            _screenshot_lcd(page, str(out_dir / filename))
            print(f'    [{idx + i + 1}/{idx + len(steps)}] {step["page"]} -> {filename} (lcd)')

        page.close()
    finally:
        browser.close()
        pw.stop()
        server.shutdown()

    total = idx + len(steps)
    print(f'  {total} screenshots saved to {out_dir}')
    return out_dir


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
        capture_theme(theme, build=build)


if __name__ == '__main__':
    main()
