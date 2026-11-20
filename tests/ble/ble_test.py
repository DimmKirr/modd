# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-compiled BLE C tests (no hardware, no embedded toolchain).

Each test compiles a C source from the vendored Optimist BLE stack on the
host with ``cc``, runs it as a native executable, and checks the exit code.
This integrates with ``task test:unit`` (pytest discovers it automatically).
"""
from pathlib import Path

from c_harness import run_c_harness

ROOT = Path(__file__).resolve().parents[2]
OPTIMIST = ROOT / 'firmware' / 'upstream' / 'optimist'
OPTIMIST_BLE = OPTIMIST / 'firmware' / 'src' / 'ble'
OPTIMIST_TESTS = OPTIMIST / 'tests'
MODD_BLE = ROOT / 'firmware' / 'src' / 'ble'


def compile_and_run(source: Path, extra_includes: list[Path] | None = None,
                    defines: list[str] | None = None) -> str:
    """Compile a C test on the host and run it. Returns stdout."""
    result = run_c_harness(source, includes=[OPTIMIST_BLE, *(extra_includes or [])],
                           defines=defines or [], expect=None,
                           extra_flags=['-Wno-unused-function'])
    return result.stdout


def test_ble_midi_codec():
    """BLE-MIDI encoder/decoder round-trip (20,000 random events)."""
    out = compile_and_run(OPTIMIST_TESTS / 'ble_midi_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out


def test_ble_scan_parser():
    """AD parser, device table, ageing, signal bars."""
    out = compile_and_run(OPTIMIST_TESTS / 'ble_scan_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out


def test_ble_hid_report():
    """Xbox HID report to MIDI CC conversion."""
    out = compile_and_run(
        ROOT / 'tests' / 'ble' / 'ble_hid_test.c',
        extra_includes=[OPTIMIST_BLE, MODD_BLE],
    )
    assert 'FAIL' not in out
    assert 'all passed' in out


def test_ble_gatt_integration():
    """Full BLE stack: connect, GATT discover, BLE-MIDI subscribe, MIDI exchange."""
    out = compile_and_run(ROOT / 'tests' / 'ble' / 'ble_gatt_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out
