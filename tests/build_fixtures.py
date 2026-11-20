#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Build helpers shared by the API tests and benches: defconfig_name, fwsc_path, genconfig_defconfig."""
import os
from pathlib import Path

import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
import fm1_paths  # noqa: E402


def defconfig_name():
    """Return the active defconfig name (e.g. 'modd', 'modd_emu')."""
    return os.environ.get('MODD_DEFCONFIG', 'modd')


def fwsc_path(defconfig=None):
    """Resolve the firmware path for a given defconfig."""
    if 'MODD_FWSC' in os.environ:
        return Path(os.environ['MODD_FWSC'])
    dc = defconfig or defconfig_name()
    return fm1_paths.fwsc_path(dc)


sys.path.insert(0, str(Path(__file__).resolve().parent))
import io_log  # noqa: E402

RESULTS_DIR = io_log.RESULTS

_run_timestamp = io_log.RUN_STAMP          # one per session: every results folder and debug.log share it


def genconfig_defconfig(defconfig, results_dir):
    """Run genconfig with --outdir into the test results directory.

    Returns the gen_dir path containing .config, config.h, manifest.json.
    """
    gen_dir = results_dir / 'config'
    gen_dir.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'build' / 'genconfig.py'),
         '--defconfig', defconfig, '--outdir', str(gen_dir)],
        capture_output=True, text=True, cwd=str(ROOT), timeout=60)
    if r.returncode != 0:
        raise RuntimeError(f'genconfig failed:\n{r.stderr}')
    return gen_dir

