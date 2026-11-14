#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Unit tests for tools/build/vendor.py and the vendir manifest.

Guards the rule that every upstream/ or vendor/ tree the firmware build
reaches into is declared (and therefore pinned) in vendir.yml.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))

import vendor  # noqa: E402

# "upstream/<tree>/..." or "vendor/<tree>/..." as written in #include lines,
# -I flags and comments; leading "../" segments are ignored.
REF = re.compile(r'(?:\.\./)*\b(upstream|vendor)/([A-Za-z0-9_.-]+)/')


def test_manifest_covers_every_tree_the_build_reaches_into():
    declared = {Path(p).relative_to('firmware').as_posix()
                for p in vendor.vendored_paths()}
    src = ROOT / 'firmware' / 'src'
    files = [*src.rglob('*.c'), *src.rglob('*.h'), ROOT / 'tools' / 'build' / 'build.py']
    used = {f'{kind}/{name}'
            for f in files
            for kind, name in REF.findall(f.read_text(errors='replace'))}
    assert used, 'expected the firmware to reference vendored trees'
    assert used <= declared, \
        f'referenced but not pinned in vendir.yml: {sorted(used - declared)}'


def test_declared_trees_are_present_in_checkout():
    assert vendor.missing_paths(vendor.vendored_paths()) == []


def test_missing_paths_flags_absent_and_empty_dirs(tmp_path):
    (tmp_path / 'full').mkdir()
    (tmp_path / 'full' / 'f.c').write_text('')
    (tmp_path / 'empty').mkdir()
    assert vendor.missing_paths(['full', 'empty', 'absent'], root=tmp_path) \
        == ['empty', 'absent']


def test_vendored_paths_parses_manifest(tmp_path):
    m = tmp_path / 'vendir.yml'
    m.write_text('apiVersion: vendir.k14s.io/v1alpha1\nkind: Config\n'
                 'directories:\n- path: a/b\n  contents: []\n'
                 '- path: c\n  contents: []\n')
    assert vendor.vendored_paths(m) == ['a/b', 'c']
