# SPDX-License-Identifier: GPL-3.0-only
"""Enforce the porting rule: every file derived from an upstream is listed.

Rule (docs/PORTING.md):
  "Never copy or rewrite upstream code unless duplication / forking is approved by the user."
  "The work of porting is writing the adapter, and nothing else."
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ALLOWLIST = ROOT / "docs" / "UPSTREAM_ORIGINS.md"
SCAN_DIRS = ("firmware/src", "firmware/adapters", "firmware/hal")
CLASSES = {"WRAPPER", "HEADER-DERIVED", "FORK", "PATCHED"}
PATCH_STATUS = re.compile(r"^approved: .+, .+, .+$")
FORK_NOTE = ("COPIED", "REDUCED", "REWRITTEN")
FORK_STATUS = re.compile(r"^(approved: .+, .+, .+|not approved: to be replaced by an include|pre-rule \(before 2026-11-23\): not reviewed)$")

HELP = (
    'Rule: "Never copy or rewrite upstream code unless duplication / forking is approved by the user." '
    '"The work of porting is writing the adapter, and nothing else." '
    "See docs/PORTING.md#the-rule-upstream-code-through-an-adapter-into-modd. "
    "Include the upstream file from a thin wrapper, or ask the user to approve a fork by file name "
    "and list it in docs/UPSTREAM_ORIGINS.md."
)


def _origin_files():
    found = set()
    for d in SCAN_DIRS:
        for p in (ROOT / d).rglob("*"):
            if not p.is_file():
                continue
            try:
                head = p.read_text(errors="replace").splitlines()[:40]
            except OSError:
                continue
            if any("Origin:" in line for line in head):
                found.add(p.relative_to(ROOT).as_posix())
    return found


def _rows():
    rows = {}
    for line in ALLOWLIST.read_text().splitlines():
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 5:
            continue
        path = cells[0].strip("`")
        if path in ("path", "Class") or set(path) <= set("-: "):
            continue
        if not path.startswith(("firmware/", "patches/")):
            continue
        rows[path] = {"class": cells[1], "upstream": cells[2], "status": cells[3], "note": cells[4]}
    return rows


def test_every_origin_file_is_listed():
    rows = _rows()
    missing = sorted(_origin_files() - rows.keys())
    assert not missing, (
        "Files with an Origin: line but no row in docs/UPSTREAM_ORIGINS.md:\n  "
        + "\n  ".join(missing) + "\n" + HELP
    )


def test_no_stale_rows():
    stale = sorted(p for p in _rows() if not (ROOT / p).is_file())
    assert not stale, (
        "Rows in docs/UPSTREAM_ORIGINS.md for files that no longer exist:\n  "
        + "\n  ".join(stale) + "\nRemove the rows."
    )


def test_rows_are_well_formed():
    bad = []
    for path, r in _rows().items():
        c = r["class"]
        if c not in CLASSES:
            bad.append(f"{path}: unknown class {c!r}")
        elif c == "WRAPPER":
            f = ROOT / path
            if f.is_file() and '#include "upstream/' not in f.read_text(errors="replace"):
                bad.append(f'{path}: WRAPPER without an #include "upstream/ line')
        elif c == "PATCHED":
            if not path.startswith("patches/") or not path.endswith(".patch"):
                bad.append(f"{path}: a PATCHED row names a patches/<upstream>/NNNN-*.patch file")
            if not PATCH_STATUS.match(r["status"]):
                bad.append(f"{path}: PATCHED status {r['status']!r} must be 'approved: <who>, <date>, <reason>'")
        elif c == "FORK":
            if not r["note"].startswith(FORK_NOTE):
                bad.append(f"{path}: FORK note must start with one of {FORK_NOTE}")
            if not FORK_STATUS.match(r["status"]):
                bad.append(
                    f"{path}: FORK status {r['status']!r} must be "
                    "'approved: <who>, <date>, <reason>', 'not approved: to be replaced by an include' "
                    "or 'pre-rule (before 2026-11-23): not reviewed'"
                )
    assert not bad, "\n".join(bad) + "\n" + HELP


def test_upstream_includes_resolve_to_upstream_tree():
    vendir = (ROOT / "vendir.yml").read_text()
    names = set(re.findall(r"path:\s*firmware/upstream/([\w.-]+)", vendir))
    pat = re.compile(r'^\s*#\s*include\s+"(upstream/[^"]+)"', re.M)
    bad = []
    for p in (ROOT / "firmware" / "src").rglob("*"):
        if not p.is_file() or p.suffix not in (".c", ".h", ".S"):
            continue
        for inc in pat.findall(p.read_text(errors="replace")):
            parts = inc.split("/")
            if len(parts) < 3 or parts[1] not in names or not parts[-1].endswith((".c", ".h")):
                bad.append(f"{p.relative_to(ROOT)}: {inc}")
    assert not bad, (
        "#include \"upstream/...\" must name a tree under firmware/upstream listed in vendir.yml:\n  "
        + "\n  ".join(bad)
    )


def _patch_files():
    return sorted((ROOT / "patches").glob("*/*.patch"))


def test_every_patch_is_approved_and_listed():
    """patches/<upstream>/*.patch: Approved-by and Forwarded in the header, a PATCHED row in UPSTREAM_ORIGINS.md."""
    rows = _rows()
    bad = []
    for f in _patch_files():
        rel = f.relative_to(ROOT).as_posix()
        header = {}
        for line in f.read_text().splitlines():
            if line.startswith(("---", "diff --git")):
                break
            m = re.match(r"^([A-Za-z][A-Za-z0-9-]*):\s*(.*)$", line)
            if m:
                header[m.group(1)] = m.group(2).strip()
        for field in ("Approved-by", "Forwarded"):
            if not header.get(field):
                bad.append(f"{rel}: header has no {field}")
        if rows.get(rel, {}).get("class") != "PATCHED":
            bad.append(f"{rel}: no PATCHED row in docs/UPSTREAM_ORIGINS.md")
    assert not bad, "\n".join(bad) + "\nA patch needs the user's approval by file name (patches/README.md). " + HELP


def test_patch_rows_name_existing_patches():
    patched = {p for p, r in _rows().items() if r["class"] == "PATCHED"}
    assert patched == {f.relative_to(ROOT).as_posix() for f in _patch_files()}
