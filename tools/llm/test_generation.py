#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

"""Fixed-input melody generation test.

All cases are solo melody (main theme). Each case runs across multiple
models via the full C firmware path: C builds request, C sends HTTP POST
to the local proxy, proxy forwards HTTPS to OpenRouter, C parses response.

Output: tests/results/<dateTimeISO>-llm-gen/<case>/<model_slug>.mid

Usage:
    python tools/llm/test_generation.py                # all cases, all models
    python tools/llm/test_generation.py --case prodigy  # one case, all models
    python tools/llm/test_generation.py --list          # list cases
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from melody_generator import (
    INSTRUMENTS,
    build_system_prompt,
    parse_abc,
    to_midi,
)
from openrouter import Proxy, generate

RESULTS_DIR = Path(__file__).resolve().parents[2] / "tests" / "results"

MODELS = [
    ("deepseek/deepseek-v4.1-flash-1017", "none",   "deepseek-none"),
    ("deepseek/deepseek-v4.1-flash-1017", "medium", "deepseek-medium"),
    ("deepseek/deepseek-v4.1-flash-1017", "high",   "deepseek-high"),
    ("z-ai/glm-5.3-flash",               "medium", "glm53-medium"),
    ("z-ai/glm-5.3-flash",               "high",   "glm53-high"),
    ("google/gemini-2.5-flash-lite",      "medium", "gemini-lite-medium"),
    ("google/gemini-2.5-flash-lite",      "high",   "gemini-lite-high"),
]

# -- All solo melody test cases: main theme of iconic tracks --

CASES = {
    "prodigy": {
        "year": 1997,
        "artist": "The Prodigy",
        "song": "Firestarter",
        "key": "E minor",
        "steps": 16,
        "style": "Aggressive, distorted synth riff. Staccato 16th-note motif "
                 "with octave jumps. 145 bpm. Repeating 4-note phrase: "
                 "E4-G4-A4-E5 with sharp accents. Punk energy, raw, "
                 "relentless. Notes in E3-E5 range (52-76). No legato.",
    },
    "kraftwerk": {
        "year": 1978,
        "artist": "Kraftwerk",
        "song": "The Robots",
        "key": "C minor",
        "steps": 16,
        "style": "Cold, precise, robotic melody. Simple repeating motif. "
                 "120 bpm. 4-note phrases separated by rests. "
                 "Stepwise motion in C minor pentatonic. "
                 "Notes in C4-C5 range (60-72). Uniform velocity, "
                 "mechanical, no swing. Vocoder-like phrasing.",
    },
    "daftpunk": {
        "year": 2001,
        "artist": "Daft Punk",
        "song": "Around the World",
        "key": "G minor",
        "steps": 16,
        "style": "Catchy, repetitive synth hook. Funky 16th-note pattern "
                 "with syncopation. 120 bpm. Short melodic cell that loops. "
                 "Notes in G3-D5 range (55-74). Accents on off-beats. "
                 "French house: filtered, groovy, hypnotic repetition.",
    },
    "boc": {
        "year": 2002,
        "artist": "Boards of Canada",
        "song": "Roygbiv",
        "key": "A major",
        "steps": 32,
        "style": "Warm, nostalgic, childlike melody. Pentatonic (A-B-C#-E-F#). "
                 "85 bpm. Simple motif repeated with small variations. "
                 "Long rests between phrases. Notes in A4-A5 range (69-81). "
                 "Soft velocity 70-90, no harsh accents. Dreamy, lo-fi.",
    },
    "vangelis": {
        "year": 1982,
        "artist": "Vangelis",
        "song": "Blade Runner Blues",
        "key": "Bb minor",
        "steps": 16,
        "style": "Cinematic, soaring solo melody over sustained pads. "
                 "Slow and expressive, 65 bpm. Long notes with few rests. "
                 "Bb minor scale, wide intervals (4ths, 5ths). "
                 "Notes in Bb4-Bb5 range (70-82). Crescendo accents "
                 "on high notes. Mournful, yearning, spacious.",
    },
    "chemical": {
        "year": 1999,
        "artist": "The Chemical Brothers",
        "song": "Star Guitar",
        "key": "F major",
        "steps": 32,
        "style": "Euphoric, building melodic sequence. Arpeggiated pattern "
                 "that rises over 8 steps then resets. 125 bpm. "
                 "F major scale, stepwise ascending runs. "
                 "Notes in F4-F5 range (65-77). Velocity builds from "
                 "80 to 120 across the phrase. Trance-like, optimistic.",
    },
    "pain": {
        "year": 2002,
        "artist": "Pain",
        "song": "Shut Your Mouth",
        "key": "D minor",
        "steps": 16,
        "style": "Heavy industrial metal synth riff. Driving, aggressive "
                 "16th-note pattern. 140 bpm. Power chord stabs with "
                 "palm-mute rhythm. D minor, chromatic passing tones. "
                 "Notes in D3-D5 range (50-74). Hard velocity accents "
                 "on beats 1 and 3. Peter Tagtgren production: massive, "
                 "distorted, relentless groove.",
    },
    "mannelig": {
        "year": 1999,
        "artist": "In Extremo",
        "song": "Herr Mannelig",
        "key": "A minor",
        "steps": 32,
        "style": "Medieval folk melody played on synth. Haunting, modal "
                 "minor theme. 100 bpm. A natural minor / Aeolian mode. "
                 "Stepwise motion with occasional 4th leaps. Long phrases "
                 "with breathing rests. Notes in A3-A5 range (57-81). "
                 "Moderate velocity 80-100, gentle accents on phrase starts. "
                 "Swedish folk: melancholic, dignified, ancient.",
    },
    "zevendagen": {
        "year": 1970,
        "artist": "Bots",
        "song": "Zeven Dagen Lang",
        "key": "G major",
        "steps": 16,
        "style": "Dutch folk-rock anthem melody. Singalong, upbeat, "
                 "cheerful progression. 120 bpm. Simple major-key melody "
                 "with stepwise motion. G major scale, root-based phrases. "
                 "Notes in G3-G5 range (55-79). Consistent velocity ~96, "
                 "accent on downbeats. Festive, communal, pub-song energy. "
                 "Think accordion-led folk melody on a synth.",
    },
    "scooter": {
        "year": 1998,
        "artist": "Scooter",
        "song": "How Much Is The Fish",
        "key": "C minor",
        "steps": 16,
        "style": "Happy hardcore / eurodance synth hook. High-energy, "
                 "bouncing melody. 150 bpm. Short repeating riff with "
                 "octave jumps and staccato articulation. C minor, "
                 "pentatonic movement. Notes in C4-C6 range (60-84). "
                 "Strong accents on every beat, offbeat ghost notes. "
                 "Peak-time rave energy, euphoric, anthem-like.",
    },
}


def build_case_prompt(case: dict) -> str:
    parts = [
        f"Write the MAIN MELODY/THEME of {case['artist']} - "
        f"\"{case['song']}\" ({case['year']}) in {case['key']}.",
        f"About {case['steps'] * 0.5:.0f} beats long.",
        f"Monophonic solo lead melody, single notes only.",
        f"Style: {case['style']}",
    ]
    return " ".join(parts)


def run_one(name: str, case: dict, model_id: str, reasoning: str,
            model_slug: str, case_dir: Path, proxy_port: int) -> dict:
    """Run one generation through the full C firmware path via proxy."""
    sys_prompt = build_system_prompt()
    usr_prompt = build_case_prompt(case)

    result = {"model": model_slug, "ok": False}
    t0 = time.time()

    try:
        text = generate(model_id, sys_prompt, usr_prompt,
                        max_tokens=1024, reasoning=reasoning,
                        port=proxy_port)
        dt = time.time() - t0
    except RuntimeError as e:
        dt = time.time() - t0
        print(f"    [{model_slug}] C error ({dt:.1f}s): {e}")
        result["error"] = str(e)
        return result

    result["time"] = round(dt, 1)

    if not text or text.strip() == "":
        print(f"    [{model_slug}] empty response ({dt:.1f}s)")
        return result

    text = text.strip()

    case_dir.mkdir(parents=True, exist_ok=True)
    (case_dir / f"{model_slug}.abc").write_text(text)

    try:
        parsed = parse_abc(text)
    except Exception as e:
        print(f"    [{model_slug}] ABC parse error ({dt:.1f}s): {e}")
        result["raw"] = text[:200]
        return result

    events = parsed.get("events", [])
    note_count = sum(1 for e in events if e["pitch"] > 0)
    rest_count = sum(1 for e in events if e["pitch"] == 0)
    total_beats = sum(e["dur"] for e in events)

    if note_count == 0:
        print(f"    [{model_slug}] no notes in ABC ({dt:.1f}s)")
        result["raw"] = text[:200]
        return result

    result["ok"] = True
    result["bpm"] = parsed.get("bpm", "?")
    result["notes"] = note_count
    result["rests"] = rest_count
    result["beats"] = round(total_beats, 1)

    json_path = case_dir / f"{model_slug}.json"
    json_path.write_text(json.dumps(parsed, indent=2, default=str))

    midi_path = case_dir / f"{model_slug}.mid"
    to_midi(parsed, str(midi_path), "solo")

    result["midi"] = str(midi_path)

    dur_set = set()
    for e in events:
        if e["pitch"] > 0:
            dur_set.add(e["dur"])
    dur_str = "/".join(f"{d:.2g}" for d in sorted(dur_set))

    print(f"    [{model_slug}] bpm={result['bpm']} "
          f"{note_count}notes {rest_count}rests {total_beats:.1f}beats "
          f"durs=[{dur_str}] ({dt:.1f}s)")

    return result


def main():
    parser = argparse.ArgumentParser(
        description="Solo melody generation test across models (full C path)")
    parser.add_argument("--case", default=None, choices=list(CASES.keys()))
    parser.add_argument("--port", type=int, default=PROXY_PORT,
                        help="proxy port (default: 8484)")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.list:
        for name, c in CASES.items():
            print(f"  {name:15s}  {c['artist']:25s}  \"{c['song']}\" ({c['year']})")
        return

    cases = {args.case: CASES[args.case]} if args.case else CASES

    ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = RESULTS_DIR / f"{ts}-llm-gen"
    run_dir.mkdir(parents=True, exist_ok=True)

    print(f"Models: {len(MODELS)}")
    print(f"Cases:  {len(cases)}")
    print(f"Total:  {len(MODELS) * len(cases)} generations")
    print(f"Output: {run_dir}/")

    with Proxy(port=args.port) as proxy:
        all_results = {}

        for name, case in cases.items():
            print(f"\n{'━'*60}")
            print(f"  {case['artist']} - \"{case['song']}\" ({case['year']})")
            print(f"  solo | {case['key']} | {case['steps']} steps")
            print(f"{'━'*60}")

            case_dir = run_dir / name
            case_dir.mkdir(parents=True, exist_ok=True)

            sys_prompt = build_system_prompt()
            usr_prompt = build_case_prompt(case)
            (case_dir / "prompt.txt").write_text(
                f"SYSTEM:\n{sys_prompt}\n\nUSER:\n{usr_prompt}")

            case_results = []
            for model_id, reasoning, model_slug in MODELS:
                r = run_one(name, case, model_id, reasoning,
                            model_slug, case_dir, args.port)
                case_results.append(r)
                time.sleep(1)

            all_results[name] = case_results

    print(f"\n{'━'*60}")
    print(f"  SUMMARY")
    print(f"{'━'*60}")

    for name, results in all_results.items():
        case = cases[name]
        ok = sum(1 for r in results if r["ok"])
        print(f"\n  {case['artist']} - \"{case['song']}\" [{ok}/{len(results)} valid]")
        for r in results:
            if r["ok"]:
                print(f"    {r['model']:25s} bpm={r['bpm']:>3} "
                      f"{r['notes']}notes {r.get('beats','?')}beats {r['time']}s")
            else:
                print(f"    {r['model']:25s} FAIL")

    summary = {
        "timestamp": ts,
        "models": [{"id": m, "reasoning": r, "slug": s}
                   for m, r, s in MODELS],
        "cases": {name: results for name, results in all_results.items()},
    }
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2,
                                                      default=str))

    total_ok = sum(1 for rs in all_results.values() for r in rs if r["ok"])
    total = len(MODELS) * len(cases)
    print(f"\n  {total_ok}/{total} valid")
    print(f"  Listen: open {run_dir}/*/*.mid")


PROXY_PORT = 8484

if __name__ == "__main__":
    main()
