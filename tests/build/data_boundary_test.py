# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Boundary test: UI and RPC files must access upstream data through adapters.

Files under firmware/src/ui/ and firmware/src/rpc/ should not use track_t
fields, song.g[], trk[], P_*/G_* ids or ->p[]/->eng_req directly.  The
allowlist below names the files that still do; it must only shrink."""

import re
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

# Files that still access upstream structs directly.  Each removal is a step
# toward the adapter boundary; additions are not allowed.
ALLOWLIST = sorted([
])

PATTERNS = [
    r'trk\[',
    r'song\.g\[',
    r'song\.sel',
    r'->p\[',
    r'->eng_req',
]
COMBINED = re.compile('|'.join(PATTERNS))


def _violating_files():
    """Return sorted list of files that match direct-access patterns."""
    dirs = [str(ROOT / "firmware/src/ui"), str(ROOT / "firmware/src/rpc")]
    result = subprocess.run(
        ["grep", "-rln", "-E", '|'.join(PATTERNS)] + dirs,
        capture_output=True, text=True, cwd=str(ROOT),
    )
    files = set()
    for line in result.stdout.strip().splitlines():
        p = Path(line)
        try:
            rel = str(p.relative_to(ROOT))
        except ValueError:
            rel = line
        files.add(rel)
    return sorted(files)


def test_no_new_direct_access():
    """No file outside the allowlist accesses upstream structs directly."""
    actual = _violating_files()
    new = sorted(set(actual) - set(ALLOWLIST))
    assert not new, (
        f"New files accessing upstream structs directly (add to adapter or allowlist):\n"
        + "\n".join(f"  {f}" for f in new)
    )


def test_allowlist_only_shrinks():
    """Every file in the allowlist still has violations (no stale entries)."""
    actual = set(_violating_files())
    stale = sorted(set(ALLOWLIST) - actual)
    assert not stale, (
        f"Files no longer violate the boundary — remove from ALLOWLIST:\n"
        + "\n".join(f"  {f}" for f in stale)
    )
