# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Wire the project-root tests/ dir onto sys.path so e2e_fixtures and
fm1_controls are importable from theme-colocated tests."""
import sys
from pathlib import Path

_TESTS_ROOT = Path(__file__).resolve().parents[6] / 'tests'
if str(_TESTS_ROOT) not in sys.path:
    sys.path.insert(0, str(_TESTS_ROOT))
