# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Compile a host C harness under tests/, run it and check that it printed its success marker.

One place for the flags, the sanitizer fallback, the timeout and the cleanup that the
``*_test.py`` wrappers around ``tests/**/*.c`` used to repeat. Defaults: ``-std=c99 -Wall
-Wextra -Werror -O2`` with AddressSanitizer and UBSan. Where the sanitizers do not link
(probed once per session with a trivial program) the harness is built without them.
``cc`` is ``$CC`` or ``cc``; the test is skipped when there is none.
"""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

SANITIZE = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
_sanitizers_link = None


def _cc():
    cc = os.environ.get('CC', 'cc')
    if shutil.which(cc) is None:
        pytest.skip(f'no host C compiler ({cc})')
    return cc


def _can_sanitize(cc, tmp):
    """True when a trivial program builds and runs with the sanitizers (cached)."""
    global _sanitizers_link
    if _sanitizers_link is None:
        src = Path(tmp) / 'probe.c'
        src.write_text('int main(void) { return 0; }\n')
        exe = Path(tmp) / 'probe'
        built = subprocess.run([cc, *SANITIZE, '-o', str(exe), str(src)],
                               capture_output=True, text=True, timeout=60)
        _sanitizers_link = (built.returncode == 0 and
                            subprocess.run([str(exe)], capture_output=True, timeout=60).returncode == 0)
    return _sanitizers_link


def run_c_harness(source, *, includes=(), defines=(), extra_flags=(), args=(),
                  sanitize=True, werror=True, timeout=120, expect='all passed'):
    """Build ``source`` and run it; fail unless it exits 0 and prints ``expect`` (None: skip that check).

    ``sanitize=False`` and ``werror=False`` are opt-outs for a harness whose included firmware
    source trips them; give the reason in a comment at the call. Returns the CompletedProcess of the run.
    """
    cc = _cc()
    with tempfile.TemporaryDirectory(prefix='c_harness_') as tmp:
        exe = str(Path(tmp) / 'harness')
        cmd = [cc, '-std=c99', '-Wall', '-Wextra', '-O2']
        if werror:
            cmd.append('-Werror')
        if sanitize and _can_sanitize(cc, tmp):
            cmd += SANITIZE
        for inc in includes:
            cmd += ['-I', str(inc)]
        for d in defines:
            cmd += ['-D', d]
        cmd += [*extra_flags, '-o', exe, str(source)]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        if built.returncode != 0:
            pytest.fail(f'compilation of {Path(source).name} failed:\n{built.stderr}')
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=timeout)
    assert result.returncode == 0, result.stdout + result.stderr
    if expect is not None:
        assert expect in result.stdout, result.stdout + result.stderr
    return result
