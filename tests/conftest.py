# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""pytest hooks for every test: a per-test trace (tests/io_log.py), subprocess.run tracing,
slow-test lines, the "test times" summary and tests/results/<stamp>-timings.tsv."""
import fnmatch
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

import io_log

# The two tiers. Unit: host only. API: boots fm1-live and drives the firmware over HTTP.
# The rule lives here and nowhere else: `--tier unit|api|all` (default all) selects by it, and
# tests/build/taskfile_tiers_test.py checks the Taskfiles and the test files against it.
_TESTS_DIR = Path(__file__).resolve().parent


def is_api_test(path):
    """True for a file named api_*_test.py or one in a directory named api (below tests/)."""
    p = Path(path).resolve()
    try:
        parts = p.relative_to(_TESTS_DIR).parts
    except ValueError:
        parts = p.parts
    return fnmatch.fnmatch(p.name, 'api_*_test.py') or 'api' in parts[:-1]


def pytest_addoption(parser):
    parser.addoption('--tier', choices=('unit', 'api', 'all'), default='all',
                     help='run only the unit tier, only the API tier (see is_api_test), or both')


def pytest_ignore_collect(collection_path, config):
    # Skipped before import: unit runs must not import the API modules (ui-test group packages).
    tier = config.getoption('--tier')
    if tier == 'all' or not collection_path.name.endswith('_test.py'):
        return None
    if is_api_test(collection_path) != (tier == 'api'):
        return True
    return None


# Tracing and timing, for every test (tests/io_log.py):
#   tests/results/<RUN_STAMP>-<module>/debug.log   every event of the module's tests, timestamped:
#       each test's start, setup / call / teardown with outcome and seconds, end; every
#       subprocess.run (command, exit code, seconds); the API client's HTTP requests; fm1-live's
#       output; the tests' own events
#   a line per test of 0.5 s or more: start time (UTC), seconds (setup, call and teardown
#       together, so a test class's emulator boot counts on its first test), its counted IO
#   the session's end: every test of 0.5 s or more, slowest first, also written to
#       tests/results/<RUN_STAMP>-timings.tsv
_TIMINGS_TSV = io_log.RESULTS / f'{io_log.RUN_STAMP}-timings.tsv'
_SHOW_S = 0.5
_began = {}                                     # nodeid -> start time
_outcome = {}                                   # nodeid -> passed / failed / skipped / error
_timings = []                                   # (seconds, start, nodeid, outcome, ios, io s, wait s)
_config = None
_run = subprocess.run


def _hms(t):
    return datetime.fromtimestamp(t, timezone.utc).strftime('%H:%M:%S')


def _cmd(args):
    if isinstance(args, (list, tuple)):
        return ' '.join(str(a) for a in args)
    return str(args)


def _traced_run(*args, **kwargs):
    """subprocess.run, with the command, its exit code and its seconds in the trace."""
    cmd = _cmd(args[0] if args else kwargs.get('args'))
    t0 = time.monotonic()
    try:
        r = _run(*args, **kwargs)
    except Exception as e:
        io_log.log('exec', f'{cmd} -> {type(e).__name__}: {e}', time.monotonic() - t0)
        raise
    io_log.log('exec', f'{cmd} -> exit {r.returncode}', time.monotonic() - t0)
    return r


def pytest_configure(config):
    global _config
    _config = config                            # the terminal reporter registers after this
    io_log.enabled = True
    subprocess.run = _traced_run


def pytest_unconfigure(config):
    subprocess.run = _run
    io_log.close()


def pytest_runtest_logstart(nodeid, location):
    _began[nodeid] = time.time()
    io_log.current = nodeid
    io_log.log('test', 'start', count=False)


def pytest_runtest_logreport(report):
    # the call's outcome; a setup that failed or skipped never reaches the call
    if report.when == 'call' or (report.when == 'setup' and report.outcome != 'passed'):
        _outcome[report.nodeid] = report.outcome
    elif report.when == 'teardown' and report.outcome == 'failed':
        _outcome[report.nodeid] = 'error'
    detail = f'{report.when} {report.outcome}'
    if report.outcome != 'passed' and report.longrepr is not None:
        detail += ': ' + str(report.longrepr).strip().splitlines()[-1][:300]
    io_log.log('test', detail, report.duration, count=False)


def pytest_runtest_logfinish(nodeid, location):
    start = _began.pop(nodeid, None)
    if start is None:
        return
    secs = time.time() - start
    outcome = _outcome.pop(nodeid, 'skipped')
    ios, io_s, wait_s = io_log.totals.get(nodeid, (0, 0.0, 0.0))
    io_log.log('test', f'end {outcome}; {ios} io, {io_s:.1f} s in io, {wait_s:.1f} s throttled', secs,
               count=False)
    io_log.current = '-'
    _timings.append((secs, start, nodeid, outcome, ios, io_s, wait_s))
    try:
        _TIMINGS_TSV.parent.mkdir(parents=True, exist_ok=True)
        new = not _TIMINGS_TSV.exists()
        with _TIMINGS_TSV.open('a') as f:
            if new:
                f.write('start_utc\tseconds\toutcome\tios\tio_seconds\tthrottle_seconds\ttest\n')
            f.write(f'{_hms(start)}\t{secs:.1f}\t{outcome}\t{ios}\t{io_s:.1f}\t{wait_s:.1f}\t{nodeid}\n')
    except OSError:
        pass
    tr = _config.pluginmanager.get_plugin('terminalreporter') if _config else None
    if tr is not None and secs >= _SHOW_S:
        tr.ensure_newline()
        tr.write_line(f'    [{_hms(start)} +{secs:6.1f}s] {_io_text(ios, io_s, wait_s)}{nodeid}')


def _io_text(ios, io_s, wait_s):
    if not ios:
        return ''
    return f'{ios:3d} io {io_s:5.1f}s, throttled {wait_s:5.1f}s  '


def pytest_terminal_summary(terminalreporter):
    slow = sorted((t for t in _timings if t[0] >= _SHOW_S), reverse=True)
    if not slow:
        return
    total = sum(t[0] for t in _timings)
    terminalreporter.section(f'test times (>= {_SHOW_S} s; {total:.0f} s in all; '
                             f'tests/results/{_TIMINGS_TSV.name}; traces: tests/results/'
                             f'{io_log.RUN_STAMP}-<module>/debug.log)')
    for secs, start, nodeid, outcome, ios, io_s, wait_s in slow:
        terminalreporter.write_line(f'{secs:7.1f}s {100 * secs / total:5.1f}%  {_hms(start)}  {outcome:7s} '
                                    f'{_io_text(ios, io_s, wait_s)}{nodeid}')

