# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""The test trace: everything a test does, timestamped, in its module's results folder.

    tests/results/<RUN_STAMP>-<test module>/debug.log

One pytest session has one RUN_STAMP (UTC, 2026-11-20T070401Z); every results folder of the
session uses it (api_fixtures, build_fixtures), so a module's screenshots, fm1-live logs and
debug.log sit together. tests/conftest.py turns the trace on and writes each test's life
(start, setup, call, teardown, end with outcome and seconds) and every subprocess.run; the API
client (api_fixtures.FirmwareApi) adds each HTTP request with the wait for the emulator's
connection budget; Fm1Live adds every line fm1-live prints; tests add their own events
(log()). The per-test totals of counted IO go into the timing lines of tests/conftest.py.

Columns (tab-separated): time_utc  test  kind  seconds  throttle_wait  detail
"""
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

RUN_STAMP = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')
RESULTS = Path(__file__).resolve().parent / 'results'
HEADER = 'time_utc\ttest\tkind\tseconds\tthrottle_wait\tdetail\n'

enabled = False             # tests/conftest.py turns it on for a pytest session
current = '-'               # the running test's node id
totals = {}                 # node id -> [ios, io seconds, throttle seconds]
_files = {}                 # module -> open debug.log
_lock = threading.Lock()    # fm1-live's output arrives on a reader thread


def module_of(nodeid):
    """tests/usb/x_test.py::Class::test -> x_test ('-' outside a test: session)."""
    if not nodeid or nodeid == '-':
        return 'session'
    return Path(nodeid.split('::', 1)[0]).stem


def results_dir(module):
    return RESULTS / f'{RUN_STAMP}-{module}'


def debug_log_path(nodeid=None):
    return results_dir(module_of(nodeid or current)) / 'debug.log'


def _stamp(t):
    return datetime.fromtimestamp(t, timezone.utc).strftime('%H:%M:%S.%f')[:-3]


def log(kind, detail, secs=None, wait=0.0, count=True):
    """One event: kind (test, exec, http, throttle, usb, fm1-live, ...), what it was, how long
    it took, how long it waited first. count=False: context, not IO (not in the totals)."""
    test = current
    if count:
        tot = totals.setdefault(test, [0, 0.0, 0.0])
        tot[0] += 1
        tot[1] += secs or 0.0
        tot[2] += wait or 0.0
    if not enabled:
        return
    line = (f'{_stamp(time.time())}\t{test}\t{kind}\t{"" if secs is None else f"{secs:.3f}"}\t'
            f'{f"{wait:.2f}" if wait else ""}\t{detail}\n')
    module = module_of(test)
    with _lock:
        f = _files.get(module)
        if f is None:
            path = results_dir(module) / 'debug.log'
            try:
                path.parent.mkdir(parents=True, exist_ok=True)
                new = not path.exists()
                f = _files[module] = path.open('a', buffering=1)     # line-buffered: tail -f works
                if new:
                    f.write(HEADER)
            except OSError:
                return
        f.write(line)


def close():
    with _lock:
        for f in _files.values():
            f.close()
        _files.clear()
