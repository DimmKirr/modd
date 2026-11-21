#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""The open pi32v2 toolchain: Taskfile plumbing and fixture package lookup.

Hardware defconfigs default to JieLi (USE_OPEN_TOOLCHAIN unset means 0, build/<defconfig>/);
emulator defconfigs override to open (USE_OPEN_TOOLCHAIN=1, build/<defconfig>-open/).
USE_OPEN_TOOLCHAIN=1 in the shell forces open for any defconfig.
Python follows the same rule through tools/build/fm1_paths.py (see fm1_paths_test.py).
"""
import importlib.util
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TASKFILE = (ROOT / 'Taskfile.yml').read_text()
CONFIGS = (ROOT / 'configs' / 'Taskfile.yml').read_text()


def _load(name, path):
    sys.path.insert(0, str(ROOT / 'tests'))
    try:
        spec = importlib.util.spec_from_file_location(name, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod
    finally:
        sys.path.pop(0)


def test_download_task_pins_a_version():
    assert re.search(r'^  toolchain:open:download:\n(?:    .*\n|\n)*?    desc: ', TASKFILE, re.M)
    m = re.search(r'^  OPEN_TOOLCHAIN_VERSION: (pi32v2-\d+\.\d+\.\d+-\d+)$', TASKFILE, re.M)
    assert m, 'OPEN_TOOLCHAIN_VERSION must be pinned in one var'
    # the download task is idempotent and checks the sha256
    assert 'status:' in TASKFILE.split('  toolchain:open:download:')[1].split('\n  build:dry-run:')[0]
    assert 'SHA256SUMS' in TASKFILE


def test_build_live_installs_open_toolchain_only_through_the_guarded_task():
    ensure = TASKFILE.split('  toolchain:open:ensure:')[1].split('\n  toolchain:open:download:')[0]
    assert 'test "$USE_OPEN_TOOLCHAIN" != 1' in ensure
    assert 'toolchain:open:ensure' in TASKFILE.split('  build:live:')[1].split('deps:')[1].split('\n')[0]


@pytest.mark.parametrize('text', [TASKFILE, CONFIGS], ids=['root', 'configs'])
def test_suffix_defined_from_the_variable(text):
    m = re.search(r"^  USE_OPEN: '(.*)'$", text, re.M)
    assert m, 'USE_OPEN must be defined'
    expr = m.group(1)
    assert 'eq (env "USE_OPEN_TOOLCHAIN") "1"' in expr, expr
    assert """TC_SUFFIX: '{{if eq .USE_OPEN "1"}}-open{{end}}'""" in text
    assert """USE_OPEN_TOOLCHAIN: '{{.USE_OPEN}}'""" in text.split('\nvars:')[0].split('\nenv:')[1]


def test_build_refuses_a_value_the_python_side_would_read_differently():
    live = TASKFILE.split('  build:live:')[1].split('\n  toolchain:open:ensure:')[0]
    assert """case "$USE_OPEN_TOOLCHAIN" in 0|1) ;; *) exit 1;; esac""" in live
    # and a value passed as a task argument, which never reaches build.py's environment
    assert """test "{{.USE_OPEN_TOOLCHAIN | default .USE_OPEN}}" = "$USE_OPEN_TOOLCHAIN\"""" in live


def test_flash_opt_in_works_as_environment_variable_or_task_argument():
    assert 'OPEN_TOOLCHAIN_FLASH_OK' in CONFIGS.split('  _flash:')[1].split('  _test:api:')[0]
    assert 'OPEN_TOOLCHAIN_FLASH_OK' in TASKFILE.split('  flash:')[1].split('  flash:midi:')[0]


def test_no_build_path_ignores_the_suffix():
    # every build/<defconfig>/modd.fwsc|map in the Taskfiles follows TC_SUFFIX
    for text in (TASKFILE, CONFIGS):
        for line in text.splitlines():
            if re.search(r"build/\{\{\.DEFCONFIG\}\}/", line):
                pytest.fail(f'path without TC_SUFFIX: {line.strip()}')


def test_flash_requires_explicit_opt_in():
    assert 'OPEN_TOOLCHAIN_FLASH_OK' in CONFIGS.split('  _flash:')[1].split('  _test:api:')[0]
    flash_dispatcher = TASKFILE.split('  flash:')[1].split('  flash:midi:')[0]
    assert 'OPEN_TOOLCHAIN_FLASH_OK' in flash_dispatcher


@pytest.mark.parametrize('loader', ['api_fixtures', 'build_fixtures'])
def test_fixture_package_path_follows_the_variable(loader, monkeypatch):
    for k in ('MODD_API_FWSC', 'MODD_FWSC', 'MODD_API_DEFCONFIG', 'MODD_DEFCONFIG'):
        monkeypatch.delenv(k, raising=False)
    mod = _load(f'_{loader}_otc', ROOT / 'tests' / f'{loader}.py')
    fn = mod.api_fwsc if loader == 'api_fixtures' else mod.fwsc_path
    # JieLi is the default; only an explicit 1 selects open
    for value in (None, '', '0', 'yes'):
        if value is None:
            monkeypatch.delenv('USE_OPEN_TOOLCHAIN', raising=False)
        else:
            monkeypatch.setenv('USE_OPEN_TOOLCHAIN', value)
        assert fn('modd_emu') == ROOT / 'build' / 'modd_emu' / 'modd.fwsc'
    monkeypatch.setenv('USE_OPEN_TOOLCHAIN', '1')
    assert fn('modd_emu') == ROOT / 'build' / 'modd_emu-open' / 'modd.fwsc'


def test_api_fwsc_override_keeps_priority(monkeypatch):
    sys.path.insert(0, str(ROOT / 'tests'))
    try:
        import api_fixtures as mod
    finally:
        sys.path.pop(0)
    monkeypatch.setenv('USE_OPEN_TOOLCHAIN', '1')
    monkeypatch.setenv('MODD_API_DEFCONFIG', 'modd_emu')
    monkeypatch.setenv('MODD_API_FWSC', '/tmp/x.fwsc')
    assert mod.api_fwsc('modd_emu') == Path('/tmp/x.fwsc')
