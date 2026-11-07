# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Every `task debug:*` run leaves its output in .scratch/debug/<name>.log.

The 2026-11-04 session lost a device status readout because a debug task printed to the
terminal only. This pins the convention: each debug task creates .scratch/debug, writes a dated
header line to its own log file, and tees every command's output into that file.

Generic tasks (dump, follow) live in the main Taskfile.yml.
Specialized tasks (wl0, sweep, ind, input) live in .scratch/Taskfile.yml.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TASKFILE = ROOT / 'Taskfile.yml'
SCRATCH_TASKFILE = ROOT / '.scratch' / 'Taskfile.yml'


def _parse_tasks(path):
    """{name: body} for every two-space-indent task in a Taskfile."""
    text = path.read_text()
    idx = text.find('\ntasks:\n')
    if idx >= 0:
        text = text[idx:]
    found = {}
    for m in re.finditer(r'^  ([a-z][a-z0-9:_-]*):\n(.*?)(?=^  [a-z][a-z0-9:_-]*:\n|\Z)', text, re.M | re.S):
        found[m.group(1)] = m.group(2)
    return found


def _debug_tasks():
    tasks = {}
    for name, body in _parse_tasks(TASKFILE).items():
        if name.startswith('debug:'):
            tasks[name] = body
    if SCRATCH_TASKFILE.exists():
        scratch = _parse_tasks(SCRATCH_TASKFILE)
        for name in ('wl0', 'sweep', 'ind', 'input'):
            if name in scratch:
                tasks[f'debug:{name}'] = scratch[name]
    return tasks


def test_there_are_debug_tasks():
    assert len(_debug_tasks()) >= 1


def _check_log_convention(name, body):
    slug = name.split(":", 1)[1]
    assert 'mkdir -p .scratch/debug' in body, f'{name}: must create .scratch/debug (or .scratch/debug/logs)'
    assert 'tee "$LOG"' in body or f'tee .scratch/debug/{slug}.log' in body, \
        f'{name}: output must be teed into $LOG or .scratch/debug/{slug}.log'
    assert f'{slug}.log' in body, f'{name}: must reference {slug}.log (direct or via symlink)'


def test_every_debug_task_tees_into_its_own_log():
    for name, body in _debug_tasks().items():
        _check_log_convention(name, body)


def test_debug_section_is_commented():
    text = TASKFILE.read_text()
    assert re.search(r'^  # .*debug', text, re.M), 'convention comment above the debug tasks'
