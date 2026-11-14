#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

"""Discover electronic music artists and their signature styles via LLM.

Library + CLI. Returns artist dicts with id, name, track, style fields
that melody_generator.py consumes directly for pattern generation.

Results are cached to .scratch/llm-cache/ so repeated queries are free.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

DEFAULT_MODEL = "deepseek/deepseek-v4.1-flash-1017"

DECADES = ["1970s", "1980s", "1990s", "2000s", "2010s", "2020s"]

CACHE_DIR = Path(__file__).resolve().parents[2] / ".scratch" / "llm-cache"

SYSTEM_PROMPT = """\
You are a music historian specializing in electronic music.
Output ONLY valid JSON, nothing else. No text, markdown, or explanation.
The JSON must parse in one pass with no trailing content."""

OUTPUT_FORMAT = """\
Output this exact JSON structure:
{"artists":[{"id":"<short_kebab_id>","name":"<Full Name>",\
"track":"<Iconic Track 1> / <Iconic Track 2>",\
"style":"<2-3 sentence style description for a synth pattern generator>"},...]}

The "id" is a short lowercase slug (e.g. "kraftwerk", "aphextwin", "boc").
The "style" should describe rhythm, melody, and texture characteristics
that a step-sequencer pattern generator can act on."""


def _cache_key(genre: str, decade: str | None, count: int) -> str:
    parts = [genre.replace(" ", "-")]
    if decade:
        parts.append(decade)
    parts.append(str(count))
    return "-".join(parts)


def _cache_path(genre: str, decade: str | None, count: int) -> Path:
    return CACHE_DIR / f"artists-{_cache_key(genre, decade, count)}.json"


def _load_cache(genre: str, decade: str | None, count: int) -> list[dict] | None:
    p = _cache_path(genre, decade, count)
    if p.exists():
        try:
            data = json.loads(p.read_text())
            if isinstance(data, list) and len(data) >= count:
                return data
        except (json.JSONDecodeError, OSError):
            pass
    return None


def _save_cache(genre: str, decade: str | None, count: int,
                artists: list[dict]):
    p = _cache_path(genre, decade, count)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(artists, indent=2))


def build_prompt(count: int = 10, genre: str = "electronic",
                 decade: str | None = None) -> str:
    parts = [f"List the top {count} most influential {genre} music artists"]
    if decade:
        parts[0] += f" of the {decade}"
    parts.append("that are known for iconic synthesizer-driven tracks.")
    parts.append("Rank by influence on electronic music production.")
    parts.append(OUTPUT_FORMAT)
    return " ".join(parts)


def fetch_artists(count: int = 10, genre: str = "electronic",
                  decade: str | None = None,
                  model: str = DEFAULT_MODEL,
                  api_key: str | None = None,
                  use_cache: bool = True) -> list[dict]:
    """Fetch artist list from LLM. Returns cached results when available."""
    if use_cache:
        cached = _load_cache(genre, decade, count)
        if cached:
            return cached

    from openrouter import call

    text = call(model, SYSTEM_PROMPT, build_prompt(count, genre, decade),
                max_tokens=min(count * 120, 2048), api_key=api_key)
    data = json.loads(text)
    artists = data.get("artists", data if isinstance(data, list) else [])

    if use_cache and artists:
        _save_cache(genre, decade, count, artists)

    return artists


def list_decades() -> list[str]:
    return list(DECADES)


def main():
    import argparse
    import sys

    parser = argparse.ArgumentParser(
        description="Discover electronic music artists via LLM")
    parser.add_argument("--count", type=int, default=10,
                        help="number of artists (default: 10)")
    parser.add_argument("--genre", default="electronic",
                        help="genre filter (default: electronic)")
    parser.add_argument("--decade", default=None,
                        choices=DECADES,
                        help="decade filter")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--no-cache", action="store_true",
                        help="skip cache, always call the API")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the prompt without calling the API")
    parser.add_argument("--json", action="store_true",
                        help="output raw JSON instead of formatted table")
    args = parser.parse_args()

    if args.dry_run:
        print("=== SYSTEM ===")
        print(SYSTEM_PROMPT)
        print("\n=== USER ===")
        print(build_prompt(args.count, args.genre, args.decade))
        return

    print(f"Fetching top {args.count} {args.genre} artists"
          f"{f' ({args.decade})' if args.decade else ''}...",
          file=sys.stderr)

    artists = fetch_artists(args.count, args.genre, args.decade,
                            args.model, use_cache=not args.no_cache)

    if args.json:
        print(json.dumps({"artists": artists}, indent=2))
    else:
        for i, a in enumerate(artists, 1):
            print(f"\n{i:2d}. {a.get('name', '?')}  [{a.get('id', '?')}]")
            print(f"    Tracks: {a.get('track', '?')}")
            print(f"    Style:  {a.get('style', '?')}")


if __name__ == "__main__":
    main()
