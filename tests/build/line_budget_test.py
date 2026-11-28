# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Line-budget test: guards against code creep in modd's owned code.

Counts lines in key directories and asserts they stay below ceilings.
Also checks that adapter headers with CONFIG_ branching have at least
two distinct branches (proving multi-implementation contracts)."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ADAPTERS_DIR = ROOT / "firmware" / "contracts"
SRC_DIR = ROOT / "firmware" / "src"

BUDGET_ADAPTERS = 3500
BUDGET_MODD_WRAPPERS = 700
BUDGET_SRC_TOTAL = 45000


def _count_lines(directory: Path, pattern: str) -> int:
    total = 0
    for p in sorted(directory.rglob(pattern)):
        if p.is_file():
            total += sum(1 for _ in p.open(encoding="utf-8", errors="replace"))
    return total


def _count_lines_multi(directory: Path, patterns: list[str]) -> int:
    total = 0
    for pat in patterns:
        total += _count_lines(directory, pat)
    return total


def test_adapters_line_budget():
    """firmware/contracts/ stays below the ceiling."""
    actual = _count_lines_multi(ADAPTERS_DIR, ["*.h", "*.c"])
    assert actual <= BUDGET_ADAPTERS, (
        f"firmware/contracts/ has {actual} lines (budget {BUDGET_ADAPTERS})"
    )


def test_modd_wrappers_line_budget():
    """firmware/src/*_modd.c wrappers stay below the ceiling."""
    actual = _count_lines(SRC_DIR, "*_modd.c")
    assert actual <= BUDGET_MODD_WRAPPERS, (
        f"firmware/src/*_modd.c has {actual} lines (budget {BUDGET_MODD_WRAPPERS})"
    )


def test_src_total_line_budget():
    """firmware/src/ total stays below the ceiling."""
    actual = _count_lines_multi(SRC_DIR, ["*.c", "*.h"])
    assert actual <= BUDGET_SRC_TOTAL, (
        f"firmware/src/ has {actual} lines (budget {BUDGET_SRC_TOTAL})"
    )


CONFIG_RE = re.compile(r"#\s*(?:if|elif|ifdef|ifndef)\b.*\bCONFIG_(\w+)")
ELIF_RE = re.compile(r"#\s*elif\b.*\bCONFIG_\w+")


def test_adapter_switch_headers_have_two_branches():
    """Adapter headers that use #elif CONFIG_* must have >=2 distinct symbols.

    This catches a contract that was written as a multi-upstream switch
    (using #elif to choose between implementations) but has only one
    branch wired.  Feature-gate headers (#ifdef with no #elif) are fine
    — they enable/disable a single feature, not switch implementations."""
    problems = []
    for p in sorted(ADAPTERS_DIR.glob("*.h")):
        text = p.read_text(encoding="utf-8", errors="replace")
        if not ELIF_RE.search(text):
            continue
        symbols = set(CONFIG_RE.findall(text))
        if len(symbols) < 2:
            problems.append(f"  {p.name}: has #elif but only one CONFIG_ symbol")
    assert not problems, (
        "Adapter headers with #elif CONFIG_ should have >=2 distinct symbols:\n"
        + "\n".join(problems)
    )
