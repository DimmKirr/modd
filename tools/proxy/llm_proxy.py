#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

"""Lightweight HTTP proxy that bridges the FM-1's plain-HTTP GET requests
to OpenRouter's HTTPS POST chat completions API.

Endpoints:
  GET /health                          health check
  GET /generate?prompt=...             raw passthrough (any prompt)
  GET /pattern?inst=solo&artist=boc    structured pattern generation
  GET /instruments                     list available instruments
  GET /artists                         list available artists

Usage:
    OPENROUTER_API_KEY=sk-or-... python tools/proxy/llm_proxy.py [--port 8484]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

try:
    import requests
except ImportError:
    sys.exit("requests not installed: pip install requests")

from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "llm"))
from melody_generator import (
    INSTRUMENTS,
    build_system_prompt,
    build_user_prompt,
    list_instruments,
)

DEFAULT_PORT = 8484
DEFAULT_MODEL = "deepseek/deepseek-v4.1-flash-1017"
MAX_RESPONSE_CHARS = 900
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"

SYSTEM_PROMPT_RAW = (
    "You are a music generation assistant for an FM synthesizer. "
    "Respond with concise, structured data the synth can use: "
    "note sequences (MIDI numbers), chord progressions, scale suggestions, "
    "or engine parameter values. Keep responses under 800 characters. "
    "Use compact formats like CSV or short JSON arrays."
)


def get_config():
    parser = argparse.ArgumentParser(description="FM-1 LLM proxy for OpenRouter")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--model", default=os.environ.get("LLM_MODEL", DEFAULT_MODEL))
    parser.add_argument("--max-tokens", type=int, default=512)
    return parser.parse_args()


class ProxyHandler(BaseHTTPRequestHandler):
    api_key: str
    model: str
    max_tokens: int

    def do_GET(self):
        parsed = urlparse(self.path)
        qs = parse_qs(parsed.query)

        if parsed.path == "/health":
            self._respond(200, {"status": "ok"})

        elif parsed.path == "/instruments":
            self._respond(200, {"instruments": list_instruments()})

        elif parsed.path == "/artists":
            self._respond(200, {"artists": []})

        elif parsed.path == "/pattern":
            self._handle_pattern(qs)

        elif parsed.path == "/generate":
            self._handle_generate_get(qs)

        else:
            self._respond(404, {"error": "not found"})

    def do_POST(self):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else b""

        try:
            data = json.loads(body) if body else {}
        except json.JSONDecodeError:
            self._respond(400, {"error": "bad json"})
            return

        if parsed.path == "/pattern":
            self._handle_pattern(data)
        elif parsed.path == "/generate":
            self._handle_generate_post(data)
        else:
            self._respond(404, {"error": "not found"})

    # -- /pattern: structured pattern generation --

    def _handle_pattern(self, params):
        def get(key, default=""):
            v = params.get(key, default)
            return v[0] if isinstance(v, list) else v

        inst = get("inst", "solo")
        artist = get("artist", "")
        key = get("key", "C minor")
        steps = int(get("steps", "16"))
        model = get("model", self.model)

        if inst not in INSTRUMENTS:
            self._respond(400, {
                "error": f"unknown instrument: {inst}",
                "available": list(INSTRUMENTS.keys()),
            })
            return

        system_prompt = build_system_prompt()
        artist_dict = {"name": artist, "track": "", "style": ""} if artist else None
        user_prompt = build_user_prompt(inst, artist_dict, key, steps)

        self.log_message("pattern: inst=%s artist=%s key=%s steps=%d",
                         inst, artist or "none", key, steps)

        self._forward_to_openrouter(user_prompt, model,
                                    self.max_tokens, system_prompt)

    # -- /generate: raw passthrough --

    def _handle_generate_get(self, qs):
        prompt = qs.get("prompt", [""])[0]
        if not prompt:
            self._respond(400, {"error": "missing prompt parameter"})
            return
        model = qs.get("model", [self.model])[0]
        max_tokens = int(qs.get("max_tokens", [str(self.max_tokens)])[0])
        system = qs.get("system", [SYSTEM_PROMPT_RAW])[0]
        self._forward_to_openrouter(prompt, model, max_tokens, system)

    def _handle_generate_post(self, data):
        if "messages" in data and "model" in data:
            self._relay_to_openrouter(data)
            return

        prompt = data.get("prompt", "")
        if not prompt:
            self._respond(400, {"error": "missing prompt"})
            return

        model = data.get("model", self.model)
        max_tokens = data.get("max_tokens", self.max_tokens)
        system = data.get("system", SYSTEM_PROMPT_RAW)
        self._forward_to_openrouter(prompt, model, max_tokens, system)

    # -- OpenRouter relay (passthrough for C-built requests) --

    def _relay_to_openrouter(self, body: dict):
        model = body.get("model", self.model)
        self.log_message("relay: model=%s", model)

        try:
            resp = requests.post(
                OPENROUTER_URL,
                headers={
                    "Authorization": f"Bearer {self.api_key}",
                    "Content-Type": "application/json",
                    "X-OpenRouter-Title": "modd FM-1",
                },
                json=body,
                timeout=55,
            )
            resp.raise_for_status()
            raw = resp.json()
            text = raw["choices"][0]["message"]["content"]
            if text:
                text = text[:MAX_RESPONSE_CHARS]
            self._respond(200, {"text": text or ""})

        except requests.Timeout:
            self.log_message("OpenRouter timeout")
            self._respond(504, {"error": "upstream timeout"})
        except requests.RequestException as e:
            self.log_message("OpenRouter error: %s", e)
            self._respond(502, {"error": str(e)[:200]})
        except (KeyError, IndexError) as e:
            self.log_message("unexpected response: %s", e)
            self._respond(502, {"error": "bad upstream response"})

    # -- OpenRouter forwarding (legacy GET-style) --

    def _forward_to_openrouter(self, prompt, model, max_tokens, system_prompt):
        self.log_message("prompt: %s (model=%s)", prompt[:80], model)

        try:
            resp = requests.post(
                OPENROUTER_URL,
                headers={
                    "Authorization": f"Bearer {self.api_key}",
                    "Content-Type": "application/json",
                    "X-OpenRouter-Title": "modd FM-1",
                },
                json={
                    "model": model,
                    "max_tokens": max_tokens,
                    "reasoning": {"effort": "none"},
                    "messages": [
                        {"role": "system", "content": system_prompt},
                        {"role": "user", "content": prompt},
                    ],
                },
                timeout=55,
            )
            resp.raise_for_status()
            data = resp.json()
            text = data["choices"][0]["message"]["content"]
            text = text[:MAX_RESPONSE_CHARS]
            self._respond(200, {"text": text})

        except requests.Timeout:
            self.log_message("OpenRouter timeout")
            self._respond(504, {"error": "upstream timeout"})
        except requests.RequestException as e:
            self.log_message("OpenRouter error: %s", e)
            self._respond(502, {"error": str(e)[:200]})
        except (KeyError, IndexError) as e:
            self.log_message("unexpected response: %s", e)
            self._respond(502, {"error": "bad upstream response"})

    def _respond(self, code: int, body: dict):
        payload = json.dumps(body, ensure_ascii=True).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        sys.stderr.write(f"[llm-proxy] {fmt % args}\n")


def main():
    cfg = get_config()

    api_key = os.environ.get("OPENROUTER_API_KEY", "")
    if not api_key:
        sys.exit("OPENROUTER_API_KEY not set")

    ProxyHandler.api_key = api_key
    ProxyHandler.model = cfg.model
    ProxyHandler.max_tokens = cfg.max_tokens

    server = HTTPServer(("0.0.0.0", cfg.port), ProxyHandler)
    print(f"[llm-proxy] listening on 0.0.0.0:{cfg.port}")
    print(f"[llm-proxy] model={cfg.model} max_tokens={cfg.max_tokens}")
    print(f"[llm-proxy] endpoints:")
    print(f"  GET /pattern?inst=solo&artist=kraftwerk&key=C+minor&steps=16")
    print(f"  GET /generate?prompt=<text>")
    print(f"  GET /instruments")
    print(f"  GET /artists")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[llm-proxy] shutting down")
        server.server_close()


if __name__ == "__main__":
    main()
