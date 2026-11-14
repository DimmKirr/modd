# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Host-side debug log reader: ring window math against a fake DAP device."""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'device'))
import fm1log
import hbdeploy

RING = 4096  # MODD_DBG_LOG_SIZE in firmware/adapters/modd_debug.h
CHUNK = 180  # bytes per D_LOG reply in firmware/src/debug/debug_dap.c


class FakeDevice:
    """Answers D_LOG the way debug_dap.c does: clamp `from` to the ring, 180-byte chunks."""

    def __init__(self, text):
        self.data = text.encode('latin-1')
        self.log_w = len(self.data)

    def ask(self, cmd, payload=b''):
        assert cmd == hbdeploy.LOG
        frm = struct.unpack('<I', payload)[0]
        w = self.log_w
        if w - frm > RING:
            frm = w - RING if w > RING else 0
        if frm > w:
            frm = w
        chunk = self.data[frm:min(w, frm + CHUNK)]
        return bytes(4) + struct.pack('<II', w, frm) + chunk


def _capture(monkeypatch, capsys, text, **kw):
    dev = FakeDevice(text)
    monkeypatch.setattr(hbdeploy, 'hello', lambda p: {'log_w': dev.log_w})
    fm1log.read_log(dev, host_ts=False, **kw)
    return capsys.readouterr().out


LONG = ''.join(f'wifi: line {i:04d}\n' for i in range(400))  # 6.4 KB, more than the ring


def test_dump_returns_everything_the_ring_holds(monkeypatch, capsys):
    assert _capture(monkeypatch, capsys, LONG) == LONG[-RING:]


def test_tail_limits_the_window(monkeypatch, capsys):
    assert _capture(monkeypatch, capsys, LONG, tail=1000) == LONG[-1000:]


def test_short_log_is_read_from_the_start(monkeypatch, capsys):
    text = 'boot\nwifi: hello\n'
    assert _capture(monkeypatch, capsys, text) == text


def test_cli_bytes_flag_defaults_to_whole_ring():
    assert fm1log.build_parser().parse_args([]).bytes == 0


class FakeUsbDevice:
    """Answers D_USB with a fixed dump (usb_debug_fill layout in usb_hub.c)."""

    def __init__(self, dump):
        self.dump = bytes(dump)

    def ask(self, cmd, payload=b''):
        assert cmd == fm1log.D_USB
        return self.dump


def _usb_dump_header():
    d = bytearray(51)        # [0] OK, no devices, no MSC candidates
    d[49] = 0xFF             # mounted: none
    return d


def test_print_usb_decodes_usb_net_candidate(capsys):
    d = _usb_dump_header() + bytes([2, 4, 2, 1, 0x81, 0x02])
    fm1log.print_usb(FakeUsbDevice(d))
    out = capsys.readouterr().out
    assert 'usb-net:   addr 2  cfg 4 iface 2 alt 1  ep in=1 out=2' in out


def test_print_usb_reports_no_usb_net_candidate(capsys):
    fm1log.print_usb(FakeUsbDevice(_usb_dump_header() + bytes(6)))
    assert 'usb-net:   none' in capsys.readouterr().out


def test_print_usb_tolerates_old_firmware_without_usb_net_block(capsys):
    fm1log.print_usb(FakeUsbDevice(_usb_dump_header()))
    assert 'usb-net' not in capsys.readouterr().out
    assert fm1log.build_parser().parse_args(['--bytes', '1000']).bytes == 1000


class ScriptedDevice(FakeDevice):
    """Appends one scripted chunk per LOG request after the first, to simulate a live device."""

    def __init__(self, text, later, die_after=None):
        super().__init__(text)
        self.later = list(later)
        self.polls = 0
        self.die_after = die_after

    def ask(self, cmd, payload=b''):
        self.polls += 1
        if self.die_after is not None and self.polls > self.die_after:
            raise SystemExit('no answer from the FM-1')
        if self.polls > 1 and self.later:
            self.data += self.later.pop(0).encode('latin-1')
            self.log_w = len(self.data)
        return super().ask(cmd, payload)


def test_follow_stops_on_the_stop_string_only_in_new_text(monkeypatch, capsys):
    old = 'boot\nwifi: scan done n=0\n'                       # an earlier scan already in the ring
    dev = ScriptedDevice(old, ['wifi: wl0 probe done\n', 'wifi: scan done n=0\n'])
    monkeypatch.setattr(hbdeploy, 'hello', lambda p: {'log_w': dev.log_w})
    pos = fm1log.dump_log(dev, host_ts=False)
    assert capsys.readouterr().out == old
    why = fm1log.follow_log(dev, pos, interval=0, host_ts=False, stop='wifi: scan done n=', timeout=5)
    assert why == 'stop'
    assert capsys.readouterr().out == 'wifi: wl0 probe done\nwifi: scan done n=0\n'


def test_follow_times_out_when_nothing_arrives(monkeypatch, capsys):
    dev = ScriptedDevice('boot\n', [])
    monkeypatch.setattr(hbdeploy, 'hello', lambda p: {'log_w': dev.log_w})
    pos = fm1log.dump_log(dev, host_ts=False)
    assert fm1log.follow_log(dev, pos, interval=0, host_ts=False, stop='never', timeout=0.05) == 'timeout'
    assert 'timeout' in capsys.readouterr().out


def test_follow_reports_a_device_that_stopped_answering(monkeypatch, capsys):
    dev = ScriptedDevice('boot\n', ['wifi: wl0 post=0\n'], die_after=2)
    monkeypatch.setattr(hbdeploy, 'hello', lambda p: {'log_w': dev.log_w})
    pos = fm1log.dump_log(dev, host_ts=False)
    assert fm1log.follow_log(dev, pos, interval=0, host_ts=False, stop='never', timeout=5) == 'lost'
    out = capsys.readouterr().out
    assert 'wifi: wl0 post=0' in out and 'stopped answering' in out


def test_cli_wl0_has_a_timeout():
    assert fm1log.build_parser().parse_args(['--wl0']).timeout == 30
