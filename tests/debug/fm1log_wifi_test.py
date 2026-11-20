#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""fm1log's WiFi diagnostics against fake DAP devices: the register sweep (--sweep) and the
WL_CTL0 probe request (--wl0, --retry)."""
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'device'))
import fm1log  # noqa: E402

# 0x40000 is the flash controller (SPECSHEET erratum 3): never swept
WINDOWS = [(0x10000, 0x11000), (0x14000, 0x15000), (0x30000, 0x32000)]


class SweepDevice:
    """Answers D_SWEEP like wifi_sweep_fill: [0] status, [4..7] base, [8..] words (addr ^ 0xA5A5A5A5)."""

    def __init__(self, max_words=60):
        self.max_words = max_words
        self.calls = []

    def ask(self, cmd, payload=b''):
        assert cmd == fm1log.D_SWEEP
        base, count = struct.unpack('<IH', payload)
        self.calls.append((base, count))
        if not any(lo <= base and base + 4 * count <= hi for lo, hi in WINDOWS):
            return b'\x0a'
        count = min(count, self.max_words)
        words = [((base + 4 * i) ^ 0xA5A5A5A5) & 0xFFFFFFFF for i in range(count)]
        return bytes(4) + struct.pack('<I', base) + struct.pack(f'<{count}I', *words)


class RecordingDevice:
    def __init__(self):
        self.sent = []

    def ask(self, cmd, payload=b''):
        self.sent.append((cmd, payload))
        return b'\x00'


def test_sweep_read_walks_the_block_in_reply_sized_chunks():
    dev = SweepDevice()
    regs = fm1log.sweep_read(dev, 0x30000, 130)
    assert [c[1] for c in dev.calls] == [60, 60, 10]
    assert len(regs) == 130
    assert regs[0x30000] == 0x30000 ^ 0xA5A5A5A5
    assert regs[0x30000 + 4 * 129] == (0x30000 + 516) ^ 0xA5A5A5A5


def test_sweep_read_refuses_outside_the_windows():
    with pytest.raises(RuntimeError):
        fm1log.sweep_read(SweepDevice(), 0x20000, 4)


def test_sweep_text_matches_the_vendor_line_format():
    text = fm1log.sweep_text({0x30F04: 0x37, 0x30000: 0xDEADBEEF})
    assert text == '0x00030000 = 0xdeadbeef\n0x00030f04 = 0x00000037\n'
    assert fm1log.build_parser().parse_args(['--sweep', '0x30000', '2048']).sweep == [0x30000, 2048]


def test_wl0_sends_the_request():
    dev = RecordingDevice()
    assert fm1log.wl0_request(dev) is True
    assert dev.sent == [(0x29, b'\x00\x00\x00\x00')]
    assert fm1log.build_parser().parse_args(['--wl0']).wl0 is True


def test_wl0_retry_sends_mode_two():
    dev = RecordingDevice()
    assert fm1log.wl0_request(dev, mode=2) is True
    assert dev.sent == [(0x29, b'\x02\x00\x00\x00')]
    assert fm1log.build_parser().parse_args(['--wl0', '--retry']).retry is True
