#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Origin: isod89/sloop-fm1 tools/fm1_install.py @ v2.8 (b77bf55e)
#         https://github.com/isod89/sloop-fm1/blob/b77bf55e3bf804665b8f5dffb5955bd8443311f0/tools/fm1_install.py
"""Shared FM-1 MIDI device discovery and 7-bit SysEx encoding.

Used by hbdeploy (fm1-dock protocol), fm1_install (OTA update protocol),
and fm1_settings_reset (factory reset command).
"""
import re

# "Core FM1" stays: devices flashed before the modd rebrand still enumerate
# their update loader as "Core FM1 Update".
FM1_NAMES = ["modd", "Core FM1", "Felucca", "FM1-DOCK", "HARBOR", "FM-1", "Melodee"]

FM1_PORT_RE = re.compile(
    r"core.fm1|fm-1|felucca|melodee|fm1-dock|harbor|ota|composite|sinco|usb-midi", re.I)


def find_port(ports, hint=None):
    """Find an FM-1 in a list of MIDI port name strings.

    Returns the index into `ports`, or None.  With `hint`, matches only that
    substring; without, tries each name in FM1_NAMES in priority order.
    """
    targets = [hint] if hint else FM1_NAMES
    for t in targets:
        lo = t.lower()
        for i, p in enumerate(ports):
            if lo in p.lower():
                return i
    return None


def pack7(data):
    """8-to-7 bit encoding, LSB first (JieLi ota_pack7 / fm1-dock frame format)."""
    out, acc, nb = bytearray(), 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(data):
    """7-to-8 bit decoding, inverse of pack7."""
    out, acc, nb = bytearray(), 0, 0
    for b in data:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)
