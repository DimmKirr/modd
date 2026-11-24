#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""One pytest session writes into one folder: tests/results/<RUN_STAMP>-<RUN_NAME>/ holds timings.tsv and a
subfolder per test module (tests/io_log.py, tests/conftest.py)."""
import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import conftest
import io_log


def test_a_modules_folder_and_the_timings_are_in_the_sessions_folder():
    run = io_log.run_dir()
    assert run.parent == io_log.RESULTS
    assert run.name == f'{io_log.RUN_STAMP}-{io_log.RUN_NAME}'
    assert io_log.results_dir('some_test') == run / 'some_test'
    assert io_log.debug_log_path('tests/build/x_test.py::test_y') == run / 'x_test' / 'debug.log'
    assert conftest._timings_tsv() == run / 'timings.tsv'


def _config(args, tier='all'):
    return SimpleNamespace(args=args, getoption=lambda name: tier)


def test_the_session_is_named_by_its_one_file_then_its_tier():
    assert conftest._run_name(_config(['tests/'], 'unit')) == 'unit'
    assert conftest._run_name(_config(['tests/'], 'api')) == 'api'
    assert conftest._run_name(_config(['tests/'])) == 'tests'
    assert conftest._run_name(_config(['tests/audio/api_audio_test.py::TestX::test_y'], 'api')) == 'api_audio_test'
    assert conftest._run_name(_config(['tests/a_test.py', 'tests/b_test.py'], 'api')) == 'api'


def test_the_name_does_not_change_once_a_file_is_open():
    before = io_log.RUN_NAME
    assert io_log._files, 'this session has written to a debug.log by now'
    io_log.set_run_name('other')
    assert io_log.RUN_NAME == before
