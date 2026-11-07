#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Reset FM1 settings to factory defaults over USB MIDI SysEx.

Sends F0 7D 46 4C 7F F7. The firmware erases saved settings,
writes defaults, and reboots.
"""
import sys

import mido

from fm1_midi import find_port

RESET_CMD = [0xF0, 0x7D, 0x46, 0x4C, 0x7F, 0xF7]


def main():
    hint = sys.argv[1] if len(sys.argv) > 1 else None
    ports = list(dict.fromkeys(mido.get_output_names()))
    idx = find_port(ports, hint)
    if idx is None:
        print(f"No FM-1 MIDI port found{f' matching {hint!r}' if hint else ''}. Available:")
        for p in ports:
            print(f"  {p}")
        sys.exit(1)

    port_name = ports[idx]
    print(f"Using: {port_name}")
    print("Sending settings reset command...")
    with mido.open_output(port_name) as port:
        port.send(mido.Message("sysex", data=RESET_CMD[1:-1]))
    print("Sent. Device will reboot with default settings.")


if __name__ == "__main__":
    main()
