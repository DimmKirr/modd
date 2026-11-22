#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Unit tests for Kconfig loading and genconfig output.

Results are written to tests/results/<timestamp>-test_genconfig/<subtest>/.
"""
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
KCONFIG = ROOT / 'Kconfig'
sys.path.insert(0, str(ROOT / 'tests'))
import io_log  # noqa: E402

RUN_DIR = io_log.results_dir(Path(__file__).stem)     # next to this module's debug.log


def _outdir(name):
    d = RUN_DIR / name
    d.mkdir(parents=True, exist_ok=True)
    return d


@pytest.fixture(autouse=True)
def require_kconfig():
    assert KCONFIG.exists(), f'{KCONFIG} is missing'


def test_kconfig_parses():
    r = subprocess.run(
        ['python3', '-c',
         'from kconfiglib import Kconfig; '
         f'k = Kconfig("{KCONFIG}"); '
         'print(len(k.syms), "symbols")'],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr


def test_genconfig_writes_header():
    genconfig = ROOT / 'tools' / 'build' / 'genconfig.py'
    assert genconfig.exists(), f'{genconfig} is missing'
    out = _outdir('header_modd')
    r = subprocess.run(
        [sys.executable, str(genconfig), '--defconfig', 'modd',
         '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    header = out / 'config.h'
    assert header.exists(), 'config.h not generated'
    content = header.read_text()
    assert '#define' in content
    assert '#define CONFIG_AUDIO 1' in content
    assert '#define CONFIG_USB 1' in content
    assert '#define CONFIG_UI_SHELL_MODD 1' in content
    for symbol in ('EMU', 'DEBUG_FRAMEBUFFER', 'RPC_INPUT'):
        assert f'#define CONFIG_{symbol} ' not in content, f'production modd must not set {symbol}'
    assert (out / '.config').exists(), '.config not written to outdir'


def test_menuconfig_imports_library():
    r = subprocess.run([sys.executable, '-c',
        "import runpy; m = runpy.run_path('tools/build/menuconfig.py'); "
        "assert m['menuconfig'].__module__ == 'menuconfig'"],
        cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_genconfig_writes_manifest():
    genconfig = ROOT / 'tools' / 'build' / 'genconfig.py'
    assert genconfig.exists(), f'{genconfig} is missing'
    out = _outdir('manifest_modd')
    r = subprocess.run(
        [sys.executable, str(genconfig), '--defconfig', 'modd',
         '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    import json
    manifest = out / 'manifest.json'
    assert manifest.exists(), 'manifest.json not generated'
    data = json.loads(manifest.read_text())
    assert 'components' in data
    assert 'audio' in data['components']
    assert 'audio-core:melodee' in data['components']
    manifest_h = out / 'manifest.h'
    assert manifest_h.exists(), 'manifest.h not generated'
    content = manifest_h.read_text()
    assert '<<MODD>>' in content
    assert 'modd_manifest' in content


DEFCONFIGS = sorted(p.name[:-len('_defconfig')]
                    for p in (ROOT / 'configs').glob('*_defconfig'))


@pytest.mark.parametrize('name', DEFCONFIGS)
def test_defconfig_loads(name):
    dc = ROOT / 'configs' / f'{name}_defconfig'
    assert dc.exists(), f'{dc} is missing'
    r = subprocess.run(
        ['python3', '-c',
         'from kconfiglib import Kconfig; '
         f'k = Kconfig("{KCONFIG}"); '
         f'k.load_config("{dc}"); '
         'enabled = sum(1 for s in k.syms.values() if s.str_value == "y"); '
         f'print("{name}:", enabled, "enabled")'],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, f'{name}: {r.stderr}'


# -- component manifest assertions per defconfig --

EXPECTED_COMPONENTS = {
    'modd': sorted([
        'audio', 'audio-core:melodee', 'boot:melodee', 'debug-log', 'effects:melodee',
        'engine:analog', 'engine:fm6', 'engine:lofi', 'engine:noise', 'engine:trio', 'flash',
        'input', 'midi:melodee', 'mod:melodee', 'modulation', 'net:lwip', 'ota',
        'scales:melodee', 'seq:melodee', 'storage:melodee', 'udisk', 'ui:modd', 'usb',
        'usb-audio', 'usb-host', 'usb-net',
    ]),
}


@pytest.mark.parametrize('name,expected', [
    pytest.param(n, c, id=n) for n, c in EXPECTED_COMPONENTS.items()
])
def test_manifest_components(name, expected):
    """genconfig produces the expected component manifest for each defconfig."""
    dc = ROOT / 'configs' / f'{name}_defconfig'
    assert dc.exists(), f'{dc} is missing'
    out = _outdir(f'manifest_{name}')
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'build' / 'genconfig.py'),
         '--defconfig', name, '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    import json
    manifest = out / 'manifest.json'
    assert manifest.exists(), 'manifest.json not generated'
    actual = sorted(json.loads(manifest.read_text())['components'])
    assert actual == expected, (
        f'{name} manifest mismatch:\n'
        f'  expected: {expected}\n'
        f'  actual:   {actual}'
    )


def test_usb_host_depends_on_usb():
    """CONFIG_USB_HOST must not be selectable without CONFIG_USB."""
    k = _kconfig_with('minimal')
    usb = k.syms['USB']
    usb_host = k.syms['USB_HOST']
    usb.set_value(0)
    usb_host.set_value(2)  # try to force y
    assert usb_host.str_value != 'y', \
        'USB_HOST must depend on USB: should not be selectable when USB=n'


# -- Network module: lwIP is link-agnostic --

# The smallest config with audio and USB (the former modd_minimal_defconfig): the base the
# dependency tests below switch symbols on. Kept here, not in configs/: nothing builds it.
MINIMAL = '''CONFIG_AUDIO=y
CONFIG_AUDIO_CORE_MELODEE=y
CONFIG_EFFECTS_MELODEE=y
# CONFIG_INPUT is not set
# CONFIG_FLASH is not set
# CONFIG_MIDI_UART is not set
# CONFIG_DUAL_CORE is not set
CONFIG_USB=y
'''


def _kconfig_with(defconfig):
    """Kconfig loaded with configs/<defconfig>, or with MINIMAL for 'minimal'."""
    from kconfiglib import Kconfig
    k = Kconfig(str(KCONFIG), warn=False)
    if defconfig == 'minimal':
        path = _outdir('kconfig_minimal') / 'minimal_defconfig'
        path.write_text(MINIMAL)
        k.load_config(str(path))
    else:
        k.load_config(str(ROOT / 'configs' / defconfig))
    return k


def test_lwip_is_selectable_without_wifi():
    """CONFIG_LWIP is the stack; it must not require the WiFi radio."""
    k = _kconfig_with('minimal')
    assert k.syms['WIFI'].str_value == 'n'
    k.syms['LWIP'].set_value(2)
    assert k.syms['LWIP'].str_value == 'y'


def test_wifi_lwip_selects_lwip():
    """Enabling the WiFi link on lwIP pulls the stack in."""
    k = _kconfig_with('modd_defconfig')
    assert k.syms['WIFI_LWIP'].str_value == 'y'
    assert k.syms['LWIP'].str_value == 'y'


def test_wifi_defconfig_manifest_gains_net_lwip():
    """WiFi builds list the stack as its own component."""
    out = _outdir('manifest_modd_wifi')
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'build' / 'genconfig.py'),
         '--defconfig', 'modd', '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    import json
    components = json.loads((out / 'manifest.json').read_text())['components']
    assert 'net:lwip' in components


def test_rpc_and_llm_follow_lwip_not_wifi():
    """IP consumers depend on the stack, not on the WiFi link."""
    k = _kconfig_with('minimal')
    k.syms['LWIP'].set_value(2)
    assert k.syms['WIFI'].str_value == 'n'
    for name in ('RPC', 'LLM'):
        k.syms[name].set_value(2)
        assert k.syms[name].str_value == 'y', \
            f'{name} must be selectable with LWIP alone'


def test_usb_net_selects_lwip_and_needs_usb_host():
    """USB_NET is a link on lwIP: needs the USB host, pulls the stack in, no radio."""
    k = _kconfig_with('minimal')
    k.syms['USB_NET'].set_value(2)
    assert k.syms['USB_NET'].str_value == 'n', 'needs USB_HOST'
    k.syms['USB_HOST'].set_value(2)
    k.syms['USB_NET'].set_value(2)
    assert k.syms['USB_NET'].str_value == 'y'
    assert k.syms['LWIP'].str_value == 'y', 'USB_NET must select LWIP'
    assert k.syms['WIFI'].str_value == 'n'


def test_secret_symbols_are_masked_for_debug_dumps():
    """config.h keeps the secrets the firmware needs; config.masked.h, the
    only header a debug dump may print, shows a mask instead."""
    genconfig = ROOT / 'tools' / 'build' / 'genconfig.py'
    out = _outdir('masked_secrets')
    env = dict(os.environ, WIFI_PASSWORD='pw-for-this-test',
               OPENROUTER_API_KEY='key-for-this-test')
    r = subprocess.run(
        [sys.executable, str(genconfig), '--defconfig', 'modd',
         '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT), env=env)
    assert r.returncode == 0, r.stderr
    header = (out / 'config.h').read_text()
    masked = (out / 'config.masked.h').read_text()
    assert '#define CONFIG_WIFI_PASSWORD "pw-for-this-test"' in header
    assert 'pw-for-this-test' not in masked
    assert 'key-for-this-test' not in masked
    assert '#define CONFIG_WIFI_PASSWORD "***"' in masked
    # everything that is not a secret is unchanged
    plain = [l for l in header.splitlines()
             if 'WIFI_PASSWORD' not in l and 'OPENROUTER_API_KEY' not in l]
    assert all(l in masked.splitlines() for l in plain)


def test_emulator_build_keeps_its_wifi_credentials():
    """WIFI_SSID / WIFI_PASSWORD from the environment (.env: a real network) reach a hardware
    build, not an emulator build: modd_emu has to join fm1-live's AP with its defconfig's."""
    genconfig = ROOT / 'tools' / 'build' / 'genconfig.py'
    env = dict(os.environ, WIFI_SSID='ssid-for-this-test', WIFI_PASSWORD='pw-for-this-test')
    headers = {}
    for dc in ('modd', 'modd_emu'):
        out = _outdir(f'emu_wifi_{dc}')
        r = subprocess.run(
            [sys.executable, str(genconfig), '--defconfig', dc, '--outdir', str(out)],
            capture_output=True, text=True, cwd=str(ROOT), env=env)
        assert r.returncode == 0, r.stderr
        headers[dc] = (out / 'config.h').read_text()
    assert '#define CONFIG_WIFI_SSID "ssid-for-this-test"' in headers['modd']
    assert '#define CONFIG_WIFI_PASSWORD "pw-for-this-test"' in headers['modd']
    assert '#define CONFIG_EMU 1' in headers['modd_emu']
    assert '#define CONFIG_WIFI_SSID "Home"' in headers['modd_emu']
    assert '#define CONFIG_WIFI_PASSWORD "fm1pass"' in headers['modd_emu']
    assert 'for-this-test' not in headers['modd_emu']

