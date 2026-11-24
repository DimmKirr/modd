# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""tools/build/patches.py: header parsing, apply on a fake upstream, the stamp, check on the real tree."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
import patches  # noqa: E402

SHA = 'a' * 40
SRC = 'line1\nline2\n#define LEN 8u\nline4\n'
DST = 'line1\nline2\n#ifndef LEN\n#define LEN 8u\n#endif\nline4\n'


def _diff(tmp, rel='firmware/src/x.c', src=SRC, dst=DST):
    a, b = tmp / 'a', tmp / 'b'
    for d, text in ((a, src), (b, dst)):
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        (d / rel).write_text(text)
    # the directories are a and b, so git writes a/a/<path>: drop the doubled root
    out = subprocess.run(['git', 'diff', '--no-index', 'a', 'b'], cwd=tmp, capture_output=True, text=True).stdout
    return out.replace('a/a/', 'a/').replace('b/b/', 'b/')


HEADER = ('Description: test\nApproved-by: Someone, 2026-01-01\nForwarded: no\n'
          'Applies-to: fake/fake v1 ({sha})\n---\n')


@pytest.fixture
def fake(tmp_path):
    """A fake upstream `fake` with firmware/src/x.c and one patch, a lock file and a patches dir."""
    up = tmp_path / 'upstream'
    (up / 'fake' / 'firmware' / 'src').mkdir(parents=True)
    (up / 'fake' / 'firmware' / 'src' / 'x.c').write_text(SRC)
    (up / 'fake' / 'firmware' / 'src' / 'y.c').write_text('untouched\n')
    pd = tmp_path / 'patches' / 'fake'
    pd.mkdir(parents=True)
    work = tmp_path / 'work'
    work.mkdir()
    (pd / '0001-guard.patch').write_text(HEADER.format(sha=SHA) + _diff(work))
    lock = tmp_path / 'vendir.lock.yml'
    lock.write_text('apiVersion: vendir.k14s.io/v1alpha1\ndirectories:\n- contents:\n  - git:\n'
                    f'      sha: {SHA}\n    path: .\n  path: firmware/upstream/fake\n')
    return {'root': tmp_path, 'up': up, 'pd': tmp_path / 'patches', 'lock': lock, 'patch': pd / '0001-guard.patch'}


def _apply(fake, out='out'):
    return patches.apply_series(fake['root'] / out, None, fake['pd'], fake['up'], fake['lock'])


def test_header_parse():
    text = ('Description: one\n two\nApproved-by: A, 2026-01-01\nForwarded: no\n'
            'Applies-to: x v1 (%s)\n---\ndiff --git a/f b/f\n' % SHA)
    h = patches.parse_header(text)
    assert h['Description'] == 'one\ntwo'
    assert h['Approved-by'] == 'A, 2026-01-01'
    assert h['Applies-to'].endswith(f'({SHA})')
    assert 'diff' not in ' '.join(h)


def test_parse_patch_needs_fields(fake):
    fake['patch'].write_text(fake['patch'].read_text().replace('Approved-by: Someone, 2026-01-01\n', ''))
    with pytest.raises(patches.PatchError, match='lacks Approved-by'):
        patches.parse_patch(fake['patch'])


def test_parse_patch_rejects_new_files_and_bad_names(fake, tmp_path):
    txt = fake['patch'].read_text()
    fake['patch'].write_text(txt.replace('index ', 'new file mode 100644\nindex ', 1))
    with pytest.raises(patches.PatchError, match='only edits existing files'):
        patches.parse_patch(fake['patch'])
    bad = fake['pd'] / 'fake' / 'fix.patch'
    bad.write_text(txt)
    with pytest.raises(patches.PatchError, match='NNNN'):
        patches.parse_patch(bad)


def test_apply_patches_the_copy_and_leaves_the_source(fake):
    before = (fake['up'] / 'fake' / 'firmware' / 'src' / 'x.c').read_bytes()
    stamp = _apply(fake)
    out = fake['root'] / 'out'
    assert (out / 'upstream/fake/firmware/src/x.c').read_text() == DST
    assert not (out / 'upstream/fake/firmware/src/y.c').exists()     # untouched files are not copied
    assert (fake['up'] / 'fake' / 'firmware' / 'src' / 'x.c').read_bytes() == before
    assert stamp['fake']['dirs'] == ['firmware/src']
    assert patches.include_dirs(out) == ['firmware/upstream/fake/firmware/src']
    assert patches.verify(out, fake['pd'], fake['up'], fake['lock']) == []


def test_apply_is_idempotent(fake):
    _apply(fake)
    first = (fake['root'] / 'out/upstream/fake/firmware/src/x.c').read_bytes()
    _apply(fake)
    assert (fake['root'] / 'out/upstream/fake/firmware/src/x.c').read_bytes() == first


def test_failing_hunk_names_the_patch(fake):
    (fake['up'] / 'fake' / 'firmware' / 'src' / 'x.c').write_text('something else entirely\n')
    with pytest.raises(patches.PatchError) as e:
        _apply(fake)
    assert 'patches/fake/0001-guard.patch does not apply' in str(e.value)
    assert 'x.c' in str(e.value)


def test_check_reports_a_failing_hunk(fake):
    (fake['up'] / 'fake' / 'firmware' / 'src' / 'x.c').write_text('something else entirely\n')
    probs = patches.check(None, fake['pd'], fake['up'], fake['lock'])
    assert len(probs) == 1 and '0001-guard.patch' in probs[0]


def test_stamp_goes_stale_when_a_patch_changes(fake):
    _apply(fake)
    out = fake['root'] / 'out'
    assert patches.verify(out, fake['pd'], fake['up'], fake['lock']) == []
    fake['patch'].write_text(fake['patch'].read_text().replace('Description: test', 'Description: test2') + '\n')
    stale = patches.verify(out, fake['pd'], fake['up'], fake['lock'])
    assert stale and 'stale' in stale[0]


def test_stamp_goes_stale_when_the_pin_or_source_changes(fake):
    _apply(fake)
    out = fake['root'] / 'out'
    fake['lock'].write_text(fake['lock'].read_text().replace(SHA, 'b' * 40))
    assert patches.verify(out, fake['pd'], fake['up'], fake['lock'])


def test_verify_without_stamp_and_without_series(fake, tmp_path):
    assert 'missing' in patches.verify(tmp_path / 'nothing', fake['pd'], fake['up'], fake['lock'])[0]
    assert patches.verify(tmp_path / 'nothing', tmp_path / 'no-patches-dir', fake['up'], fake['lock']) == []


def test_check_passes_and_reports_a_moved_pin(fake):
    assert patches.check(None, fake['pd'], fake['up'], fake['lock']) == []
    fake['lock'].write_text(fake['lock'].read_text().replace(SHA, 'b' * 40))
    probs = patches.check(None, fake['pd'], fake['up'], fake['lock'])
    assert len(probs) == 1
    assert 'vendir.lock.yml now pins' in probs[0] and 'refresh it' in probs[0]


def test_check_reports_missing_upstream_tree(fake):
    import shutil
    shutil.rmtree(fake['up'] / 'fake')
    probs = patches.check(None, fake['pd'], fake['up'], fake['lock'])
    assert any('not fetched' in p and 'vendor:ensure' in p for p in probs)


def test_apply_refuses_to_wipe_the_upstream_tree(fake):
    with pytest.raises(patches.PatchError, match='refusing'):
        patches.apply_series(fake['up'], None, fake['pd'], fake['up'], fake['lock'])


def test_apply_does_not_touch_the_enclosing_git_repo(fake):
    """Output inside a git repository: git apply must not look at or change that repository."""
    repo = fake['root'] / 'repo'
    repo.mkdir()
    subprocess.run(['git', 'init', '-q'], cwd=repo, check=True)
    (repo / 'x.c').write_text('keep\n')
    out = repo / 'build' / 'gen' / 'patched'
    patches.apply_series(out, None, fake['pd'], fake['up'], fake['lock'])
    assert (out / 'upstream/fake/firmware/src/x.c').read_text() == DST
    assert (repo / 'x.c').read_text() == 'keep\n'
    assert subprocess.run(['git', 'status', '--porcelain', '--', 'x.c'], cwd=repo, capture_output=True,
                          text=True).stdout.strip() == '?? x.c'
    assert not (repo / '.git' / 'index').exists()


_REAL_FELUCCA = ROOT / 'firmware' / 'upstream' / 'felucca' / 'firmware' / 'src' / 'fx.c'


@pytest.mark.skipif(not _REAL_FELUCCA.is_file(), reason='firmware/upstream/felucca is not fetched (task vendor:ensure)')
def test_check_passes_on_the_real_tree():
    assert patches.check() == []


@pytest.mark.skipif(not _REAL_FELUCCA.is_file(), reason='firmware/upstream/felucca is not fetched (task vendor:ensure)')
def test_real_patch_one_adds_only_ifndef_guards(tmp_path):
    out = tmp_path / 'patched'
    patches.apply_series(out)
    for rel in ('fx.c', 'slicer.c'):
        old = (ROOT / 'firmware/upstream/felucca/firmware/src' / rel).read_text().splitlines()
        new = (out / 'upstream/felucca/firmware/src' / rel).read_text().splitlines()
        added = [l for l in new if l not in old]
        assert added and all(l.startswith(('#ifndef ', '#endif')) for l in added), added
        assert [l for l in old if l not in new] == []
    stamp = json.loads((out / 'stamp.json').read_text())
    assert stamp['felucca']['sha'] == patches.locked_sha('felucca')
    assert hashlib.sha256(_REAL_FELUCCA.read_bytes()).hexdigest() == stamp['felucca']['sources']['firmware/src/fx.c']
