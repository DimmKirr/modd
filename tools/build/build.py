#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Build modd: genconfig + JieLi clang compile + .fwsc package.

Usage:
  tools/build.py                    # build from .config
  tools/build.py --defconfig NAME   # load defconfig, then build
  tools/build.py --genconfig-only   # just write config.h, don't compile
  tools/build.py --dry-run          # show what would be compiled

The JieLi toolchain is Linux x86-64 only. Runner, first match:
  - Native x86-64 Linux: runs directly.
  - Other Linux whose kernel runs x86-64 ELFs itself (Rosetta on Apple-silicon
    VMs, a binfmt_misc registration): runs through an x86-64 ld.so.
  - Other Linux with qemu-x86_64: runs via user-mode QEMU.
  - Otherwise (macOS, or JIELI_DOCKER=1): runs in a linux/amd64 Docker container.
The two Linux fallbacks need x86-64 glibc and libstdc++: JIELI_X86_GLIBC and
JIELI_X86_STDCPP, else $JIELI_TOOLCHAIN/../x86libs (task toolchain:x86libs),
else the nix store.
"""
import argparse
import glob
import hashlib
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import check_colors
import fm1pkg_make
import lz4blk
import mem_report
from fm1_paths import open_toolchain

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / 'firmware'
BUILD = ROOT / 'build'
OUT = BUILD  # overridden per defconfig in main()

# The default toolchain: the open pi32v2 LLVM (github.com/jvitkauskas/pi32v2-llvm releases).
# Only USE_OPEN_TOOLCHAIN=0 selects JieLi's clang 4.0.1 (rule in fm1_paths.py).
OPEN = open_toolchain()
# Fallback when OPEN_TOOLCHAIN is unset (the Taskfile always sets it from OPEN_TOOLCHAIN_VERSION,
# the pin; keep this tag equal to it).
OPEN_DEFAULT = '~/.fm1-toolchain/open/pi32v2-23.1.5-5'
GEN = Path(os.environ['MODD_GEN_DIR']) if 'MODD_GEN_DIR' in os.environ else BUILD / 'modd' / 'gen'

DOCKER_IMAGE = os.environ.get('JIELI_DOCKER_IMAGE', 'debian:bookworm-slim')

CFLAGS = [
    '-mcpu=r3', '-mfprev1', '-ffp-contract=off',
    '-Werror=double-promotion', '-Os',
    '-ffunction-sections', '-fdata-sections',
    '-fno-builtin', '-Wall', '-Wno-unused-function',
]


def open_toolchain_path():
    root = Path(os.environ.get('OPEN_TOOLCHAIN') or OPEN_DEFAULT).expanduser()
    if not (root / 'bin' / 'clang').exists():
        raise SystemExit(
            f'open toolchain not found: {root}/bin/clang is missing.\n'
            'Run "task toolchain:open:download" (or set OPEN_TOOLCHAIN to its root).')
    return root


# JieLi clang 4 accepts what the fork's clang 23 reports as errors; downgrade them again.
_OPEN_DOWNGRADED = ['implicit-function-declaration', 'implicit-int', 'int-conversion',
                    'incompatible-function-pointer-types', 'incompatible-pointer-types',
                    'return-mismatch', 'incompatible-pointer-types-discards-qualifiers']
_OPEN_TOOLS = {   # JieLi-relative tool name used by this file -> open toolchain binary
    'pi32v2/bin/ld': 'ld.lld', 'pi32v2/bin/ar': 'llvm-ar',
    'common/bin/objcopy': 'llvm-objcopy', 'common/bin/objdump': 'llvm-objdump',
}


def _open_cc_args(args):
    """Translate JieLi clang options to the open clang's (as tools/dropin/jlwrap.py does)."""
    out = []
    for a in args:
        if a == '-mfprev1':
            out += ['-Xclang', '-target-feature', '-Xclang', '+fprev1']
        elif a == '-fallow-pointer-null':
            out.append('-fno-delete-null-pointer-checks')
        else:
            out.append(a)
    pre = ['-Wno-unknown-warning-option', *(f'-Wno-error={w}' for w in _OPEN_DOWNGRADED)]
    if any(a.endswith('.c') for a in out) and not any(a.startswith('-std') for a in out):
        pre.insert(0, '-std=gnu11')   # clang 4 compiled C as gnu11
    return pre + out


def _open_run(tool, args, capture=False):
    root = open_toolchain_path()
    if tool == 'cc':
        exe, argv = 'clang', ['--target=pi32v2', *_open_cc_args(args)]
    else:
        exe, argv = _OPEN_TOOLS[tool], list(args)
        if tool == 'pi32v2/bin/ld':
            if any('--plugin' in a for a in argv):
                raise SystemExit('the vendor-library link (WIFI_NONFREE) is not supported '
                                 'with the open toolchain yet')
            argv += [str(root / 'lib' / 'clang' / '23' / 'lib' / 'pi32v2' / 'libclang_rt.builtins.a')]
    cmd = [str(root / 'bin' / exe), *argv]
    if capture:
        return subprocess.run(cmd, check=True, capture_output=True, text=True, cwd=str(ROOT)).stdout
    subprocess.run(cmd, check=True, cwd=str(ROOT))
    return None


def toolchain_path():
    tc = os.environ.get('JIELI_TOOLCHAIN')
    if not tc:
        raise SystemExit(
            'JIELI_TOOLCHAIN not set. Run "task fm1-platform:download" first.')
    p = Path(tc)
    if not (p / 'pi32v2' / 'bin' / 'clang').exists():
        raise SystemExit(
            f'JIELI_TOOLCHAIN={tc} does not contain pi32v2/bin/clang')
    return p


def _find_x86_libs(tc_path=None):
    """x86-64 glibc and libstdc++ for the Rosetta and qemu runners: two directories
    holding lib/ld-linux-x86-64.so.2 and lib/libstdc++.so.6 (may be the same one)."""
    glibc = os.environ.get('JIELI_X86_GLIBC', '')
    stdcpp = os.environ.get('JIELI_X86_STDCPP', '')
    if glibc and stdcpp:
        return glibc, stdcpp

    if tc_path is not None:
        local = Path(tc_path).parent / 'x86libs'
        if ((local / 'lib' / 'ld-linux-x86-64.so.2').exists()
                and (local / 'lib' / 'libstdc++.so.6').exists()):
            return str(local), str(local)

    for p in sorted(glob.glob('/nix/store/*/lib/ld-linux-x86-64.so.2')):
        glibc = str(Path(p).parents[1])
        break
    for p in sorted(glob.glob('/nix/store/*/lib/libstdc++.so.6')):
        lib = Path(p)
        try:
            out = subprocess.run(
                ['readelf', '-h', str(lib)],
                capture_output=True, text=True, timeout=5)
            if 'X86-64' in out.stdout:
                stdcpp = str(lib.parents[1])
                break
        except (subprocess.TimeoutExpired, FileNotFoundError):
            continue
    return glibc, stdcpp


def _ar_member(archive, member):
    """One member of a `!<arch>` archive, GNU or BSD naming. Done here because the
    host ar differs: macOS ar has no --output and extracts into its cwd only."""
    data = Path(archive).read_bytes()
    if data[:8] != b'!<arch>\n':
        raise SystemExit(f'{archive}: not an ar archive')
    pos, longnames = 8, b''
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        name = hdr[:16].decode('latin-1').rstrip()
        size = int(hdr[48:58])
        body = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
        if name == '/':
            continue                     # symbol table
        if name == '//':
            longnames = body             # GNU long-name table
            continue
        if name.startswith('/'):
            off = int(name[1:])
            name = longnames[off:longnames.index(b'/\n', off)].decode('latin-1')
        elif name.startswith('#1/'):
            n = int(name[3:])            # BSD: name precedes the data
            name = body[:n].rstrip(b'\0').decode('latin-1')
            body = body[n:]
        elif name.endswith('/'):
            name = name[:-1]
        if name == member:
            return body
    raise SystemExit(f'{archive}: no member {member}')


def _driver_plan(text):
    """Sub-commands printed by `clang -###`, one argv list per line that starts with ` "`."""
    import shlex
    return [shlex.split(line) for line in text.splitlines() if line.startswith(' "')]


class _NativeRunner:
    def __init__(self, tc_path):
        self.tc = tc_path

    def run(self, tool_path, args, capture=False):
        cmd = [str(self.tc / tool_path), *args]
        return subprocess.run(cmd, check=True, cwd=str(ROOT),
                              stdout=subprocess.PIPE if capture else None, text=True).stdout

    def label(self):
        return ''


class _X86Runner:
    """Run the x86-64 toolchain behind an argv prefix (an x86-64 ld.so, or qemu-x86_64).

    The driver forks `clang -cc1` as a child process. That execve leaves the
    prefix and goes to the kernel, which on Apple-silicon VMs hands x86-64 ELFs
    to Rosetta; Rosetta then rejects the nix-ld shim installed as
    /lib64/ld-linux-x86-64.so.2 ("not an x86-64 elf (0xb7)"). So ask the driver
    for its plan and run every sub-command behind the prefix ourselves."""

    def __init__(self, tc_path, prefix, env, label, exe=None):
        self.tc = tc_path
        self.prefix = prefix
        self.env = env
        self._label = label
        # What /proc/self/exe reads as inside the prefix. An explicit ld.so
        # reports itself, so the driver names the loader as its -cc1 executable
        # and derives its resource and tool directories from the loader's
        # directory; the plan is rewritten back to the real driver. qemu fakes
        # /proc/self/exe, so nothing to fix there.
        self.exe = exe

    def _fix(self, argv, tool):
        if not self.exe:
            return argv
        exe_dir, tool_dir = str(Path(self.exe).parent), str(Path(tool).parent)
        return [a.replace(self.exe, tool).replace(exe_dir, tool_dir) for a in argv]

    def run(self, tool_path, args, capture=False):
        env = os.environ.copy()
        env.update(self.env)
        tool = str(self.tc / tool_path)
        if str(tool_path).endswith('clang'):
            plan = subprocess.run([*self.prefix, tool, '-###', *args], cwd=str(ROOT), env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            cmds = _driver_plan(plan.stderr)
            if plan.returncode != 0 or not cmds:
                sys.stderr.write(plan.stderr)
                raise subprocess.CalledProcessError(plan.returncode or 1, plan.args)
            out = ''
            for argv in cmds:
                argv = self._fix(argv, tool)
                out = subprocess.run([*self.prefix, *argv], check=True, cwd=str(ROOT), env=env,
                                     stdout=subprocess.PIPE if capture else None, text=True).stdout
            return out
        cmd = [*self.prefix, tool, *args]
        return subprocess.run(cmd, check=True, cwd=str(ROOT), env=env,
                              stdout=subprocess.PIPE if capture else None, text=True).stdout

    def label(self):
        return self._label


class _RosettaRunner(_X86Runner):
    """The kernel executes x86-64 ELFs itself; only the loader and libraries are ours."""

    def __init__(self, tc_path, glibc, stdcpp):
        loader = f'{glibc}/lib/ld-linux-x86-64.so.2'
        super().__init__(tc_path,
                         [loader, '--library-path', f'{stdcpp}/lib:{glibc}/lib'],
                         {}, ' (rosetta)', exe=loader)


class _QemuRunner(_X86Runner):
    def __init__(self, tc_path, glibc, stdcpp):
        super().__init__(tc_path, [shutil.which('qemu-x86_64')],
                         {'QEMU_LD_PREFIX': glibc,
                          'LD_LIBRARY_PATH': f'{stdcpp}/lib:{glibc}/lib'},
                         ' (qemu)')


def _can_exec_x86(glibc, tc_path=None):
    """True when an x86-64 ld.so can run the toolchain (Rosetta, binfmt_misc).

    A simple ``ld.so --version`` succeeds even when Rosetta is absent (the
    loader itself is a static binary).  We therefore also compile a trivial
    file with the real toolchain when *tc_path* is given; a SIGSEGV (exit 139)
    means the instruction-level emulation layer is missing."""
    ld = Path(glibc) / 'lib' / 'ld-linux-x86-64.so.2'
    try:
        if subprocess.run([str(ld), '--version'], capture_output=True,
                          timeout=10).returncode != 0:
            return False
    except (OSError, subprocess.TimeoutExpired):
        return False
    if tc_path is None:
        return True
    import tempfile
    clang = str(Path(tc_path) / 'pi32v2' / 'bin' / 'clang')
    lib_path = f'{glibc}/lib'
    with tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False) as f:
        f.write('void _x(void){}\n')
        src = f.name
    obj = src.replace('.c', '.o')
    try:
        r = subprocess.run(
            [str(ld), '--library-path', lib_path, clang,
             '-target', 'pi32v2', '-c', src, '-o', obj],
            capture_output=True, timeout=30)
        return r.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False
    finally:
        for p in (src, obj):
            try:
                os.unlink(p)
            except OSError:
                pass


DOCKER_RETRIES = 2


class _DockerRunner:
    def __init__(self, tc_path):
        self.tc = tc_path

    def run(self, tool_path, args, capture=False):
        mapped = []
        for a in args:
            s = str(a)
            mapped.append(s.replace(str(ROOT), '/work') if str(ROOT) in s else s)
        cmd = [
            'docker', 'run', '--rm', '--platform', 'linux/amd64',
            '-v', f'{ROOT}:/work',
            '-v', f'{self.tc}:/opt/jieli:ro',
            '-w', '/work',
            DOCKER_IMAGE,
            f'/opt/jieli/{tool_path}', *mapped,
        ]
        # x86-64 clang under Docker Desktop's amd64 emulation on Apple silicon segfaults now and
        # then on input that compiles fine natively (2026-11-20: an LVGL file that preprocesses
        # to nothing). The driver then exits 254 (frontend crash) or the process dies on SIGSEGV
        # (139); run it again, up to DOCKER_RETRIES times, before failing the build.
        for attempt in range(DOCKER_RETRIES + 1):
            r = subprocess.run(cmd, cwd=str(ROOT),
                               stdout=subprocess.PIPE if capture else None, text=True)
            if r.returncode not in (254, 139) or attempt == DOCKER_RETRIES:
                break
            print(f'build: {tool_path} crashed under emulation (exit {r.returncode}), retrying...',
                  file=sys.stderr)
        if r.returncode:
            raise subprocess.CalledProcessError(r.returncode, cmd, r.stdout)
        return r.stdout

    def label(self):
        return ' (docker)'


def _pick_runner(tc_path):
    forced = os.environ.get('JIELI_DOCKER')
    if forced == '1':
        return _DockerRunner(tc_path)

    is_x86 = platform.machine() in ('x86_64', 'AMD64')
    is_linux = platform.system() == 'Linux'
    is_darwin = platform.system() == 'Darwin'

    if is_linux and is_x86:
        return _NativeRunner(tc_path)

    if is_linux:
        glibc, stdcpp = _find_x86_libs(tc_path)
        if glibc and stdcpp:
            if _can_exec_x86(glibc, tc_path):
                return _RosettaRunner(tc_path, glibc, stdcpp)
            if shutil.which('qemu-x86_64'):
                return _QemuRunner(tc_path, glibc, stdcpp)

    if is_darwin:
        glibc, stdcpp = _find_x86_libs(tc_path)
        if glibc and stdcpp and shutil.which('qemu-x86_64'):
            return _QemuRunner(tc_path, glibc, stdcpp)

    return _DockerRunner(tc_path)


_runner = None


def tc(tool, *args, capture=False):
    global _runner
    if OPEN:
        return _open_run(tool, [str(a) for a in args], capture=capture)
    if _runner is None:
        _runner = _pick_runner(toolchain_path())

    if tool == 'cc':
        tool_path = 'pi32v2/bin/clang'
        extra = ['-target', 'pi32v2']
    else:
        tool_path = tool
        extra = []

    return _runner.run(tool_path, [*extra, *[str(a) for a in args]], capture=capture)


def genconfig(defconfig=None, outdir=None):
    cmd = [sys.executable, str(ROOT / 'tools' / 'build' / 'genconfig.py')]
    if defconfig:
        cmd += ['--defconfig', defconfig]
    if outdir:
        cmd += ['--outdir', str(outdir)]
    subprocess.run(cmd, check=True, cwd=str(ROOT))


def loader_wrap(image):
    """SPL loader container: JLFS headers and independently checked LZ4 blocks."""
    def header(crc, offset, size, attr):
        body = struct.pack('<HIIBBH16s', crc, offset, size, attr, 0, 0,
                           b'usb_hid_ota.bin')
        return struct.pack('<H', fm1pkg_make.crc16(body)) + body

    blocks = bytearray()
    for start in range(0, len(image), 4096):
        raw = image[start:start + 4096]
        compressed = lz4blk.compress(raw)
        if len(compressed) > 4096 or lz4blk.decompress(compressed) != raw:
            raise SystemExit(f'loader: invalid compressed block at {start}')
        blocks += struct.pack('<II', len(compressed), len(raw)) + compressed
    body = header(fm1pkg_make.crc16(image), len(image), 0x01C0A800, 0) + blocks
    return header(fm1pkg_make.crc16(body), 0x20, len(body), 0x41) + body


def build_support(name, entry):
    """Compile the standalone boot stub or RAM update loader."""
    src, out = FW / name, OUT / name
    out.mkdir(parents=True, exist_ok=True)
    tc('cc', f'-I{src}', '-c', src / entry, '-o', out / 'crt0.o')
    tc('cc', *CFLAGS, '-Ifirmware/hal', '-Ifirmware/src', f'-I{src}',
       '-c', src / f'{name}.c', '-o', out / f'{name}.o')
    elf = out / f'{name}.elf'
    tc('pi32v2/bin/ld', '--gc-sections', '-e', '_start', '-T', src / f'{name}.ld',
       out / 'crt0.o', out / f'{name}.o', '-o', elf)
    binary = out / f'{name}.bin'
    binary.unlink(missing_ok=True)
    tc('common/bin/objcopy', '-O', 'binary', '-j', '.text', elf, binary)
    return binary.read_bytes()


def package(version):
    stub = build_support('stub', 'crt0_stub.S')
    if not fm1pkg_make.has_stub(stub + bytes(fm1pkg_make.STUB_LEN)):
        raise SystemExit('boot stub: missing CRSTUB signature')
    if not stub or len(stub) > fm1pkg_make.STUB_LEN - 256:
        raise SystemExit('boot stub exceeds its reserved slot')
    image = build_support('loader', 'crt0_ldr.S')
    if not image or len(image) > 0x14000:
        raise SystemExit('loader: image empty or too big')
    ota = loader_wrap(image)
    if len(ota) > 0x4000:
        raise SystemExit('loader overlaps the update-record sector')
    (OUT / 'loader' / 'ota.bin').write_bytes(ota)
    body = (OUT / 'body.bin').read_bytes()
    if not body or len(body) > fm1pkg_make.BODY_MAX:
        raise SystemExit('application body empty or too big')
    app = stub.ljust(fm1pkg_make.STUB_LEN, b'\xff') + body
    (OUT / 'modd.bin').write_bytes(app)
    fwsc = fm1pkg_make.ufw(fm1pkg_make.flash_image(app, fm1pkg_make.KEY), ota, 'FM-1_900')
    path = OUT / 'modd.fwsc'
    path.write_bytes(fwsc)
    (OUT / 'modd.version').write_text(version)
    print(f'package: {path.relative_to(ROOT)} ({len(fwsc)} bytes)')


def _firmware_version():
    """Return the firmware version string.

    Priority: FIRMWARE_VERSION env var > dev-<datetime>-<sha> > v0.0.0.
    """
    env_ver = os.environ.get('FIRMWARE_VERSION')
    if env_ver:
        return env_ver
    try:
        sha = subprocess.run(
            ['git', 'rev-parse', '--short', 'HEAD'],
            capture_output=True, text=True, cwd=str(ROOT), timeout=5,
        ).stdout.strip()
        if sha:
            from datetime import datetime, timezone
            now = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
            return f'dev-{now}-{sha}'
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return 'v0.0.0'


def gen_felucca_ui():
    """Run Felucca's font/icon/keycap/palette generators into GEN dir."""
    GEN.mkdir(parents=True, exist_ok=True)
    felucca = FW / 'upstream' / 'felucca'
    tools = felucca / 'tools'
    cmds = [
        [tools / 'gen_aa_font.py', GEN / 'ui_fonts.h', '--preset', 'inter-tight'],
        [tools / 'gen_aa_icons.py', GEN / 'ui_icons.h'],
        [tools / 'gen_aa_keycaps.py', GEN / 'ui_keycaps.h'],
        [tools / 'gen_ui_palettes.py', GEN / 'ui_palettes.h'],
    ]
    procs = [subprocess.Popen([sys.executable, *map(str, c)],
             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
             for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f'gen_felucca_ui: {", ".join(failed)} failed')
    print(f'gen_felucca_ui: {len(cmds)} headers -> {GEN.relative_to(ROOT)}/')


def gen_melodee_ui():
    """Run Melodee's font/keycap generators and modd's palette list into GEN/melodee/."""
    out = GEN / 'melodee'
    out.mkdir(parents=True, exist_ok=True)
    tools = FW / 'upstream' / 'melodee' / 'tools'
    rubik = str(FW / 'upstream' / 'melodee' / 'assets' / 'fonts' / 'Rubik[wght].ttf')
    cmds = [
        [tools / 'gen_aa_font.py', out / 'ui_fonts.h', '--preset', 'rubik'],
        [tools / 'gen_aa_font.py', out / 'ui_font_xs.h',
         '--face', f'XS={rubik}@400:12:32-126', '--kern-min', '0'],
        [tools / 'gen_aa_keycaps.py', out / 'ui_keycaps.h'],
        [ROOT / 'tools' / 'codegen' / 'gen_modd_palettes.py', out / 'ui_palettes.h'],
    ]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], cwd=str(tools.parent),
             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
             for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f'gen_melodee_ui: {", ".join(failed)} failed')
    print(f'gen_melodee_ui: {len(cmds)} headers -> {out.relative_to(ROOT)}/')


def gen_engine_tables():
    """Generate fixed-point lookup tables for Felucca-derived engines."""
    GEN.mkdir(parents=True, exist_ok=True)
    dst = GEN / 'fe_tables.h'
    subprocess.run([sys.executable, str(ROOT / 'tools' / 'codegen' / 'gen_engine_tables.py'),
                    str(dst)], check=True, cwd=str(ROOT))


def extract_section(elf, section):
    """objcopy one section to build/<name>.bin and return its bytes.

    objcopy writes to a path that never existed before (a fresh token in the
    name) and Python copies it to the canonical name afterwards. Deleting
    build/data.bin on the host and having the Docker runner recreate it in the
    next second is what Docker Desktop's shared-folder cache trips over
    ("No such file or directory" from objcopy, one build in a few)."""
    name = section[1:]
    fresh = OUT / f'{name}.{time.time_ns()}.tmp'
    tc('common/bin/objcopy', '-O', 'binary', '-j', section, str(elf), str(fresh))
    blob = fresh.read_bytes() if fresh.exists() else b''
    fresh.unlink(missing_ok=True)
    (OUT / f'{name}.bin').write_bytes(blob)
    return blob


LVGL = FW / 'vendor' / 'lvgl'
LVGL_CONF = FW / 'src' / 'ui' / 'themes' / 'lvgl'   # lv_conf.h, project-owned


def lvgl_flags():
    """What every translation unit that includes LVGL needs: where lv_conf.h is and LVGL's
    public headers (`#include <lvgl/lvgl.h>`, the layout LVGL's manual recommends)."""
    return ['-DLV_CONF_INCLUDE_SIMPLE', f'-I{LVGL_CONF}', f'-I{LVGL / "include"}']


def lvgl_cache_key(cflags, conf_bytes, version_bytes, root, toolchain_root):
    """Key of the kept LVGL objects. The repo and the toolchain are replaced by placeholders so
    that building one tree from two hosts (macOS and a Linux container mount it at different
    paths) does not invalidate the objects."""
    flags = '\0'.join(cflags)
    # Longer root first: the toolchain may live under the repo root.
    for real, name in sorted([(str(r), n) for r, n in ((root, '<root>'), (toolchain_root, '<toolchain>'))
                              if r], key=lambda p: -len(p[0])):
        flags = flags.replace(real, name)
    return hashlib.sha256(flags.encode() + conf_bytes + version_bytes).hexdigest()


def build_lvgl():
    """Compile firmware/vendor/lvgl/src into OUT/lvgl/liblvgl.a and return the archive.

    LVGL cannot join the unity build: its files are written to be compiled one by one and
    reuse static names. Objects are kept between builds; a change of flags, of lv_conf.h or
    of the vendored version rebuilds them all. The linker takes only the members modd.o
    refers to, and --gc-sections drops the unused functions inside them."""
    if not (LVGL / 'src').is_dir():
        raise SystemExit(f"{LVGL.relative_to(ROOT)} not found. Run 'task vendor:ensure'.")
    out = OUT / 'lvgl'
    out.mkdir(parents=True, exist_ok=True)
    cflags = ['-mcpu=r3', '-mfprev1', '-ffp-contract=off', '-Os',
              '-ffunction-sections', '-fdata-sections', '-fno-builtin',
              '-w',                      # vendored code: its warnings are upstream's
              *([] if OPEN else ['-isystem', str(toolchain_path() / 'pi32v2' / 'include')]),  # libc headers only
              *lvgl_flags()]
    version = (LVGL / 'lv_version.h').read_bytes()
    key = lvgl_cache_key(cflags, (LVGL_CONF / 'lv_conf.h').read_bytes(), version, ROOT,
                         None if OPEN else toolchain_path())
    stamp = out / 'flags.sha256'
    fresh = stamp.exists() and stamp.read_text() == key
    if not fresh:
        for stale in out.glob('*.o'):
            stale.unlink()
        # Stamp now, not after the last file: an object exists only once it compiled with these
        # flags, so a build that fails part way keeps its objects and the next run goes on from there.
        stamp.write_text(key)
    srcs = sorted((LVGL / 'src').rglob('*.c'))
    todo = []
    objs = []
    for src in srcs:
        obj = out / ('_'.join(src.relative_to(LVGL / 'src').with_suffix('').parts) + '.o')
        objs.append(obj)
        if not obj.exists() or obj.stat().st_mtime < src.stat().st_mtime:
            todo.append((src, obj))
    lib = out / 'liblvgl.a'
    if todo:
        jobs = int(os.environ.get('MODD_JOBS', min(4, os.cpu_count() or 2)))
        print(f'build: lvgl: compiling {len(todo)} of {len(srcs)} files ({jobs} jobs)...')

        def compile_one(pair):
            tc('cc', *cflags, '-c', pair[0], '-o', pair[1])

        with ThreadPoolExecutor(jobs) as pool:
            list(pool.map(compile_one, todo))
    if todo or not lib.exists():
        lib.unlink(missing_ok=True)
        tc('pi32v2/bin/ar', 'rcs', lib, *objs)
    print(f'build: lvgl: {lib.relative_to(ROOT)} ({len(objs)} objects)')
    return lib


def linker_script(enabled):
    """The memory map for a set of CONFIG_* names: app_wifi_nonfree.ld carries the
    nonfree WiFi stack's larger RAM; app_dock.ld keeps fm1-dock's module
    region (0x01C30000, 272 KiB) free. Kconfig forbids DOCK with WiFi."""
    if 'WIFI_NONFREE' in enabled:
        return 'app_wifi_nonfree.ld'
    if 'DOCK' in enabled:
        return 'app_dock.ld'
    if 'EMU' in enabled:
        return 'app_emu.ld'
    return 'app.ld'


def build(dry_run=False):
    gen_inc = [f'-I{GEN}']
    if str(GEN) != str(OUT / 'gen'):
        gen_inc.append(f'-I{OUT / "gen"}')
    flags = [*CFLAGS, '-Ifirmware/hal', '-Ifirmware/adapters',
             '-Ifirmware/src', '-Ifirmware', *gen_inc,
             '-Ifirmware/src/udisk',  # FatFs project config: ffconf.h, diskio.h
             '-Ifirmware/upstream/optimist/firmware/src/ble',
             '-Ifirmware/vendor/lwip/src/include',
             '-Ifirmware/src/net',   # lwipopts.h, arch/cc.h
             '-Ifirmware/src/wifi']  # wifi_rf_tables.h

    config_h = GEN / 'config.h'
    if not config_h.exists():
        raise SystemExit(
            f'{config_h} not found. Run "task config:generate:local" first.')

    enabled = set(re.findall(r'^#define CONFIG_(\w+) 1$', config_h.read_text(), re.M))

    if 'GEN_FELUCCA_UI' in enabled:
        if not (GEN / 'ui_fonts.h').exists():
            gen_felucca_ui()
        else:
            print(f'gen_felucca_ui: headers exist, skipping (delete {GEN}/ui_fonts.h to regenerate)')
    if 'GEN_MELODEE_UI' in enabled:
        if not (GEN / 'melodee' / 'ui_fonts.h').exists():
            gen_melodee_ui()
        else:
            print(f'gen_melodee_ui: headers exist, skipping (delete {GEN}/melodee/ to regenerate)')
        flags.insert(0, f'-I{GEN / "melodee"}')   # before GEN: its ui_fonts.h must win
    if 'UI_LVGL' in enabled:
        # the LVGL theme uses the modd theme's palettes (Settings > DISPLAY > THEME), not its
        # fonts: only the palette list, which needs no Pillow
        pal = GEN / 'lvgl' / 'ui_palettes.h'
        if not pal.exists():
            pal.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([sys.executable, str(ROOT / 'tools' / 'codegen' / 'gen_modd_palettes.py'),
                            str(pal)], check=True, cwd=str(ROOT))
        flags.insert(0, f'-I{GEN / "lvgl"}')
    if 'ENGINES' in enabled:
        gen_engine_tables()
    if 'WIFI' in enabled and not (GEN / 'ble_rf_tables.h').exists():
        src = FW / 'upstream' / 'optimist' / 'firmware' / 'hal' / 'ble_rf_tables_v15.h'
        if src.exists():
            import shutil
            shutil.copy2(src, GEN / 'ble_rf_tables.h')
            print(f'ble_rf_tables: copied {src.name} -> {GEN.relative_to(ROOT)}/')
        else:
            raise SystemExit(f'{src} not found; WiFi builds need the shared RF init tables')
    pending = enabled & {'MIDI_UART'}
    unsupported = {s for s in enabled if s.startswith('HAL_') and
                   s.endswith(('_FELUCCA', '_SLOOP'))}
    if pending or unsupported:
        raise SystemExit('Not integrated yet: ' + ', '.join(sorted(pending | unsupported)) +
                         '. Load the minimal defconfig for the LCD boot milestone.')

    # Strip the Kconfig version define so the build-time -D takes precedence
    # without a macro-redefined warning.
    config_text = config_h.read_text()
    filtered = re.sub(r'^#define CONFIG_FIRMWARE_VERSION ".*"$', '', config_text, flags=re.M)
    if filtered != config_text:
        config_h.write_text(filtered)

    flags.append(f'-include{GEN}/config.h')

    if 'DUAL_CORE' in enabled:
        flags.append('-DMODD_DUAL_CORE=1')
    if 'UI_LVGL' in enabled:
        flags += lvgl_flags()

    version = _firmware_version()
    flags.append(f'-DCONFIG_FIRMWARE_VERSION="{version}"')
    print(f'build: version {version}')

    if 'RPC' in enabled:
        # The REST API's OpenAPI document for this build: build/<dc>/openapi.json, and the
        # same bytes as gen/openapi_json.h for GET /openapi.json (CONFIG_RPC_OPENAPI).
        OUT.mkdir(parents=True, exist_ok=True)
        subprocess.run([sys.executable, str(ROOT / 'tools' / 'codegen' / 'gen_openapi.py'),
                        '--config', str(config_h), '--version', version,
                        '--json', str(OUT / 'openapi.json'),
                        '--header', str(GEN / 'openapi_json.h')],
                       check=True, cwd=str(ROOT))

    if 'WIFI_NONFREE' in enabled:
        fm1b_shim = ROOT / '.scratch' / 'examples' / 'fm1-base' / 'ble' / 'shim'
        if fm1b_shim.exists():
            flags.append(f'-I{fm1b_shim}')

    wifi_pass = os.environ.get('MODD_WIFI_PASSWORD', '')
    if wifi_pass and 'WIFI' in enabled:
        flags.append(f'-DCONFIG_WIFI_PASSWORD="{wifi_pass}"')
        print(f'build: WiFi password set ({len(wifi_pass)} chars)')

    asm_srcs = []
    if (FW / 'crt0.S').exists():
        asm_srcs.append(('crt0.S', FW / 'crt0.S'))
    for f in sorted((FW / 'hal').glob('*.S')):
        asm_srcs.append((f.name, f))
    if 'SCHED' in enabled:
        ctx_s = FW / 'src' / 'sched' / 'ctx.S'
        if ctx_s.exists():
            asm_srcs.append(('ctx.S', ctx_s))

    c_src = FW / 'src' / 'modd.c'
    if not c_src.exists():
        raise SystemExit(f'{c_src} not found')

    if dry_run:
        print('Would compile:')
        for name, path in asm_srcs:
            print(f'  [asm] {path.relative_to(ROOT)}')
        print(f'  [cc]  {c_src.relative_to(ROOT)}')
        if 'UI_LVGL' in enabled:
            print(f'  [lib] {LVGL.relative_to(ROOT)}/src/**/*.c -> liblvgl.a')
        print(f'Flags: {" ".join(flags)}')
        print(f'Then build firmware/stub, firmware/loader and package {OUT.relative_to(ROOT)}/modd.fwsc')
        return

    logo_src = ROOT / 'assets' / 'logo.svg'
    logo_hdr = GEN / 'boot_logo.h'
    if logo_src.exists() and not logo_hdr.exists():
        GEN.mkdir(parents=True, exist_ok=True)
        subprocess.run([sys.executable, str(ROOT / 'tools' / 'codegen' / 'logo_convert.py'),
                        str(logo_src), str(logo_hdr)],
                       check=True, cwd=str(ROOT))

    OUT.mkdir(parents=True, exist_ok=True)
    if OPEN:
        print(f'build: compiling with the open toolchain {open_toolchain_path()}...')
    else:
        runner = _pick_runner(toolchain_path())
        print(f'build: compiling{runner.label()}...')

    asm_gen_inc = [f'-I{GEN}']
    if str(GEN) != str(OUT / 'gen'):
        asm_gen_inc.append(f'-I{OUT / "gen"}')
    asm_flags = ['-Ifirmware', *asm_gen_inc, '-include', f'{GEN}/config.h']
    for name, path in asm_srcs:
        obj_name = Path(name).stem + '.o'
        tc('cc', *asm_flags, '-c', str(path), '-o', str(OUT / obj_name))

    tc('cc', *flags, '-c', str(c_src), '-o', str(OUT / 'modd.o'))

    objs = [str(OUT / (Path(name).stem + '.o')) for name, _ in asm_srcs]
    objs.append(str(OUT / 'modd.o'))
    if 'UI_LVGL' in enabled:
        objs.append(str(build_lvgl()))   # after modd.o: the linker takes what it refers to

    if 'USB_HOST' in enabled:
        sdk = Path(os.environ.get('AC79_SDK', Path.home() / '.fm1-toolchain' / 'AC79_SDK'))
        cpu_a = sdk / 'cpu' / 'wl82' / 'liba' / 'cpu.a'
        if cpu_a.exists():
            bc_obj = OUT / 'usb_sdk.bc'
            native_obj = OUT / 'usb_sdk.o'
            bc_obj.write_bytes(_ar_member(cpu_a, 'usb.c.o'))
            tc('cc', '-target', 'pi32v2', '-mcpu=r3', '-Os',
               '-ffunction-sections', '-fdata-sections',
               '-c', str(bc_obj), '-o', str(native_obj))
            objs.append(str(native_obj))
            print(f'build: compiled usb_sdk.o from cpu.a bitcode')

    vendor_libs = []

    if 'WIFI_NONFREE' in enabled:
        if OPEN:
            raise SystemExit('CONFIG_WIFI_NONFREE is not supported with the open toolchain yet '
                             '(set USE_OPEN_TOOLCHAIN=0 for JieLi).')
        if not os.environ.get('ALLOW_NONFREE'):
            raise SystemExit(
                'CONFIG_WIFI_NONFREE requires ALLOW_NONFREE=1.\n'
                'The vendor binary driver links proprietary .a libraries from\n'
                'the JieLi SDK. The resulting binary is for personal testing only\n'
                'and cannot be distributed under GPL-3.0.\n'
                'To opt in: ALLOW_NONFREE=1 task build -- --defconfig wifi_nonfree')

        nf_libs = ROOT / 'build' / 'nonfree' / 'libs'
        fm1b = ROOT / '.scratch' / 'examples' / 'fm1-base'
        nf_dir = OUT / 'nonfree'
        nf_dir.mkdir(parents=True, exist_ok=True)

        if not fm1b.exists():
            raise SystemExit(f'fm1-base not found at {fm1b}. '
                             'Copy fm1-base into .scratch/examples/fm1-base/')
        if not nf_libs.exists():
            raise SystemExit(
                f'Vendor libs not provisioned at {nf_libs}.\n'
                'Run: ALLOW_NONFREE=1 task nonfree:provision')

        nf_cf = [
            '-integrated-as', '-mcpu=r3', '-mfprev1',
            '-fno-common', '-Oz', '-flto',
            '-ffunction-sections', '-fdata-sections',
            '-fallow-pointer-null', '-fprefer-gnu-section',
            '-femulated-tls', '-fms-extensions', '-w',
            '-DSUPPORT_MS_EXTENSIONS', '-DCONFIG_RELEASE_ENABLE',
            '-DCONFIG_CPU_WL82', '-DCONFIG_FREE_RTOS_ENABLE',
            '-DCONFIG_THREAD_ENABLE', '-D__GCC_PI32V2__',
            '-DCONFIG_NEW_ECC_ENABLE', '-DCONFIG_SFC_ENABLE',
            '-DCONFIG_NO_SDRAM_ENABLE', '-DCONFIG_BT_ENABLE=1',
            '-DSHIM_WIFI=1',
            '-DSHIM_HEAP_KB=200',
            '-mllvm', '-pi32v2-large-program=true',
        ]
        tc_inc = toolchain_path() / 'pi32v2' / 'include'
        nf_inc = [f'-I{fm1b / "ble" / "shim"}', f'-I{tc_inc}']

        shim_c = [
            fm1b / 'ble' / 'shim' / 'os.c',
            fm1b / 'ble' / 'shim' / 'misc.c',
            fm1b / 'ble' / 'shim' / 'systimer.c',
            fm1b / 'ble' / 'shim' / 'rf_const.c',
            fm1b / 'ble' / 'wifi' / 'wifi_shim.c',
            FW / 'src' / 'wifi' / 'nonfree' / 'nf_sdk_stubs.c',
        ]
        for src in shim_c:
            obj = nf_dir / f'nf_{src.stem}.o'
            tc('cc', *nf_cf, *nf_inc, '-c', str(src), '-o', str(obj))
            objs.append(str(obj))

        ctx_obj = nf_dir / 'nf_ctx.o'
        tc('cc', *nf_inc, '-c', str(fm1b / 'ble' / 'shim' / 'ctx.S'),
           '-o', str(ctx_obj))
        objs.append(str(ctx_obj))

        for lib_name in ['btctrler.a', 'btstack.a', 'crypto_toolbox_Osize.a',
                         'lib_ccm_aes.a', 'wl_rf_common.a',
                         'wl_wifi_sta_sfc.a', 'wpasupplicant.a', 'hsm.a',
                         'libcompiler_rt.a']:
            p = nf_libs / lib_name
            if p.exists():
                vendor_libs.append(str(p))

        print(f'build: nonfree WiFi: {len(shim_c)} shim files + '
              f'{len(vendor_libs)} vendor libs')

    elf = OUT / 'modd.elf'
    lto_plugin = []
    if vendor_libs:
        gold = toolchain_path() / 'pi32v2' / 'bin' / 'LLVMgold.so'
        if gold.exists():
            lto_plugin = ['--plugin', str(gold),
                          '--plugin-opt=mcpu=r3',
                          '--plugin-opt=-pi32v2-large-program=true']
    ld_script = FW / linker_script(enabled)
    link_args = [*lto_plugin, '--gc-sections', '-T', str(ld_script), *objs]
    if vendor_libs:
        link_args += ['--allow-multiple-definition',
                      '--start-group', *vendor_libs, '--end-group']
    tc('pi32v2/bin/ld', *link_args, '-Map', str(OUT / 'modd.map'), '-o', str(elf))

    text = extract_section(elf, '.text')
    extract_section(elf, '.data')
    text_size = len(text)
    print(f'build: {elf.relative_to(ROOT)} ({text_size} bytes text)')
    # JieLi objcopy accepts one section per invocation. Assemble relocated
    # code/data at linker load addresses, never their runtime RAM addresses.
    symbols = tc('common/bin/objdump', '-t', elf, capture=True)
    body = bytearray(text)
    for section, symbol in (('.ram_text', '_rt_load'), ('.dsp_text', '_dsp_load'),
                            ('.dsp_tables', '_dt_load'), ('.data', '_data_load'),
                            ('.audio_text', '_audio_load')):
        blob = extract_section(elf, section)
        if not blob:
            continue
        match = re.search(r'^([0-9a-f]+) .*\s' + symbol + r'$', symbols, re.M)
        if not match:
            raise SystemExit(f'missing linker symbol: {symbol}')
        offset = int(match.group(1), 16) - 0x02001000
        if offset < len(body) or offset % 4 or offset + len(blob) > fm1pkg_make.BODY_MAX:
            raise SystemExit(f'invalid application load address: {symbol}')
        body += b'\xff' * (offset - len(body)) + blob
    body += b'\xff' * (-len(body) % 4)
    (OUT / 'body.bin').write_bytes(body)
    package(version)
    # Regions, budgets, stacks and the largest objects (tools/build/mem_report.py). The linker
    # script's regions and ASSERTs fail the link; a report error never fails a build, but too
    # little free RAM does (mem_report.RAM_MIN_FREE).
    try:
        print(mem_report.report(OUT))
        errors = mem_report.analyse_build(OUT)['errors']
    except Exception as e:  # noqa: BLE001
        print(f'mem_report: skipped ({e})')
        errors = []
    if errors:
        raise SystemExit('build failed: ' + '; '.join(errors))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--defconfig', help='load defconfig before building')
    ap.add_argument('--genconfig-only', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    global OUT, GEN
    dc = args.defconfig or 'modd'
    OUT = BUILD / (dc + '-open' if OPEN else dc)
    if 'MODD_GEN_DIR' not in os.environ:
        GEN = OUT / 'gen'

    if 'MODD_GEN_DIR' not in os.environ:
        genconfig(args.defconfig, outdir=GEN)
    if args.genconfig_only:
        return

    palette_dir = GEN / 'melodee'
    theme_dirs = [FW / 'src' / 'ui' / 'themes' / 'modd']
    cc_errors = []
    if palette_dir.exists():
        cc_errors.extend(check_colors.check_palette(palette_dir))
    cc_errors.extend(check_colors.check_theme_sources(theme_dirs))
    if cc_errors:
        print(f'check_colors: {len(cc_errors)} issue(s):')
        for e in cc_errors:
            print(f'  {e}')
        raise SystemExit(1)

    build(dry_run=args.dry_run)


if __name__ == '__main__':
    main()
