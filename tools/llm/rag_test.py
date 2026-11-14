#!/usr/bin/env python3
"""Quick A/B test: melody generation WITH vs WITHOUT reference ABC.

Compares LLM output quality when given a real melody as context (RAG)
vs generating from scratch.

Usage:
    python tools/llm/test_rag.py
"""

from __future__ import annotations

import json
import time
from datetime import datetime, timezone
from pathlib import Path

from melody_generator import build_system_prompt, parse_abc, to_midi
from openrouter import Proxy, generate

RESULTS_DIR = Path(__file__).resolve().parents[2] / "tests" / "results"

MODELS = [
    ("z-ai/glm-5.3-flash", "medium", "glm53"),
    ("google/gemini-2.5-flash", "medium", "gemini"),
    ("deepseek/deepseek-v4.1-flash-1017", "none", "deepseek"),
]

# Real ABC from thesession.org (Ev Chistr 'Ta Laou = Son ar Chistr)
# This is the folk tune that Scooter sampled for "How Much Is The Fish"
# and Bots used for "Zeven Dagen Lang"
REF_SON_AR_CHISTR = """\
X:1
T:Ev Chistr 'Ta Laou (Son ar Chistr)
M:4/4
L:1/8
Q:1/4=132
K:Em
A AF|G2 E2 EA AG|FA AF|G2 E2 FD|E2 z E GA|\
B2 B2 cA|B3 B AA|G2 E2 EA AG|FA AF|G2 E2 FD|E2 z||"""

# Herr Mannelig - transcribed from known folk sources (D minor)
REF_HERR_MANNELIG = """\
X:1
T:Herr Mannelig
M:3/4
L:1/8
Q:1/4=132
K:Dm
A2 BA GF|D2 EF GA|GF ED EF|D2 z2 DE|F2 FF G2|\
FG A2 z2|F3 A BA|GF ED EF|D2 z2||"""

# Pain - Shut Your Mouth (2002, Peter Tägtgren) - intro synth hook
# Transcribed from MIDI (midifind.com #47107, track 1 "Keys 1")
REF_SHUT_YOUR_MOUTH = """\
X:1
T:Shut Your Mouth (Intro Hook)
M:4/4
L:1/8
Q:1/4=140
K:Bm
Bz^fBgz^fe|^fze^fgg^fe|Bz^fBgz^fe|dzed^c^cd^c||"""

# Axel F - Harold Faltermeyer (1984) - main synth hook
# Transcribed from MIDI (bitmidi.com Axel-F-1.mid, track 7 "SAW WAVE")
REF_AXEL_F = """\
X:1
T:Axel F (Main Synth Hook)
M:4/4
L:1/16
Q:1/4=118
K:Fm
F4A3F2FB2F2E2|F4c3F2Fd2c2A2|F2c2f2FE2EC2G2z2|F6z8z2||"""

# Back to the Future - Alan Silvestri (1985) - main brass fanfare
# Transcribed from MIDI (bitmidi.com back_to_the_future.mid, track 1 "BrassSect")
REF_BTTF = """\
X:1
T:Back to the Future (Main Fanfare)
M:4/4
L:1/16
Q:1/4=120
K:C
C4G6c2_B6|zAGA3G3F3G8|z8z2||"""

CASES = {
    "scooter": {
        "prompt": (
            "Write the MAIN SYNTH HOOK of Scooter - \"How Much Is The Fish\" (1998) "
            "in E minor. About 8 beats long. Monophonic solo lead melody. "
            "Style: Happy hardcore / eurodance. High-energy bouncing melody. "
            "150 bpm. Based on the Breton folk tune \"Son ar Chistr\"."
        ),
        "reference": REF_SON_AR_CHISTR,
        "bpm_expect": 150,
        "quantize": 0.8,
    },
    "mannelig": {
        "prompt": (
            "Write the MAIN VOCAL MELODY of In Extremo - \"Herr Mannelig\" (1999) "
            "in D minor. About 16 beats long. Monophonic solo melody. "
            "Style: Medieval folk metal. Haunting modal minor theme. "
            "132 bpm. Swedish folk song."
        ),
        "reference": REF_HERR_MANNELIG,
        "bpm_expect": 132,
        "quantize": 0.6,
    },
    "zevendagen": {
        "prompt": (
            "Write the MAIN MELODY of Bots - \"Zeven Dagen Lang\" (1970) "
            "in E minor. About 8 beats long. Monophonic melody. "
            "Style: Dutch folk-rock anthem. Singalong, upbeat, cheerful. "
            "120 bpm. Based on the Breton folk tune \"Son ar Chistr\"."
        ),
        "reference": REF_SON_AR_CHISTR,
        "bpm_expect": 120,
        "quantize": 0.8,
    },
    "pain": {
        "prompt": (
            "Write the INTRO SYNTH HOOK of Pain - \"Shut Your Mouth\" (2002) "
            "in B minor. About 16 beats long. Monophonic synth melody. "
            "Style: Industrial metal. Staccato eighth notes with gaps. "
            "140 bpm."
        ),
        "reference": REF_SHUT_YOUR_MOUTH,
        "bpm_expect": 140,
        "quantize": 1.0,
    },
    "axelf": {
        "prompt": (
            "Write the MAIN SYNTH HOOK of Harold Faltermeyer - \"Axel F\" (1984) "
            "in F minor. About 16 beats long. Monophonic synth melody. "
            "Style: Synth-pop / electro-funk. Bouncy staccato lead. "
            "118 bpm. Beverly Hills Cop theme."
        ),
        "reference": REF_AXEL_F,
        "bpm_expect": 118,
        "quantize": 0.8,
    },
    "bttf": {
        "prompt": (
            "Write the MAIN BRASS FANFARE of Alan Silvestri - \"Back to the Future\" (1985) "
            "in C major. About 12 beats long. Monophonic brass melody. "
            "Style: Heroic film score. Triumphant rising fanfare with triplet feel. "
            "120 bpm."
        ),
        "reference": REF_BTTF,
        "bpm_expect": 120,
        "quantize": 0.4,
    },
}


def build_rag_prompt(case: dict) -> str:
    """Build the RAG user prompt that injects a reference melody."""
    return (
        f"Here is the reference melody to reproduce:\n"
        f"{case['reference']}\n\n"
        f"{case['prompt']}\n"
        f"Reproduce the reference melody's pitches and rhythm faithfully. "
        f"Only change the tempo (Q: header) to match the target BPM. "
        f"Keep the rests (z) at phrase boundaries as shown in the reference "
        f"for natural breathing room between phrases."
    )


def run_one(name: str, case: dict, with_ref: bool,
            model_id: str, reasoning: str, model_slug: str,
            port: int, out_dir: Path) -> dict:
    sys_prompt = build_system_prompt("abc")

    if with_ref:
        user_prompt = build_rag_prompt(case)
        tag = f"rag-{model_slug}"
    else:
        user_prompt = case["prompt"]
        tag = f"noref-{model_slug}"

    t0 = time.time()
    try:
        text = generate(model_id, sys_prompt, user_prompt,
                        max_tokens=1024, reasoning=reasoning, port=port)
        dt = time.time() - t0
    except RuntimeError as e:
        dt = time.time() - t0
        print(f"    [{tag}] error ({dt:.1f}s): {e}")
        return {"ok": False, "tag": tag}

    if not text or not text.strip():
        print(f"    [{tag}] empty ({dt:.1f}s)")
        return {"ok": False, "tag": tag}

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{tag}.abc").write_text(text.strip())

    case_q = case.get("quantize", 1.0)
    try:
        parsed = parse_abc(text, quantize=case_q)
    except Exception as e:
        print(f"    [{tag}] parse error ({dt:.1f}s): {e}")
        return {"ok": False, "tag": tag}

    events = parsed.get("events", [])
    notes = [e for e in events if e["pitch"] > 0]
    if not notes:
        print(f"    [{tag}] no notes ({dt:.1f}s)")
        return {"ok": False, "tag": tag}

    (out_dir / f"{tag}.json").write_text(json.dumps(parsed, indent=2))
    to_midi(parsed, str(out_dir / f"{tag}.mid"), "solo")

    durs = sorted(set(e["dur"] for e in notes))
    pitches = [e["pitch"] for e in notes]
    total_beats = sum(e["dur"] for e in events)

    q_used = parsed.get("quantize", case_q)
    print(f"    [{tag}] bpm={parsed['bpm']} q={q_used:.1f} {len(notes)}notes "
          f"{total_beats:.1f}beats range={min(pitches)}-{max(pitches)} "
          f"durs={durs} ({dt:.1f}s)")

    return {
        "ok": True, "tag": tag, "bpm": parsed["bpm"],
        "quantize": q_used,
        "notes": len(notes), "beats": total_beats,
        "pitch_range": (min(pitches), max(pitches)),
        "durations": durs, "time": round(dt, 1),
    }


def main():
    ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = RESULTS_DIR / f"{ts}-rag-ab"
    model_names = ", ".join(s for _, _, s in MODELS)
    print(f"A/B test: RAG melody generation")
    print(f"Models: {model_names}")
    print(f"Output: {run_dir}/\n")

    with Proxy() as proxy:
        for name, case in CASES.items():
            print(f"{'━'*50}")
            print(f"  {name}")
            print(f"{'━'*50}")

            case_dir = run_dir / name
            case_dir.mkdir(parents=True, exist_ok=True)

            for model_id, reasoning, model_slug in MODELS:
                r_noref = run_one(name, case, False,
                                  model_id, reasoning, model_slug,
                                  proxy.port, case_dir)
                time.sleep(1)
                r_rag = run_one(name, case, True,
                                model_id, reasoning, model_slug,
                                proxy.port, case_dir)
                time.sleep(1)

    print(f"\n{'━'*50}")
    print(f"Listen and compare:")
    print(f"  {run_dir}/<case>/noref-<model>.mid  (no reference)")
    print(f"  {run_dir}/<case>/rag-<model>.mid    (with reference ABC)")


if __name__ == "__main__":
    main()
