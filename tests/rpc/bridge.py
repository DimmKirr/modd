#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""RPC bridge: WebSocket server backed by fm1-emu via Playwright (async).

Boots the WASM emulator, then exposes FM1.* RPC methods over WebSocket.
Each method calls page.evaluate() to read emulator state or send commands.
Both Playwright and the WebSocket server share one asyncio event loop.
"""
import asyncio
import base64
import json
import http.server
import os
import socket
import sys
import threading
import time
from pathlib import Path

import websockets


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from e2e_fixtures import fwsc_path

_EMU_ROOT = Path(os.environ.get('FM1_EMU_ROOT',
                                  '/Users/dmitry/dev/modd-fm/fm1-emu/.worktrees/master'))
EMU_DIR = Path(os.environ.get('FM1_WEB_DIR', _EMU_ROOT / '_site'))
EMU_WIFI_DIR = Path(os.environ.get(
    'FM1_WIFI_WEB_DIR', _EMU_ROOT / '_site'))
FWSC_PATH = fwsc_path()

if 'PLAYWRIGHT_BROWSERS_PATH' not in os.environ:
    _pw_cache = Path.home() / '.cache' / 'ms-playwright'
    if _pw_cache.is_dir():
        os.environ['PLAYWRIGHT_BROWSERS_PATH'] = str(_pw_cache)


def _make_handler(directory):
    class _H(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(directory), **kw)
        def end_headers(self):
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            super().end_headers()
        def log_message(self, *a):
            pass
    return _H


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class Bridge:
    """Manages the emulator page and handles RPC method dispatch."""

    def __init__(self, page):
        self.page = page
        self.log = []
        self._console_log = []
        page.on('console', lambda msg: self._console_log.append(
            f'[{msg.type}] {msg.text}'))
        page.on('pageerror', lambda err: self._console_log.append(
            f'[pageerror] {err}'))

    async def dispatch(self, method, params):
        if method == 'FM1.getInfo':
            return await self._get_info()
        elif method == 'FM1.memoryRead':
            return await self._mem_read(params)
        elif method == 'FM1.getAudioMetrics':
            return await self._audio_metrics()
        elif method == 'FM1.getEngineState':
            return await self._engine_state()
        elif method == 'FM1.sendMidi':
            return await self._send_midi(params)
        elif method == 'FM1.pressButton':
            return await self._press_button(params)
        elif method == 'FM1.turnEncoder':
            return await self._turn_encoder(params)
        elif method == 'FM1.getLeds':
            return await self._get_leds()
        elif method == 'FM1.navigate':
            return await self._navigate(params)
        elif method == 'Target.getTargetInfo':
            return {'targetInfo': {
                'targetId': 'fm1-0', 'type': 'page',
                'title': 'FM-1', 'url': 'fm1://localhost'}}
        else:
            raise ValueError(f'Unknown method: {method}')

    async def _get_info(self):
        return await self.page.evaluate('''() => {
            const s = document.getElementById('status')?.textContent || '';
            // fw is module-scoped; parse firmware name from the <select> or status
            const sel = document.getElementById('fw');
            const fwName = sel?.selectedOptions?.[0]?.textContent?.trim()
                || s.split(':')[0]?.trim() || 'unknown';
            return {
                firmware: fwName,
                status: s,
                uptime_ms: typeof frame !== 'undefined' && frame ? (frame.ms || 0) : 0,
                halted: typeof frame !== 'undefined' && frame ? !!frame.halted : false,
            };
        }''')

    async def _mem_read(self, params):
        addr = params.get('address', 0)
        length = min(params.get('length', 64), 4096)
        return await self.page.evaluate(f'''() => {{
            // _rpc_wasm[0] is the panel WASM instance captured by our init script
            const inst = window._rpc_wasm?.[0];
            if (!inst?.exports?.memory) return {{ data: "", address: {addr}, length: 0, error: "no wasm instance" }};
            const buf = inst.exports.memory.buffer;
            const maxAddr = buf.byteLength;
            const addr = {addr};
            const len = Math.min({length}, maxAddr - addr);
            if (addr < 0 || addr >= maxAddr) return {{ data: "", address: addr, length: 0 }};
            const bytes = new Uint8Array(buf, addr, len);
            let binary = '';
            for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
            return {{ data: btoa(binary), address: addr, length: len }};
        }}''')

    async def _audio_metrics(self):
        return await self.page.evaluate('''() => {
            if (typeof frame === 'undefined' || !frame) return {
                sample_rate: 44100, block_size: 128, busy_ms: 0, worst_ms: 0, quanta: 0
            };
            return {
                sample_rate: 44100,
                block_size: 128,
                busy_ms: frame.busy || 0,
                worst_ms: frame.worst || 0,
                quanta: frame.quanta || 0,
                short: frame.short || 0,
            };
        }''')

    async def _engine_state(self):
        return await self.page.evaluate('''() => ({
            engine_index: 0,
            halted: typeof frame !== 'undefined' && frame ? !!frame.halted : false,
            ms: typeof frame !== 'undefined' && frame ? (frame.ms || 0) : 0,
        })''')

    async def _send_midi(self, params):
        data = params.get('data', [])
        if not data or len(data) < 1:
            raise ValueError('data must be a non-empty array of MIDI bytes')
        data_json = json.dumps(data)
        await self.page.evaluate(f'''() => {{
            const n = typeof node !== 'undefined' ? node : null;
            if (n && n.port) n.port.postMessage({{type:"midi",bytes:{data_json}}});
        }}''')
        return {'ok': True, 'bytes_sent': len(data)}

    async def _press_button(self, params):
        key_id = params.get('id', 0)
        await self.page.evaluate(f'''() => {{
            const n = typeof node !== 'undefined' ? node : null;
            if (n && n.port) {{
                n.port.postMessage({{type:"key",id:{key_id},down:true}});
                setTimeout(() => n.port.postMessage({{type:"key",id:{key_id},down:false}}), 100);
            }}
        }}''')
        return {'ok': True, 'key': key_id}

    async def _turn_encoder(self, params):
        enc = params.get('encoder', 0)
        clicks = params.get('clicks', 1)
        await self.page.evaluate(f'''() => {{
            const n = typeof node !== 'undefined' ? node : null;
            if (n && n.port) n.port.postMessage({{type:"enc",enc:{enc},clicks:{clicks}}});
        }}''')
        return {'ok': True, 'encoder': enc, 'clicks': clicks}

    async def _get_leds(self):
        return await self.page.evaluate('''() => {
            if (typeof frame === 'undefined' || !frame || !frame.leds) return { leds: [] };
            return { leds: Array.from(frame.leds) };
        }''')

    async def _navigate(self, params):
        url = params.get('url', '')
        if not url.startswith('modd://'):
            raise ValueError(f'URL must start with modd://, got: {url}')
        path = url[len('modd://'):]
        parts = [p for p in path.strip('/').split('/') if p]
        if not parts:
            raise ValueError('Empty navigation path')

        page_name = parts[0].lower()
        sub = parts[1].lower() if len(parts) > 1 else None

        result = await self._navigate_to(page_name, sub)
        await asyncio.sleep(0.3)
        return result

    async def _navigate_to(self, page_name, sub=None):
        ALIASES = {
            'menu': 'settings',
            'conn': 'weather',
        }
        page_name = ALIASES.get(page_name, page_name)

        BUTTON_NAV = {
            'home':     ('7', False),
            'settings': ('7', True),
            'weather':  ('1', True),
            'wx':       ('1', True),
            'demo':     ('1', True),
            'files':    ('8', True),
            'disk':     ('8', True),
            'log':      ('6', True),
            'debug':    ('6', True),
        }

        if page_name == 'palette' or page_name == 'colors':
            return await self._navigate_to_palette(sub)

        if page_name not in BUTTON_NAV:
            raise ValueError(
                f'Unknown page: {page_name}. '
                f'Available: {", ".join(sorted(BUTTON_NAV))}')

        key, is_hold = BUTTON_NAV[page_name]
        await self._snapshot_lcd()

        if is_hold:
            await self._key_hold(key, 900)
        else:
            await self._key_tap(key, 80)

        await self._wait_lcd_change(timeout_ms=5000)

        if page_name in ('demo', 'disk'):
            conn_pages = ['weather', 'wx', 'demo']
            demo_pages = ['files', 'disk']
            target_list = demo_pages if page_name == 'disk' else conn_pages
            if page_name != target_list[0]:
                clicks = target_list.index(page_name)
                for _ in range(clicks):
                    await self._snapshot_lcd()
                    await self._enc_click('select', 1)
                    await self._wait_lcd_change(timeout_ms=3000)

        if page_name == 'settings' and sub:
            tab_order = ['display', 'control', 'audio', 'midi', 'system',
                         'hw', 'net']
            if sub not in tab_order:
                raise ValueError(
                    f'Unknown settings tab: {sub}. '
                    f'Available: {", ".join(tab_order)}')
            target_idx = tab_order.index(sub)
            for _ in range(target_idx):
                await self._snapshot_lcd()
                await self._enc_click('select', 1)
                await self._wait_lcd_change(timeout_ms=3000)

        return {'ok': True, 'page': page_name, 'sub': sub}

    async def _navigate_to_palette(self, sub=None):
        """Navigate to the COLORS page (hold HOME to settings, then cycle).

        COLORS is in VIEW_SETTINGS, so: hold HOME -> settings view, then
        cycle forward through settings pages until we land on COLORS.
        Then if sub is a palette index (e.g. '03'), turn an encoder to reach it.
        """
        await self._snapshot_lcd()
        await self._key_hold('7', 900)
        await self._wait_lcd_change(timeout_ms=5000)

        # COLORS is the second page in VIEW_SETTINGS (after MENU).
        # Cycle forward once to reach it.
        await self._snapshot_lcd()
        await self._key_hold('7', 900)
        await self._wait_lcd_change(timeout_ms=5000)

        if sub is not None:
            try:
                target_idx = int(sub)
            except ValueError:
                raise ValueError(f'Palette sub must be a number, got: {sub}')
            # Turn the algorithm encoder (non-select) to change palette.
            # We go to palette 0 first (turn left a lot), then right to target.
            for _ in range(15):
                await self._enc_click('algorithm', -1)
                await asyncio.sleep(0.02)
            await asyncio.sleep(0.1)
            for _ in range(target_idx):
                await self._enc_click('algorithm', 1)
                await asyncio.sleep(0.02)
            await asyncio.sleep(0.2)

        return {'ok': True, 'page': 'palette', 'sub': sub}

    async def _key_tap(self, key, hold_ms=80):
        await self.page.evaluate('''(canvas) => {
            canvas.tabIndex = 0;
            canvas.focus({preventScroll: true});
        }''', await self.page.query_selector('canvas#panel'))
        await self.page.keyboard.down(key)
        await asyncio.sleep(hold_ms / 1000)
        await self.page.keyboard.up(key)
        await asyncio.sleep(0.1)

    async def _key_hold(self, key, hold_ms=900):
        await self.page.evaluate('''(canvas) => {
            canvas.tabIndex = 0;
            canvas.focus({preventScroll: true});
        }''', await self.page.query_selector('canvas#panel'))
        await self.page.keyboard.down(key)
        await asyncio.sleep(hold_ms / 1000)
        await self.page.keyboard.up(key)
        await asyncio.sleep(0.2)

    async def _enc_click(self, enc_name, direction):
        keys = {'select': ('ArrowRight', 'ArrowLeft'),
                'presets': ('ArrowUp', 'ArrowDown'),
                'algorithm': ('.', ',')}
        pair = keys.get(enc_name)
        if not pair:
            raise ValueError(f'Unknown encoder: {enc_name}')
        await self.page.evaluate('''(canvas) => {
            canvas.tabIndex = 0;
            canvas.focus({preventScroll: true});
        }''', await self.page.query_selector('canvas#panel'))
        await self.page.keyboard.press(pair[0] if direction > 0 else pair[1])
        await asyncio.sleep(0.05)

    async def _snapshot_lcd(self):
        await self.page.evaluate('''() => {
            const c = document.getElementById('panel');
            const ctx = c.getContext('2d', {willReadFrequently: true});
            window._navBefore = ctx.getImageData(370, 42, 480, 480).data.slice();
        }''')

    async def _wait_lcd_change(self, timeout_ms=5000):
        await self.page.evaluate(f'''() => {{
            const c = document.getElementById('panel');
            const ctx = c.getContext('2d', {{willReadFrequently: true}});
            window._navDone = false;
            const t0 = performance.now();
            function check() {{
                const after = ctx.getImageData(370, 42, 480, 480).data;
                let diff = 0;
                for (let i = 0; i < after.length; i += 16) {{
                    if (Math.abs(after[i] - window._navBefore[i]) > 10 ||
                        Math.abs(after[i+1] - window._navBefore[i+1]) > 10 ||
                        Math.abs(after[i+2] - window._navBefore[i+2]) > 10) diff++;
                }}
                if (diff > 50) {{
                    window._navDone = true;
                }} else if (performance.now() - t0 > {timeout_ms}) {{
                    window._navDone = true;
                }} else {{
                    setTimeout(check, 20);
                }}
            }}
            setTimeout(check, 30);
        }}''')
        await self.page.wait_for_function(
            'window._navDone === true', timeout=timeout_ms + 2000)

    async def screenshot_lcd(self):
        """Capture the LCD region (480x480) as PNG bytes."""
        data_url = await self.page.evaluate('''() => {
            const c = document.getElementById('panel');
            if (!c) return null;
            const ctx = c.getContext('2d');
            const tmp = document.createElement('canvas');
            tmp.width = 480; tmp.height = 480;
            tmp.getContext('2d').putImageData(
                ctx.getImageData(370, 42, 480, 480), 0, 0);
            return tmp.toDataURL('image/png');
        }''')
        if not data_url:
            return None
        return base64.b64decode(data_url.split(',', 1)[1])

    async def screenshot_full(self):
        """Capture the full panel canvas as PNG bytes."""
        buf = await self.page.locator('canvas#panel').screenshot()
        return bytes(buf)

    async def get_emulator_log(self):
        """Read the emulator's status line and fm1log DOM element."""
        return await self.page.evaluate('''() => ({
            status: document.getElementById('status')?.textContent || '',
            fm1log: document.getElementById('fm1log')?.textContent || '',
        })''')

    def format_log(self):
        """Return the full log as a string: RPC traffic + console + emulator."""
        lines = ['=== RPC request/response log ===']
        for entry in self.log:
            lines.append(json.dumps(entry, indent=None))
        lines.append('')
        lines.append(f'=== JS console ({len(self._console_log)} messages) ===')
        lines.extend(self._console_log or ['(no console output)'])
        return '\n'.join(lines)


async def _handle_ws(bridge, ws):
    """Handle one WebSocket connection with JSON-RPC."""
    async for raw in ws:
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            await ws.send(json.dumps({
                'error': {'code': -32700, 'message': 'Parse error'}}))
            continue

        msg_id = msg.get('id')
        method = msg.get('method', '')
        params = msg.get('params', {})
        t0 = time.monotonic()

        try:
            result = await bridge.dispatch(method, params)
            resp = {'id': msg_id, 'result': result}
        except ValueError as e:
            resp = {'id': msg_id, 'error': {'code': -32601, 'message': str(e)}}
        except Exception as e:
            resp = {'id': msg_id, 'error': {'code': -32603, 'message': str(e)}}

        elapsed_ms = (time.monotonic() - t0) * 1000
        bridge.log.append({
            'method': method, 'params': params, 'id': msg_id,
            'response': resp, 'elapsed_ms': round(elapsed_ms, 2),
        })
        await ws.send(json.dumps(resp))


def _start_http_server(directory, port):
    """Start a COOP/COEP HTTP server in a daemon thread."""
    server = http.server.HTTPServer(('127.0.0.1', port), _make_handler(directory))
    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()
    return server


async def boot_and_serve(fwsc_path=None, mhz=10, ws_port=0, chromium_path=None,
                         emu_dir=None):
    """Async entry point: boot emulator, start WS server, return context.

    Args:
        emu_dir: path to emulator web directory (defaults to EMU_DIR).
                 Set to EMU_WIFI_DIR to use the WiFi-enabled emulator.

    Returns a dict with keys: page, browser, pw, http_server, ws_server, ws_port, emu_dir.
    The caller's event loop must keep running for the WS server to serve.
    """
    import shutil
    from playwright.async_api import async_playwright

    fwsc = Path(fwsc_path) if fwsc_path else FWSC_PATH
    if emu_dir is None:
        emu_dir = EMU_DIR
    emu_dir = Path(emu_dir)
    if not fwsc.is_file():
        raise FileNotFoundError(f'Firmware not found: {fwsc}')
    if not (emu_dir / 'fm1_web.wasm').is_file():
        raise FileNotFoundError(f'Emulator not found in {emu_dir}')

    http_port = _free_port()
    http_server = _start_http_server(emu_dir, http_port)
    emu_url = f'http://127.0.0.1:{http_port}/?fw=local&mhz={mhz}&debug=1'

    pw = await async_playwright().start()
    launch_kw = dict(headless=True,
                     args=['--autoplay-policy=no-user-gesture-required'])
    chromium = chromium_path or os.environ.get('CHROMIUM_PATH') or shutil.which('chromium')
    if chromium and os.path.isfile(chromium):
        launch_kw['executable_path'] = chromium
    browser = await pw.chromium.launch(**launch_kw)
    page = await browser.new_page()

    # Hook WebAssembly.instantiate BEFORE main.js loads so we can capture
    # the panel WASM instance (module-scoped `ui` is otherwise inaccessible).
    await page.add_init_script('''
        window._rpc_wasm = [];
        const _origInstantiate = WebAssembly.instantiate;
        WebAssembly.instantiate = async function(source, imports) {
            const result = await _origInstantiate.call(this, source, imports);
            const inst = result.instance || result;
            window._rpc_wasm.push(inst);
            return result;
        };
    ''')

    await page.goto(emu_url)
    await page.evaluate("async () => { await import('./main.js'); }")

    await page.locator('#open').set_input_files(str(fwsc))
    await page.wait_for_function(
        'document.getElementById("status").textContent.includes("ready")',
        timeout=30_000)
    await page.locator('#start').click()
    # Wait for emulator running: stock shows "instructions/s", WiFi build shows "real time"
    await page.wait_for_function(
        '''() => {
            const s = document.getElementById("status")?.textContent || "";
            return s.includes("instructions/s") || s.includes("real time");
        }''',
        timeout=120_000)

    bridge = Bridge(page)
    if ws_port == 0:
        ws_port = _free_port()

    async def handler(ws):
        await _handle_ws(bridge, ws)

    ws_server = await websockets.serve(handler, '127.0.0.1', ws_port)

    return {
        'page': page,
        'bridge': bridge,
        'browser': browser,
        'pw': pw,
        'http_server': http_server,
        'ws_server': ws_server,
        'ws_port': ws_port,
        'emu_dir': str(emu_dir),
    }


async def shutdown(ctx):
    """Tear down everything returned by boot_and_serve."""
    if ctx.get('ws_server'):
        ctx['ws_server'].close()
        await ctx['ws_server'].wait_closed()
    if ctx.get('browser'):
        await ctx['browser'].close()
    if ctx.get('pw'):
        await ctx['pw'].stop()
    if ctx.get('http_server'):
        ctx['http_server'].shutdown()
