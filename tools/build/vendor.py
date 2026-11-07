#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Helpers around vendir.yml, the manifest of vendored upstream trees.

    vendor.py paths      print every vendored directory, one per line
    vendor.py present    exit 1 when a vendored directory is missing or empty

The trees under firmware/upstream/ and firmware/vendor/ are not committed:
`task vendor:ensure` runs `present` as its status check and fetches them with
`vendir sync --locked` when it fails. `task vendor:sync` re-resolves the pins.
"""
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'vendir.yml'
LOCK = ROOT / 'vendir.lock.yml'


def vendored_paths(manifest=MANIFEST):
    """Return the `directories[].path` entries of a vendir manifest."""
    data = yaml.safe_load(Path(manifest).read_text()) or {}
    return [d['path'] for d in data.get('directories', [])]


def missing_paths(paths, root=ROOT):
    """Return the vendored paths that do not exist or contain no files."""
    gone = []
    for rel in paths:
        d = Path(root) / rel
        if not d.is_dir() or not any(p.is_file() for p in d.rglob('*')):
            gone.append(rel)
    return gone


def main(argv):
    if len(argv) != 2 or argv[1] not in ('paths', 'present'):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    paths = vendored_paths()

    if argv[1] == 'paths':
        print('\n'.join(paths))
        return 0

    if argv[1] == 'present':
        gone = missing_paths(paths)
        if gone:
            print('Vendored trees missing or empty:\n  ' + '\n  '.join(gone)
                  + "\nRun 'task vendor:sync'.", file=sys.stderr)
            return 1
        return 0
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
