#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Upstream patch series: patches/<upstream>/NNNN-*.patch applied to a copy, never to the vendored tree.

    patches.py apply --out DIR [--upstream NAME ...]
        For each upstream with patches (all of them, or the named ones): copy the files its patches
        touch from firmware/upstream/<name>/ to DIR/upstream/<name>/ (same relative path), then
        `git apply` the series on the copy, in file-name order. Strict: no fuzz, no partial apply.
        Always starts from the pristine files and an empty DIR. Writes DIR/stamp.json.
    patches.py verify --out DIR
        Exit 1 when DIR was not produced from the current patches, pins and pristine sources.
    patches.py check [--upstream NAME ...]
        Every patch parses, has its header fields, names the commit vendir.lock.yml pins, and
        applies cleanly to the vendored tree (in a temp dir). Exit 1 on any failure.

The rules for writing a patch are in patches/README.md. build.py puts DIR first on the include path,
so `#include "upstream/felucca/firmware/src/fx.c"` finds the patched copy; the files a patch does
not touch are found in the vendored tree (include_dirs gives the fallback directories).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
PATCHES = ROOT / 'patches'
UPSTREAM = ROOT / 'firmware' / 'upstream'
LOCK = ROOT / 'vendir.lock.yml'

REQUIRED = ('Description', 'Approved-by', 'Forwarded', 'Applies-to')
NAME_RE = re.compile(r'^\d{4}-[A-Za-z0-9._+-]+\.patch$')
SHA_RE = re.compile(r'\(([0-9a-f]{40})\)')
FIELD_RE = re.compile(r'^([A-Za-z][A-Za-z0-9-]*):[ \t]*(.*)$')
STAMP = 'stamp.json'


class PatchError(Exception):
    pass


class Patch:
    def __init__(self, path, upstream, header, files):
        self.path = Path(path)
        self.upstream = upstream
        self.header = header
        self.files = files

    @property
    def name(self):
        return self.path.name

    @property
    def label(self):
        return f'patches/{self.upstream}/{self.name}'


def parse_header(text):
    """The DEP-3 style header: `Key: value` lines before the first `---` or `diff --git` line;
    a line starting with a space continues the previous field. Returns {key: value}."""
    fields, last = {}, None
    for line in text.splitlines():
        if line.startswith('---') or line.startswith('diff --git'):
            break
        if line[:1] in (' ', '\t') and last:
            fields[last] += '\n' + line.strip()
            continue
        m = FIELD_RE.match(line)
        if m:
            last = m.group(1)
            fields[last] = m.group(2).strip()
        else:
            last = None
    return fields


def parse_files(text, label='patch'):
    """The files a git-style diff modifies (paths relative to the upstream root). Only changes to
    existing files are allowed: a patch adds hooks to upstream files, it does not add, delete or rename."""
    files = []
    for line in text.splitlines():
        m = re.match(r'^diff --git a/(\S+) b/(\S+)$', line)
        if m:
            if m.group(1) != m.group(2):
                raise PatchError(f'{label}: rename {m.group(1)} -> {m.group(2)} (a patch only edits files in place)')
            rel = m.group(1)
            if rel.startswith('/') or '..' in Path(rel).parts:
                raise PatchError(f'{label}: path {rel} must be relative to the upstream root')
            if rel not in files:
                files.append(rel)
        elif re.match(r'^(new file mode|deleted file mode|rename |copy |similarity )', line):
            raise PatchError(f'{label}: "{line.split()[0]} ..." (a patch only edits existing files)')
    if not files:
        raise PatchError(f'{label}: no "diff --git a/<path> b/<path>" line (make it with git diff --no-index)')
    return files


def parse_patch(path, upstream=None):
    path = Path(path)
    upstream = upstream or path.parent.name
    label = f'patches/{upstream}/{path.name}'
    if not NAME_RE.match(path.name):
        raise PatchError(f'{label}: file name must be NNNN-<what>.patch')
    text = path.read_text(encoding='utf-8')
    header = parse_header(text)
    missing = [k for k in REQUIRED if not header.get(k)]
    if missing:
        raise PatchError(f'{label}: header lacks {", ".join(missing)} (see patches/README.md)')
    return Patch(path, upstream, header, parse_files(text, label))


def series(patches_dir=PATCHES, only=None):
    """{upstream: [Patch, ...]} in name order, for each patches/<upstream>/ directory."""
    out = {}
    patches_dir = Path(patches_dir)
    if not patches_dir.is_dir():
        return out
    for d in sorted(p for p in patches_dir.iterdir() if p.is_dir()):
        if only and d.name not in only:
            continue
        ps = [parse_patch(p, d.name) for p in sorted(d.glob('*.patch'))]
        if ps:
            out[d.name] = ps
    return out


def locked_sha(upstream, lock=LOCK):
    """The commit vendir.lock.yml records for firmware/upstream/<upstream>."""
    data = yaml.safe_load(Path(lock).read_text()) or {}
    want = f'firmware/upstream/{upstream}'
    for d in data.get('directories', []):
        if d.get('path') == want:
            for c in d.get('contents', []):
                sha = (c.get('git') or {}).get('sha')
                if sha:
                    return sha
    raise PatchError(f'{LOCK.name} has no pinned commit for {want}')


def _sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def expected_stamp(sr, upstream_dir=UPSTREAM, lock=LOCK):
    """What DIR/stamp.json must say for this series: the pin, every patch's hash, every pristine
    source's hash and the directories the build falls back to for the files a patch did not touch."""
    stamp = {}
    for name, ps in sr.items():
        src = Path(upstream_dir) / name
        files = sorted({f for p in ps for f in p.files})
        for f in files:
            if not (src / f).is_file():
                raise PatchError(f'{(src / f)} is missing: run "task vendor:ensure"')
        stamp[name] = {
            'sha': locked_sha(name, lock),
            'patches': {p.name: _sha256(p.path) for p in ps},
            'sources': {f: _sha256(src / f) for f in files},
            'dirs': sorted({str(Path(f).parent) for f in files}),
        }
    return stamp


def _git_apply(cwd, patch):
    # Outside index mode, strict, no fuzz. The ceiling stops git from finding the modd repository
    # above DIR, so nothing but the copy is looked at or changed.
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(Path(cwd).resolve().parent.parent))
    env.pop('GIT_DIR', None)
    env.pop('GIT_WORK_TREE', None)
    r = subprocess.run(['git', 'apply', '--verbose', '--whitespace=nowarn', str(patch.path.resolve())],
                       cwd=cwd, env=env, capture_output=True, text=True)
    if r.returncode:
        raise PatchError(f'{patch.label} does not apply to the vendored tree '
                         f'(git apply exit {r.returncode}):\n{r.stderr.strip()}\n'
                         'Refresh the patch against the pinned commit (patches/README.md).')


def apply_series(out, only=None, patches_dir=PATCHES, upstream_dir=UPSTREAM, lock=LOCK):
    """Rebuild `out` from the pristine files and the series; returns the stamp (also written)."""
    out = Path(out).resolve()
    up = Path(upstream_dir).resolve()
    if out == ROOT or out in up.parents or up in (out, *out.parents):   # never delete the vendored tree
        raise PatchError(f'refusing to use {out} as the output directory')
    sr = series(patches_dir, only)
    stamp = expected_stamp(sr, upstream_dir, lock)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for name, ps in sr.items():
        root = out / 'upstream' / name
        copied = set()
        for p in ps:
            for f in p.files:
                if f not in copied:
                    (root / f).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(up / name / f, root / f)
                    copied.add(f)
            _git_apply(root, p)
    (out / STAMP).write_text(json.dumps(stamp, indent=1, sort_keys=True) + '\n')
    return stamp


def verify(out, patches_dir=PATCHES, upstream_dir=UPSTREAM, lock=LOCK):
    """Problems (a list of strings) with the patched tree at `out`; empty when it is current.
    Series with no patches need no tree."""
    sr = series(patches_dir)
    if not sr:
        return []
    stamp_file = Path(out) / STAMP
    if not stamp_file.is_file():
        return [f'{stamp_file} is missing: the patched upstream tree was not made for this build']
    try:
        want = expected_stamp(sr, upstream_dir, lock)
    except PatchError as e:
        return [str(e)]
    have = json.loads(stamp_file.read_text())
    return [f'{name}: the patched tree is stale (a patch, the pin or a pristine file changed since it was made)'
            for name in sorted(want) if have.get(name) != want[name]]


def include_dirs(out):
    """The directories under the vendored tree to search after `out`: upstream files include their
    neighbours by bare name (fx.c: #include "perform.c"), and the copy has only the patched files."""
    stamp = json.loads((Path(out) / STAMP).read_text())
    return [f'firmware/upstream/{n}/{d}' for n, s in sorted(stamp.items()) for d in s['dirs']]


def check(only=None, patches_dir=PATCHES, upstream_dir=UPSTREAM, lock=LOCK):
    """Returns a list of failure messages for every patch; empty when all is well."""
    problems = []
    pdir = Path(patches_dir)
    if pdir.is_dir():
        for d in sorted(p for p in pdir.iterdir() if p.is_dir()):
            for f in sorted(d.iterdir()):
                if f.suffix != '.patch' and f.name != 'README.md':
                    problems.append(f'{d.name}/{f.name}: not a *.patch file')
    parsed = {}
    for d in sorted(p for p in pdir.iterdir() if p.is_dir()) if pdir.is_dir() else []:
        if only and d.name not in only:
            continue
        for f in sorted(d.glob('*.patch')):
            try:
                p = parse_patch(f, d.name)
            except PatchError as e:
                problems.append(str(e))
                continue
            parsed.setdefault(d.name, []).append(p)
            try:
                sha = locked_sha(d.name, lock)
            except PatchError as e:
                problems.append(f'{p.label}: {e}')
                continue
            m = SHA_RE.search(p.header['Applies-to'])
            if not m:
                problems.append(f'{p.label}: Applies-to must end in the full commit, "(<40 hex>)"')
            elif m.group(1) != sha:
                problems.append(f'{p.label}: written for {m.group(1)}, vendir.lock.yml now pins {sha}. '
                                'Re-read the patch against the new commit, refresh it (patches/README.md) '
                                'and update Applies-to.')
    for name, ps in parsed.items():
        if not (Path(upstream_dir) / name).is_dir():
            problems.append(f'firmware/upstream/{name} is not fetched: cannot confirm the patches apply. '
                            'Run "task vendor:ensure" first.')
            continue
        with tempfile.TemporaryDirectory(prefix='patches-check-') as tmp:
            try:
                apply_series(Path(tmp) / 'patched', {name}, pdir, upstream_dir, lock)
            except PatchError as e:
                problems.append(str(e))
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('apply')
    a.add_argument('--out', required=True)
    a.add_argument('--upstream', action='append')
    v = sub.add_parser('verify')
    v.add_argument('--out', required=True)
    c = sub.add_parser('check')
    c.add_argument('--upstream', action='append')
    args = ap.parse_args(argv)
    try:
        if args.cmd == 'apply':
            stamp = apply_series(args.out, set(args.upstream or ()) or None)
            n = sum(len(s['patches']) for s in stamp.values())
            print(f'patches: {n} patch(es) for {", ".join(sorted(stamp)) or "no upstream"} -> {args.out}')
        elif args.cmd == 'verify':
            probs = verify(args.out)
            if probs:
                print('\n'.join(probs) + '\nRun "task vendor:patch".', file=sys.stderr)
                return 1
        else:
            probs = check(set(args.upstream or ()) or None)
            if probs:
                print('patches check failed:\n  ' + '\n  '.join(p.replace('\n', '\n    ') for p in probs),
                      file=sys.stderr)
                return 1
            n = sum(len(ps) for ps in series(PATCHES, set(args.upstream or ()) or None).values())
            print(f'patches: {n} patch(es) parse and apply to the locked commits')
    except PatchError as e:
        print(f'patches: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
