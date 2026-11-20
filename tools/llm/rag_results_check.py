#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Check the output of a `tools/llm/rag_test.py` run (networked, manual).

    python tools/llm/rag_test.py
    uv run python -m pytest tools/llm/rag_results_check.py

Checks the newest tests/results/*-rag-ab folder, or the one named by RAG_AB_DIR.
Fails when there is no such folder: it is not part of the unit tier.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

RESULTS_DIR = Path(__file__).resolve().parents[2] / "tests" / "results"


def _latest_rag_dir() -> Path | None:
    """RAG_AB_DIR if set, else the most recent rag-ab results directory."""
    env = os.environ.get("RAG_AB_DIR")
    if env:
        return Path(env)
    candidates = sorted(RESULTS_DIR.glob("*-rag-ab"), reverse=True)
    return candidates[0] if candidates else None


@pytest.fixture(scope="module")
def rag_dir():
    d = _latest_rag_dir()
    if d is None or not d.is_dir():
        pytest.fail(
            f"No rag-ab results directory found in {RESULTS_DIR} (or RAG_AB_DIR). "
            "Produce it with: python tools/llm/rag_test.py (needs the network and an API key)",
            pytrace=False,
        )
    return d


# -- Generated RAG results must contain rests --


class TestGeneratedRAGHasRests:
    """Validate that actual LLM-generated RAG outputs contain rest events."""

    @pytest.fixture(params=["mannelig", "scooter", "zevendagen", "pain", "axelf", "bttf"])
    def case_name(self, request):
        return request.param

    @pytest.fixture(params=["rag-glm53", "rag-gemini", "rag-deepseek"])
    def model_tag(self, request):
        return request.param

    def test_rag_abc_has_z_tokens(self, rag_dir, case_name, model_tag):
        abc_path = rag_dir / case_name / f"{model_tag}.abc"
        if not abc_path.exists():
            pytest.skip(f"Not found: {abc_path}")

        abc = abc_path.read_text()
        body = abc.split("K:")[1] if "K:" in abc else abc
        assert "z" in body, (
            f"{case_name}/{model_tag}: ABC has no 'z' rest tokens. "
            f"Melody plays continuously with no breathing room."
        )

    def test_rag_json_has_rest_events(self, rag_dir, case_name, model_tag):
        json_path = rag_dir / case_name / f"{model_tag}.json"
        if not json_path.exists():
            pytest.skip(f"Not found: {json_path}")

        data = json.loads(json_path.read_text())
        events = data.get("events", [])
        rests = [e for e in events if e["pitch"] == 0]
        assert len(rests) >= 1, (
            f"{case_name}/{model_tag}: {len(rests)} rests in output. "
            f"Need >= 1 for breathing room."
        )


# -- Tempo matches expected BPM --


class TestGeneratedRAGTempo:
    """RAG output must use the target BPM, not the reference's original tempo."""

    EXPECTED_BPM = {
        "scooter": 150,
        "mannelig": 132,
        "zevendagen": 120,
        "pain": 140,
        "axelf": 118,
        "bttf": 120,
    }

    @pytest.fixture(params=["mannelig", "scooter", "zevendagen", "pain", "axelf", "bttf"])
    def case_name(self, request):
        return request.param

    @pytest.fixture(params=["rag-glm53", "rag-gemini", "rag-deepseek"])
    def model_tag(self, request):
        return request.param

    def test_bpm_matches_target(self, rag_dir, case_name, model_tag):
        json_path = rag_dir / case_name / f"{model_tag}.json"
        if not json_path.exists():
            pytest.skip(f"Not found: {json_path}")

        data = json.loads(json_path.read_text())
        expected = self.EXPECTED_BPM[case_name]
        assert data["bpm"] == expected, (
            f"{case_name}/{model_tag}: bpm={data['bpm']}, expected {expected}"
        )
