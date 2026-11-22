#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""The tier rule (tests/conftest.py is_api_test) and the Taskfiles and test files that depend on it."""
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from conftest import is_api_test  # noqa: E402

# Unit tier by the rule, but also run in the API run (config checks plus a live part that skips
# without builds and the emulator); it does not use ApiTestCase.
BOTH_TIERS = {'tests/usb/usb_otg_presence_safety_test.py'}

ROOT_TASKS = yaml.safe_load((ROOT / 'Taskfile.yml').read_text())['tasks']
CONFIG_TASKS = yaml.safe_load((ROOT / 'configs' / 'Taskfile.yml').read_text())['tasks']


def _cmds(task):
    return ' '.join(c if isinstance(c, str) else c.get('cmd', '') for c in task.get('cmds', []))


def _test_vars():
    """(task name, TESTS value) for every TESTS variable written in configs/Taskfile.yml."""
    for name, task in CONFIG_TASKS.items():
        for c in task.get('cmds', []):
            v = c.get('vars', {}).get('TESTS') if isinstance(c, dict) else None
            if v:
                yield name, v


def test_rule_examples():
    t = ROOT / 'tests'
    assert is_api_test(t / 'audio' / 'api_audio_test.py')
    assert is_api_test(t / 'ui' / 'shells' / 'modd' / 'api' / 'modd_api_test.py')
    assert is_api_test(t / 'made_up' / 'api_made_up_test.py')
    assert not is_api_test(t / 'build' / 'genconfig_test.py')
    assert not is_api_test(t / 'build' / 'taskfile_tiers_test.py')
    assert not is_api_test(t / 'rpc' / 'rpc_api_notes_test.py')


def test_unit_task_selects_the_unit_tier():
    cmds = _cmds(ROOT_TASKS['test:unit'])
    assert '--tier unit' in cmds
    assert '--ignore-glob' not in cmds, 'the tier rule lives in tests/conftest.py is_api_test'


def test_api_task_selects_the_api_tier():
    task = CONFIG_TASKS['test:modd_emu:api']
    api = [c['vars']['TESTS'] for c in task['cmds'] if isinstance(c, dict) and 'TESTS' in c.get('vars', {})]
    assert api and all('--tier api' in v for v in api)
    assert ROOT_TASKS['test:api']['cmds'][0]['task'] == 'modd:test:modd_emu:api'


@pytest.mark.parametrize('name,value', [(n, v) for n, v in _test_vars()])
def test_listed_test_paths_exist_and_are_api_tier(name, value):
    words = [w for w in value.split() if not w.startswith('--') and w not in ('api', 'unit', 'all')]
    for w in words:
        path = ROOT / w
        assert path.exists(), f'{name}: {w} does not exist'
        if name.endswith(':wifi'):
            assert is_api_test(path / 'x_test.py' if path.is_dir() else path), \
                f'{name}: {w} is not API tier by is_api_test'


def test_api_test_case_users_are_api_tier():
    wrong = []
    for f in sorted((ROOT / 'tests').rglob('*_test.py')):
        rel = f.relative_to(ROOT).as_posix()
        if rel in BOTH_TIERS or f == Path(__file__).resolve():
            continue
        if 'ApiTestCase' in f.read_text() and not is_api_test(f):
            wrong.append(rel)
    assert not wrong, f'use ApiTestCase but run in the unit tier (rename to api_*_test.py or move to api/): {wrong}'


def test_usb_safety_runs_after_the_api_tests():
    cmds = CONFIG_TASKS['test:modd_emu:api']['cmds']
    order = [c['task'] for c in cmds]
    assert order == ['_test:api', 'test:usb_safety']
    assert 'deps' not in CONFIG_TASKS['test:modd_emu:api']
    assert CONFIG_TASKS['test:usb_safety']['desc']
