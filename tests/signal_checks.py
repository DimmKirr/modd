#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Signal helpers for the audio and LCD checks: note names, RMS, tone power, WAV and RGB565 conversion. Stdlib only."""
import array
import cmath
import math
import struct
from pathlib import Path

NOTE_NAMES = {'c': 0, 'c#': 1, 'db': 1, 'd': 2, 'd#': 3, 'eb': 3, 'e': 4, 'f': 5, 'f#': 6,
              'gb': 6, 'g': 7, 'g#': 8, 'ab': 8, 'a': 9, 'a#': 10, 'bb': 10, 'b': 11}


def midi_note(name):
    """'C4' -> 60, 'f#3' -> 54; an int passes through."""
    if isinstance(name, int):
        return name
    n = name.strip().lower()
    pitch, octave = n[:-1], int(n[-1])
    return 12 * (octave + 1) + NOTE_NAMES[pitch]


def note_hz(note):
    return 440.0 * 2 ** ((midi_note(note) - 69) / 12)


def rms(samples):
    """RMS of int16 samples, 0..1 of full scale."""
    if not samples:
        return 0.0
    return math.sqrt(sum(s * s for s in samples) / len(samples)) / 32768.0


def tone_power(samples, hz, rate=44100):
    """Magnitude of one frequency (Goertzel-style DFT bin), 0..1 of full scale."""
    if not samples:
        return 0.0
    w = -2j * math.pi * hz / rate
    acc = sum(s * cmath.exp(w * i) for i, s in enumerate(samples))
    return 2 * abs(acc) / len(samples) / 32768.0


def write_wav(path, samples, rate=44100):
    """Mono 16-bit WAV, for the results directory."""
    data = array.array('h', samples).tobytes()
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16))
        f.write(b'data' + struct.pack('<I', len(data)) + data)
    return path


def rgb565_to_rgb(raw, width=240, height=240):
    """Little-endian RGB565 bytes -> packed RGB888 bytes."""
    px = array.array('H', raw[:width * height * 2])
    out = bytearray(width * height * 3)
    for i, v in enumerate(px):
        out[3 * i] = ((v >> 11) & 0x1F) * 255 // 31
        out[3 * i + 1] = ((v >> 5) & 0x3F) * 255 // 63
        out[3 * i + 2] = (v & 0x1F) * 255 // 31
    return bytes(out)
