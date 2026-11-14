#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Bench: modd theme settings walkthrough via fm1-live --shot.

Headless emulator with FM1_BENCH_KEYS scripted input. Produces PPM frames
for visual inspection. Same scenario as e2e_settings_test.py:

  1. Hold HOME to open settings
  2. SELECT +1 x3 to cycle through all tabs
  3. ALGO +1 x3 to scroll rows
  4. PRESET +1 to change a value
  5. HOME tap to return

Output: tests/results/<timestamp>-bench_settings_modd/
  - frames/    PPM frames (every 16ms of emulated time)
  - bench.log  emulator stats

Usage:
    uv run python tests/ui/themes/modd/bench_settings.py
    LCD_ONLY=1 uv run python tests/ui/themes/modd/bench_settings.py
"""
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
FWSC = ROOT / 'build' / 'modd.fwsc'
FM1_LIVE = Path.home() / '.fm1-emulator' / 'bin' / 'fm1-live'
RESULTS = ROOT / 'tests' / 'results'


def results_dir():
    ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')
    d = RESULTS / f'{ts}-bench_settings_modd'
    d.mkdir(parents=True, exist_ok=True)
    return d


def build_keys_script():
    """FM1_BENCH_KEYS for a settings walkthrough.

    Encoder mapping (modd theme settings):
      enc0 = SELECT (switches tabs)
      enc1 = ALGO (scrolls rows)
      enc2 = PRESET/K1 (changes value)

    Timeline (firmware boots ~700ms):
      1000-1600: hold HOME (600ms) to open settings
      2200: screenshot initial tab
      2500: SELECT +1 (next tab)
      3000: SELECT +1 (next tab)
      3500: SELECT +1 (wraps or next)
      4500: ALGO +1 x3 (scroll rows)
      5500: PRESET +1 (change value)
      6500: HOME tap (return)
      7000: end
    """
    items = []

    # long-press HOME (button '7', held 600ms)
    items.append('1000-1600:HOME')

    # SELECT +1 x3 (switch tabs, enc0)
    items.append('2500:enc0:+1')
    items.append('3000:enc0:+1')
    items.append('3500:enc0:+1')

    # ALGO +1 x3 (scroll rows, enc1)
    for i in range(3):
        items.append(f'{4500 + i * 200}:enc1:+1')

    # PRESET +1 (change value, enc2)
    items.append('5500:enc2:+1')

    # HOME tap (return)
    items.append('6500-6580:HOME')

    return ','.join(items)


def main():
    if not FWSC.is_file():
        print(f'Firmware not found at {FWSC}. Run "task build" first.')
        sys.exit(1)

    if not FM1_LIVE.is_file():
        print(f'fm1-live not found at {FM1_LIVE}.')
        print('Run "task fm1-emulator:download:fm1-emu" first.')
        sys.exit(1)

    out = results_dir()
    frame_dir = out / 'frames'
    frame_dir.mkdir(exist_ok=True)

    keys = build_keys_script()
    duration_ms = 7500
    lcd_only = bool(os.environ.get('LCD_ONLY'))

    print(f'Running {duration_ms}ms emulated settings walkthrough...')
    print(f'Keys: {keys[:80]}...')

    env = {**os.environ}
    env['FM1_BENCH_KEYS'] = keys
    env['FM1_WARMUP_MS'] = '800'

    cmd = [
        str(FM1_LIVE),
        '--fwsc', str(FWSC),
        '--shot', str(duration_ms),
        '--record', str(frame_dir),
    ]
    if lcd_only:
        cmd.append('--lcd-only')

    r = subprocess.run(cmd, capture_output=True, text=True,
                       timeout=300, env=env, cwd=str(ROOT))

    log = (f'--- stdout ---\n{r.stdout}\n--- stderr ---\n{r.stderr}\n'
           f'--- exit {r.returncode} ---\n')
    (out / 'bench.log').write_text(log)
    print(r.stdout.strip() if r.stdout.strip() else r.stderr.strip())

    frames = sorted(frame_dir.glob('*.ppm'))
    print(f'  {len(frames)} PPM frames captured')
    print(f'\nResults: {out}')


if __name__ == '__main__':
    main()
