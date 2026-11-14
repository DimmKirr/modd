#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

"""LLM-driven melody generator for the FM-1 step sequencer.

UI page flow:
  1. Select instrument  (solo | bass | strings | drum)
  2. Select decade      (1970s..2020s)  -> fetches 10 artists from LLM
  3. Select artist      (scroll loads next 10)
  4. Press "Generate"   -> returns step_t-compatible JSON + optional MIDI

Library usage:
    from melody_generator import generate, get_artists, INSTRUMENTS
    from artist_inspiration import list_decades

    artists = get_artists(decade="1980s")        # cached LLM call
    pattern = generate("solo", artist=artists[0]) # returns JSON string

CLI:
    python tools/llm/melody_generator.py --inst solo --decade 1980s
    python tools/llm/melody_generator.py --inst drum --decade 1990s --midi out.mid
    python tools/llm/melody_generator.py --list
"""

from __future__ import annotations

import json
import re

from artist_inspiration import fetch_artists, list_decades

# -- Output format specification --

ABC_FORMAT = """\
Output ONLY valid ABC notation. No text, markdown, or explanation outside the ABC.

Required headers (one per line):
X:1
T:<title>
M:4/4
L:1/8
Q:1/4=<bpm>
K:<key>
%%quantize:<0.0-1.0>

%%quantize is a standard ABC %% directive. It controls rhythmic grid strictness:
%%quantize:1.0 for industrial/electronic, %%quantize:0.6 for folk/ballads, %%quantize:0.4 for jazz/film scores.

Then the melody on one or more lines.
Use standard ABC: C-B = octave 4, c-b = octave 5, C, = octave 3, c' = octave 6.
Use ^ for sharp, _ for flat. Use z for rests.
Duration: A=1x base, A2=2x, A/2=half, A3=3x. With L:1/8, A=eighth, A2=quarter, A4=half.
Use !f! for loud, !p! for soft, > for accent.
Use VARIED note durations for realistic rhythm. Mix eighths, quarters, halves, and dotted notes.
Allowed durations: A/2=16th, A=8th, A3/2=dotted-8th, A2=quarter, A3=dotted-quarter, A4=half, A8=whole.
Do NOT use odd multipliers beyond 3: no A5, A7.
Keep melodies simple, memorable, singable. Real musical phrases with breathing room."""

STEP_FORMAT = """\
Output EXACTLY this JSON, nothing else:
{"bpm":<int>,"len":<1-64>,"steps":[<step>,...]}

Each <step> is one of:
- 0                     rest (silence)
- <midi_note>           single note, default velocity 96
- [<note>,<vel>]        single note with velocity 1-127
- [<note>,<note>,...]   chord (up to 4 notes), default velocity
- [[<n>,<n>,...],<vel>] chord with velocity

MIDI note range: 36-96. Middle C = 60. Velocity: 1-127 (96=normal, 127=accent).
Maximum 64 steps. Use 16 or 32 steps for typical patterns.
No text outside the JSON. No markdown. No explanation."""

# -- Instruments (fixed by hardware) --

INSTRUMENTS = {
    "solo": {
        "name": "Solo Lead",
        "desc": "Monophonic melody line. Single notes only, no chords.",
        "rules": "Use single notes (no arrays). Add rests for rhythmic interest. "
                 "Keep within one octave range. Velocity accents on downbeats.",
    },
    "bass": {
        "name": "Bass",
        "desc": "Bassline pattern. Low register, rhythmic.",
        "rules": "Notes in range 36-60 only. Prefer root/fifth movement. "
                 "Add rests for groove. Repeat short motifs. No chords.",
    },
    "strings": {
        "name": "Strings / Pad",
        "desc": "Sustained chords. 3-4 note voicings, slow movement.",
        "rules": "Use chord arrays [note,note,note] with 3-4 notes. "
                 "Hold each chord for 4-8 steps (repeat same chord). "
                 "Notes in range 48-84. Voice-lead smoothly between chords.",
    },
    "drum": {
        "name": "Drum Pattern",
        "desc": "Drum machine pattern using fixed MIDI mapping.",
        "rules": "Use these MIDI notes: 36=kick, 38=snare, 42=closed hat, "
                 "46=open hat, 49=crash, 51=ride. Use chords for simultaneous hits "
                 "(e.g. [36,42] = kick+hat). Velocity 96=normal, 127=accent, 64=ghost.",
    },
}

DEFAULT_MODEL = "deepseek/deepseek-v4.1-flash-1017"

# -- ABC notation parser --

_ABC_SEMI = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}

_KEY_ACC = {
    'C': {}, 'G': {'F': 1}, 'D': {'F': 1, 'C': 1},
    'A': {'F': 1, 'C': 1, 'G': 1}, 'E': {'F': 1, 'C': 1, 'G': 1, 'D': 1},
    'B': {'F': 1, 'C': 1, 'G': 1, 'D': 1, 'A': 1},
    'F': {'B': -1}, 'Bb': {'B': -1, 'E': -1},
    'Eb': {'B': -1, 'E': -1, 'A': -1},
    'Ab': {'B': -1, 'E': -1, 'A': -1, 'D': -1},
    'Am': {}, 'Em': {'F': 1}, 'Bm': {'F': 1, 'C': 1},
    'Dm': {'B': -1}, 'Gm': {'B': -1, 'E': -1},
    'Cm': {'B': -1, 'E': -1, 'A': -1},
    'Fm': {'B': -1, 'E': -1, 'A': -1, 'D': -1},
    'Bbm': {'B': -1, 'E': -1, 'A': -1, 'D': -1, 'G': -1},
    'F#m': {'F': 1, 'C': 1, 'G': 1},
    'C#m': {'F': 1, 'C': 1, 'G': 1, 'D': 1},
}

_DYN_VEL = {'ppp': 24, 'pp': 40, 'p': 56, 'mp': 72, 'mf': 88,
            'f': 104, 'ff': 120, 'fff': 127}


def _abc_duration(text: str, pos: int, base: float) -> tuple[float, int]:
    """Parse ABC duration modifier. Returns (beats, new_pos)."""
    dur = base
    i = pos
    if i < len(text) and text[i].isdigit():
        n = ''
        while i < len(text) and text[i].isdigit():
            n += text[i]; i += 1
        dur *= int(n)
    if i < len(text) and text[i] == '/':
        i += 1
        if i < len(text) and text[i].isdigit():
            n = ''
            while i < len(text) and text[i].isdigit():
                n += text[i]; i += 1
            dur /= int(n)
        else:
            dur /= 2
    return dur, i


def parse_abc(text: str, quantize: float = 1.0) -> dict:
    """Parse ABC notation into {bpm, events: [{pitch, vel, dur}, ...]}."""
    text = text.strip()
    if text.startswith("```"):
        lines = text.split("\n")
        text = "\n".join(l for l in lines if not l.startswith("```")).strip()

    headers = {}
    body_parts = []
    llm_quantize = None
    for line in text.split('\n'):
        line = line.strip()
        if not line:
            continue
        m_q = re.match(r'%%quantize[:\s]\s*([\d.]+)$', line)
        if m_q:
            val = float(m_q.group(1))
            if 0.0 <= val <= 1.0:
                llm_quantize = val
            continue
        if line.startswith('%'):
            continue
        if len(line) > 1 and line[1] == ':' and line[0] in 'XTMQLKNRSHwW':
            headers[line[0]] = line[2:].strip()
        else:
            body_parts.append(line)

    bpm = 120
    q = headers.get('Q', '')
    m = re.search(r'=\s*(\d+)', q) or re.search(r'^(\d+)$', q)
    if m:
        bpm = int(m.group(1))

    base_dur = 0.5  # L:1/8 default (in beats: 0.5 = eighth note)
    l_field = headers.get('L', '')
    lm = re.match(r'(\d+)/(\d+)', l_field)
    if lm:
        base_dur = int(lm.group(1)) / int(lm.group(2)) * 4

    key_str = headers.get('K', 'C').strip()
    key_clean = re.match(r'([A-G][b#]?m?)', key_str)
    key_name = key_clean.group(1) if key_clean else 'C'
    key_acc = dict(_KEY_ACC.get(key_name, {}))

    if llm_quantize is not None:
        quantize = llm_quantize

    body = ' '.join(body_parts)
    body = re.sub(r'\[1[^]]*\]|\[2[^]]*\]', '', body)

    events = []
    vel = 96
    bar_acc = {}
    i = 0

    while i < len(body):
        c = body[i]

        if c in ' \t\n':
            i += 1; continue

        if c in '|:':
            bar_acc = {}
            i += 1; continue

        if c in '[](){}~.':
            i += 1; continue

        if c == '"':
            i += 1
            while i < len(body) and body[i] != '"':
                i += 1
            if i < len(body): i += 1
            continue

        if c == '!':
            end = body.find('!', i + 1)
            if end > i:
                dyn = body[i+1:end].lower()
                if dyn in _DYN_VEL:
                    vel = _DYN_VEL[dyn]
                elif dyn == 'accent':
                    vel = min(vel + 24, 127)
                i = end + 1
                continue
            i += 1; continue

        if c == '>':
            if events:
                events[-1]["vel"] = min(events[-1]["vel"] + 24, 127)
            i += 1; continue

        if c in 'zZ':
            i += 1
            dur, i = _abc_duration(body, i, base_dur)
            events.append({"pitch": 0, "vel": 0, "dur": dur})
            continue

        if c == '-':
            i += 1; continue

        acc = None
        if c == '^':
            acc = 1; i += 1
            if i < len(body) and body[i] == '^': acc = 2; i += 1
        elif c == '_':
            acc = -1; i += 1
            if i < len(body) and body[i] == '_': acc = -2; i += 1
        elif c == '=':
            acc = 0; i += 1

        if i >= len(body):
            break
        c = body[i]

        if c.upper() not in _ABC_SEMI:
            i += 1; continue

        note_name = c.upper()
        octave = 5 if c.islower() else 4
        i += 1

        while i < len(body):
            if body[i] == "'": octave += 1; i += 1
            elif body[i] == ",": octave -= 1; i += 1
            else: break

        if acc is not None:
            bar_acc[note_name] = acc
            semi_adj = acc
        elif note_name in bar_acc:
            semi_adj = bar_acc[note_name]
        elif note_name in key_acc:
            semi_adj = key_acc[note_name]
        else:
            semi_adj = 0

        midi = _ABC_SEMI[note_name] + semi_adj + (octave + 1) * 12
        midi = max(0, min(127, midi))

        dur, i = _abc_duration(body, i, base_dur)
        events.append({"pitch": midi, "vel": vel, "dur": dur})

    for e in events:
        e["dur"] = _quantize_dur(e["dur"], quantize)

    return {"bpm": bpm, "quantize": quantize, "events": events}


_GRID = [0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0]


def _quantize_dur(dur: float, strength: float = 1.0) -> float:
    """Snap duration toward nearest grid value.

    strength=1.0 snaps fully (default), 0.0 keeps raw duration,
    values in between blend for a human feel.
    """
    best = _GRID[0]
    best_dist = abs(dur - best)
    for g in _GRID[1:]:
        d = abs(dur - g)
        if d < best_dist:
            best = g
            best_dist = d
    if strength >= 1.0:
        return best
    if strength <= 0.0:
        return dur
    return dur + (best - dur) * strength


# -- Artist loading (delegates to artist_inspiration) --


def get_artists(decade: str | None = None, count: int = 10,
                offset: int = 0, genre: str = "electronic",
                model: str = DEFAULT_MODEL,
                api_key: str | None = None) -> list[dict]:
    """Load artists for a decade. Supports pagination via offset.

    First call fetches count artists. If offset > 0, fetches offset+count
    and returns the slice starting at offset.
    """
    total = offset + count
    all_artists = fetch_artists(total, genre, decade, model, api_key)
    return all_artists[offset:offset + count]


# -- Prompt building --


def build_system_prompt(fmt: str = "abc") -> str:
    if fmt == "abc":
        return (
            "You are a melody generator for an FM synthesizer. "
            "You output ONLY valid ABC notation. "
            "Never include text, markdown, code fences, or explanation.\n\n"
            + ABC_FORMAT
        )
    return (
        "You are a pattern generator for an FM synthesizer's step sequencer. "
        "You output ONLY valid JSON in the exact format specified. "
        "Never include text, markdown, code fences, or explanation. "
        "The JSON must parse in one pass with no trailing content.\n\n"
        + STEP_FORMAT
    )


def build_user_prompt(instrument: str, artist: dict | None = None,
                      key: str = "C minor", steps: int = 16,
                      fmt: str = "abc") -> str:
    """Build the user prompt. artist is a dict with name, track, style keys."""
    inst = INSTRUMENTS.get(instrument, INSTRUMENTS["solo"])
    if fmt == "abc":
        parts = [
            f"Write the MAIN MELODY of a {inst['name'].lower()} part in {key}.",
            f"About {steps * 0.5:.0f} beats long (adjust naturally for the music).",
            f"Instrument character: {inst['desc']}",
        ]
    else:
        parts = [
            f"Generate a {steps}-step {inst['name'].lower()} pattern in {key}.",
            f"Instrument: {inst['desc']}",
            f"Rules: {inst['rules']}",
        ]
    if artist:
        name = artist.get("name", "")
        track = artist.get("track", "")
        style = artist.get("style", "")
        parts.append(f"Style: inspired by {name} ({track}). {style}")
    return " ".join(parts)


def list_instruments() -> list[dict]:
    return [{"id": k, "name": v["name"]} for k, v in INSTRUMENTS.items()]


# -- Step parsing + MIDI --


def parse_step(step):
    """Normalize one step from the LLM JSON into [(note, vel), ...] or None."""
    if step == 0:
        return None
    if isinstance(step, int):
        return [(step, 96)]
    if isinstance(step, list):
        if len(step) == 2 and isinstance(step[0], int) and isinstance(step[1], int):
            if step[1] <= 127:
                return [(step[0], step[1])]
            return [(step[0], 96), (step[1], 96)]
        if len(step) >= 2 and isinstance(step[0], list):
            vel = step[1] if isinstance(step[1], int) else 96
            return [(n, vel) for n in step[0]]
        return [(n, 96) for n in step if isinstance(n, int)]
    return None


def to_midi(data: dict, out_path: str, instrument: str = "solo"):
    """Write a pattern as a Standard MIDI File. Accepts ABC events or legacy steps."""
    from midiutil import MIDIFile

    bpm = data.get("bpm", 120)
    is_drum = instrument == "drum"

    midi = MIDIFile(1, deinterleave=False)
    midi.addTempo(0, 0, bpm)
    if is_drum:
        midi.addProgramChange(0, 9, 0, 0)
    channel = 9 if is_drum else 0

    if "events" in data:
        t = 0.0
        for ev in data["events"]:
            pitch = ev["pitch"]
            vel = ev.get("vel", 96)
            dur = ev.get("dur", 1.0)
            if pitch > 0 and vel > 0:
                midi.addNote(0, channel, pitch, t, dur * 0.95, vel)
            t += dur
    elif "steps" in data:
        step_dur = 0.25
        for i, step in enumerate(data["steps"]):
            notes = parse_step(step)
            if not notes:
                continue
            t = i * step_dur
            for note, vel in notes:
                midi.addNote(0, channel, note, t, step_dur * 0.9, vel)

    with open(out_path, "wb") as f:
        midi.writeFile(f)


# -- Generate (LLM call) --


def generate(instrument: str = "solo", artist: dict | None = None,
             key: str = "C minor", steps: int = 16,
             model: str = DEFAULT_MODEL, max_tokens: int = 512,
             api_key: str | None = None) -> str:
    """Generate a pattern. C builds the request and parses the response."""
    from openrouter import call

    return call(model, build_system_prompt(),
                build_user_prompt(instrument, artist, key, steps),
                max_tokens=max_tokens, api_key=api_key)


# -- CLI --


def main():
    import argparse
    import sys

    parser = argparse.ArgumentParser(
        description="Generate step-sequencer patterns via OpenRouter LLM")
    parser.add_argument("--inst", default="solo",
                        choices=list(INSTRUMENTS.keys()),
                        help="instrument type (default: solo)")
    parser.add_argument("--decade", default=None,
                        choices=list_decades(),
                        help="decade for artist discovery")
    parser.add_argument("--artist-index", type=int, default=0,
                        help="pick Nth artist from the decade list (default: 0)")
    parser.add_argument("--key", default="C minor",
                        help="musical key (default: C minor)")
    parser.add_argument("--steps", type=int, default=16,
                        help="number of steps (default: 16)")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--max-tokens", type=int, default=512)
    parser.add_argument("--midi", metavar="FILE",
                        help="write output as a .mid file")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the prompt without calling the API")
    parser.add_argument("--list", action="store_true",
                        help="list instruments and decades")
    args = parser.parse_args()

    if args.list:
        print("Instruments:")
        for k, v in INSTRUMENTS.items():
            print(f"  {k:10s}  {v['name']}")
        print(f"\nDecades: {', '.join(list_decades())}")
        return

    artist = None
    if args.decade:
        print(f"Loading artists for {args.decade}...", file=sys.stderr)
        artists = get_artists(decade=args.decade)
        for i, a in enumerate(artists):
            marker = " <--" if i == args.artist_index else ""
            print(f"  {i:2d}. {a['name']}{marker}", file=sys.stderr)
        if 0 <= args.artist_index < len(artists):
            artist = artists[args.artist_index]

    if args.dry_run:
        print("=== SYSTEM ===")
        print(build_system_prompt())
        print("\n=== USER ===")
        print(build_user_prompt(args.inst, artist, args.key, args.steps))
        return

    label = artist["name"] if artist else "no artist"
    print(f"Generating {args.steps}-step {args.inst} pattern"
          f" ({label}) in {args.key}...", file=sys.stderr)

    text = generate(args.inst, artist, args.key, args.steps,
                    args.model, args.max_tokens)
    print(text)

    try:
        data = json.loads(text)
        n = len(data.get("steps", []))
        print(f"\n--- Parsed: {n} steps, bpm={data.get('bpm', '?')}",
              file=sys.stderr)
        if args.midi:
            to_midi(data, args.midi, args.inst)
            print(f"--- Wrote {args.midi}", file=sys.stderr)
    except json.JSONDecodeError:
        print("\n--- Warning: response is not valid JSON", file=sys.stderr)


if __name__ == "__main__":
    main()
