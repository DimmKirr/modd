#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify RAG melody quality: pauses, rhythm, tempo, distinctness.

Tests validate:
1. Reference ABCs contain rests at phrase boundaries
2. RAG prompt allows breathing rests
3. Distinct songs use distinct references
4. Reference rhythm variety, quantize detection

Checks on generated LLM output live in tools/llm/rag_results_check.py.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

TOOLS_LLM = Path(__file__).resolve().parents[2] / "tools" / "llm"
sys.path.insert(0, str(TOOLS_LLM))

from melody_generator import parse_abc  # noqa: E402

def _load_refs():
    from rag_test import (
        REF_AXEL_F, REF_BTTF, REF_HERR_MANNELIG, REF_SHUT_YOUR_MOUTH,
        REF_SON_AR_CHISTR,
    )
    return {
        "son_ar_chistr": REF_SON_AR_CHISTR,
        "herr_mannelig": REF_HERR_MANNELIG,
        "shut_your_mouth": REF_SHUT_YOUR_MOUTH,
        "axel_f": REF_AXEL_F,
        "bttf": REF_BTTF,
    }


# -- Test 1: reference ABCs must contain rests --


class TestReferenceABCsHaveRests:
    """Reference melodies injected into the RAG prompt must include
    phrase-boundary rests (z tokens)."""

    @pytest.fixture(params=["son_ar_chistr", "herr_mannelig", "shut_your_mouth", "axel_f", "bttf"])
    def ref_name(self, request):
        return request.param

    def test_abc_text_has_z(self, ref_name):
        abc = _load_refs()[ref_name]
        body = abc.split("K:")[1] if "K:" in abc else abc
        assert "z" in body.lower(), (
            f"{ref_name}: ABC body has no rests (z). "
            "Add z or z2 at phrase boundaries for breathing room."
        )

    def test_parsed_has_rest_events(self, ref_name):
        abc = _load_refs()[ref_name]
        parsed = parse_abc(abc)
        rests = [e for e in parsed["events"] if e["pitch"] == 0]
        assert len(rests) >= 1, (
            f"{ref_name}: parsed melody has {len(rests)} rests, need >= 1."
        )


# -- Test 2: RAG prompt must allow breathing rests --


class TestRAGPromptAllowsPauses:
    """The RAG user prompt must not forbid rests and must mention breathing."""

    def _get_prompt(self):
        from rag_test import CASES, build_rag_prompt
        return build_rag_prompt(CASES["mannelig"])

    def test_no_forbid_adding(self):
        prompt = self._get_prompt()
        assert "do not improvise, add, remove" not in prompt.lower(), (
            "RAG prompt forbids adding rests. Rephrase to allow "
            "phrase-boundary pauses while keeping pitches faithful."
        )

    def test_mentions_breathing_or_rests(self):
        prompt = self._get_prompt()
        has_breathing = any(
            word in prompt.lower()
            for word in ["breathing", "rest", "pause"]
        )
        assert has_breathing, (
            "RAG prompt does not mention breathing, rests, or pauses."
        )


# -- Test 3: distinct songs use distinct references --


class TestDistinctReferences:
    """Songs not derived from the same folk tune must have different
    reference melodies to avoid identical output."""

    def test_pain_differs_from_son_ar_chistr(self):
        refs = _load_refs()
        pain_events = parse_abc(refs["shut_your_mouth"])["events"]
        folk_events = parse_abc(refs["son_ar_chistr"])["events"]

        pain_pitches = [e["pitch"] for e in pain_events if e["pitch"] > 0]
        folk_pitches = [e["pitch"] for e in folk_events if e["pitch"] > 0]
        assert pain_pitches != folk_pitches, (
            "Pain and Son ar Chistr references produce identical pitch sequences."
        )

    def test_mannelig_differs_from_son_ar_chistr(self):
        refs = _load_refs()
        mann_events = parse_abc(refs["herr_mannelig"])["events"]
        folk_events = parse_abc(refs["son_ar_chistr"])["events"]

        mann_pitches = [e["pitch"] for e in mann_events if e["pitch"] > 0]
        folk_pitches = [e["pitch"] for e in folk_events if e["pitch"] > 0]
        assert mann_pitches != folk_pitches, (
            "Mannelig and Son ar Chistr references produce identical pitch sequences."
        )

    def test_axelf_differs_from_son_ar_chistr(self):
        refs = _load_refs()
        axelf_events = parse_abc(refs["axel_f"])["events"]
        folk_events = parse_abc(refs["son_ar_chistr"])["events"]

        axelf_pitches = [e["pitch"] for e in axelf_events if e["pitch"] > 0]
        folk_pitches = [e["pitch"] for e in folk_events if e["pitch"] > 0]
        assert axelf_pitches != folk_pitches, (
            "Axel F and Son ar Chistr references produce identical pitch sequences."
        )

    def test_cases_has_pain(self):
        from rag_test import CASES
        assert "pain" in CASES, "Pain case not found in CASES dict."
        assert CASES["pain"]["bpm_expect"] == 140

    def test_cases_has_axelf(self):
        from rag_test import CASES
        assert "axelf" in CASES, "Axel F case not found in CASES dict."
        assert CASES["axelf"]["bpm_expect"] == 118

    def test_bttf_differs_from_son_ar_chistr(self):
        refs = _load_refs()
        bttf_events = parse_abc(refs["bttf"])["events"]
        folk_events = parse_abc(refs["son_ar_chistr"])["events"]

        bttf_pitches = [e["pitch"] for e in bttf_events if e["pitch"] > 0]
        folk_pitches = [e["pitch"] for e in folk_events if e["pitch"] > 0]
        assert bttf_pitches != folk_pitches, (
            "BTTF and Son ar Chistr references produce identical pitch sequences."
        )

    def test_cases_has_bttf(self):
        from rag_test import CASES
        assert "bttf" in CASES, "BTTF case not found in CASES dict."
        assert CASES["bttf"]["bpm_expect"] == 120


# -- Test 6: rhythm quality --


class TestReferenceRhythmVariety:
    """Reference melodies should have appropriate rhythm for their style.
    Folk melodies need duration variety; industrial staccato (Pain) is
    legitimately all-eighths."""

    # min distinct durations per reference
    EXPECTED_MIN_DURS = {
        "son_ar_chistr": 3,   # eighth, quarter, dotted-quarter
        "herr_mannelig": 3,   # eighth, quarter, dotted-quarter
        "shut_your_mouth": 1, # staccato industrial: all-eighths is correct
        "axel_f": 4,          # sixteenth, eighth, dotted-eighth, quarter, dotted-quarter
        "bttf": 5,            # sixteenth, eighth, dotted-eighth, quarter, dotted-quarter, half
    }
    # max consecutive same-duration notes
    EXPECTED_MAX_STREAK = {
        "son_ar_chistr": 10,
        "herr_mannelig": 12,
        "shut_your_mouth": 32, # 26 consecutive eighths is the real riff
        "axel_f": 8,
        "bttf": 3,             # triplet A-G-F run
    }

    @pytest.fixture(params=["son_ar_chistr", "herr_mannelig", "shut_your_mouth", "axel_f", "bttf"])
    def ref_name(self, request):
        return request.param

    def test_duration_variety(self, ref_name):
        refs = _load_refs()
        parsed = parse_abc(refs[ref_name])
        notes = [e for e in parsed["events"] if e["pitch"] > 0]
        dur_set = set(e["dur"] for e in notes)
        expected = self.EXPECTED_MIN_DURS[ref_name]
        assert len(dur_set) >= expected, (
            f"{ref_name}: only {dur_set} durations, need >= {expected}."
        )

    def test_max_same_duration_streak(self, ref_name):
        refs = _load_refs()
        parsed = parse_abc(refs[ref_name])
        durs = [e["dur"] for e in parsed["events"] if e["pitch"] > 0]
        max_streak = 1
        streak = 1
        for i in range(1, len(durs)):
            if durs[i] == durs[i - 1]:
                streak += 1
                max_streak = max(max_streak, streak)
            else:
                streak = 1
        limit = self.EXPECTED_MAX_STREAK[ref_name]
        assert max_streak <= limit, (
            f"{ref_name}: {max_streak} consecutive same-duration notes, "
            f"max {limit} allowed."
        )


# -- Test 7: quantize detection --


class TestQuantizeDetection:
    """parse_abc reads %%quantize: from ABC and returns it in the output."""

    def test_detects_quantize_colon(self):
        abc = "X:1\nT:Test\nM:4/4\nL:1/8\nK:C\n%%quantize:0.6\nCDEF|"
        parsed = parse_abc(abc)
        assert parsed["quantize"] == pytest.approx(0.6)

    def test_detects_quantize_space(self):
        abc = "X:1\nT:Test\nM:4/4\nL:1/8\nK:C\n%%quantize 0.85\nCDEF|"
        parsed = parse_abc(abc)
        assert parsed["quantize"] == pytest.approx(0.85)

    def test_ignores_out_of_range(self):
        abc = "X:1\nT:Test\nM:4/4\nL:1/8\nK:C\n%%quantize 8\nCDEF|"
        parsed = parse_abc(abc)
        assert parsed["quantize"] == pytest.approx(1.0)

    def test_ignores_fraction_format(self):
        abc = "X:1\nT:Test\nM:4/4\nL:1/8\nK:C\n%%quantize 1/8\nCDEF|"
        parsed = parse_abc(abc)
        assert parsed["quantize"] == pytest.approx(1.0)

    def test_llm_quantize_overrides_parameter(self):
        abc = "X:1\nT:Test\nM:4/4\nL:1/8\nK:C\n%%quantize:0.4\nCDEF|"
        parsed = parse_abc(abc, quantize=1.0)
        assert parsed["quantize"] == pytest.approx(0.4)

    def test_fallback_to_parameter(self):
        abc = "X:1\nT:Test\nM:4/4\nL:1/8\nK:C\nCDEF|"
        parsed = parse_abc(abc, quantize=0.7)
        assert parsed["quantize"] == pytest.approx(0.7)

    def test_default_quantize_is_1(self):
        abc = "X:1\nT:Test\nM:4/4\nL:1/8\nK:C\nCDEF|"
        parsed = parse_abc(abc)
        assert parsed["quantize"] == pytest.approx(1.0)

    def test_cases_have_quantize(self):
        from rag_test import CASES
        for name, case in CASES.items():
            assert "quantize" in case, f"{name}: missing quantize field"
            q = case["quantize"]
            assert 0.0 <= q <= 1.0, f"{name}: quantize {q} out of range"
