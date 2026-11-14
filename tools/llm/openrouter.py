#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

"""Ctypes wrapper around the firmware C LLM client.

The C code does everything: builds the request JSON, sends HTTP POST
to the local proxy, parses the response. This is the exact firmware
code path. Python just manages the proxy process and collects output.

Architecture (same as the real device):
    C llm_generate() --HTTP POST--> proxy :8484 --HTTPS--> OpenRouter

Usage:
    from openrouter import Proxy, generate, agent_loop

    with Proxy():                          # starts/stops proxy
        text = generate("model", "sys", "user")
        # or
        results = agent_loop("model", "sys", prompt_fn, result_cb)
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_MODEL = "deepseek/deepseek-v4.1-flash-1017"
PROXY_HOST = "127.0.0.1"
PROXY_PORT = 8484
PROXY_PATH = "/generate"

LLM_MAX_RESPONSE = 2048
LLM_MAX_REQUEST = 4096
LLM_MAX_PROMPT = 1024

_C_SRC = Path(__file__).resolve().parents[1] / "proxy"
_PROXY_SCRIPT = _C_SRC / "llm_proxy.py"
_BUILD_DIR = Path(__file__).resolve().parents[2] / "build" / "llm"
_SO_PATH = _BUILD_DIR / "llm_client.so"

_lib = None


# -- Compile --

def _compile() -> Path:
    src = _C_SRC / "llm_client.c"
    if not src.exists():
        raise FileNotFoundError(f"C source not found: {src}")

    _BUILD_DIR.mkdir(parents=True, exist_ok=True)

    if _SO_PATH.exists() and _SO_PATH.stat().st_mtime > src.stat().st_mtime:
        return _SO_PATH

    print(f"[openrouter] compiling {src.name}...", file=sys.stderr)
    subprocess.check_call([
        "cc", "-shared", "-fPIC", "-O2",
        "-I", str(_C_SRC),
        "-o", str(_SO_PATH),
        str(src),
    ])
    return _SO_PATH


# -- C structs --

class LlmEndpoint(ctypes.Structure):
    _fields_ = [
        ("host", ctypes.c_char_p),
        ("port", ctypes.c_uint16),
        ("path", ctypes.c_char_p),
        ("api_key", ctypes.c_char_p),
    ]


class LlmResult(ctypes.Structure):
    _fields_ = [
        ("text", ctypes.c_char * LLM_MAX_RESPONSE),
        ("text_len", ctypes.c_size_t),
        ("http_status", ctypes.c_int),
        ("ok", ctypes.c_int),
    ]


PROMPT_FN = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p, ctypes.c_size_t,  # prompt_buf, prompt_max
    ctypes.c_char_p,                    # prev_response
    ctypes.c_int,                       # iteration
    ctypes.c_void_p,                    # ctx
)

AGENT_CB = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.POINTER(LlmResult),         # result
    ctypes.c_int,                       # iteration
    ctypes.c_void_p,                    # ctx
)


def _get_lib():
    global _lib
    if _lib is not None:
        return _lib

    so = _compile()
    _lib = ctypes.CDLL(str(so))

    # llm_build_request
    _lib.llm_build_request.argtypes = [
        ctypes.c_char_p, ctypes.c_size_t,
        ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_int, ctypes.c_char_p,
    ]
    _lib.llm_build_request.restype = ctypes.c_size_t

    # llm_parse_response
    _lib.llm_parse_response.argtypes = [
        ctypes.c_char_p, ctypes.c_size_t,
        ctypes.c_char_p, ctypes.c_size_t,
    ]
    _lib.llm_parse_response.restype = ctypes.c_size_t

    # llm_generate
    _lib.llm_generate.argtypes = [
        ctypes.POINTER(LlmEndpoint),
        ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_int, ctypes.c_char_p,
        ctypes.POINTER(LlmResult),
    ]
    _lib.llm_generate.restype = ctypes.c_int

    # llm_agent_loop
    _lib.llm_agent_loop.argtypes = [
        ctypes.POINTER(LlmEndpoint),
        ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_int, ctypes.c_int,
        PROMPT_FN, AGENT_CB,
        ctypes.c_void_p,
    ]
    _lib.llm_agent_loop.restype = ctypes.c_int

    return _lib


# -- Proxy lifecycle --

class Proxy:
    """Context manager that starts/stops the local HTTP→HTTPS proxy."""

    def __init__(self, port: int = PROXY_PORT, model: str = DEFAULT_MODEL):
        self.port = port
        self.model = model
        self._proc = None

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()

    def start(self):
        if self._proc is not None:
            return

        api_key = os.environ.get("OPENROUTER_API_KEY", "")
        if not api_key:
            raise SystemExit("OPENROUTER_API_KEY not set")

        self._proc = subprocess.Popen(
            [sys.executable, str(_PROXY_SCRIPT),
             "--port", str(self.port), "--model", self.model],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            env={**os.environ, "OPENROUTER_API_KEY": api_key},
        )
        self._wait_ready()
        print(f"[openrouter] proxy started on :{self.port}", file=sys.stderr)

    def stop(self):
        if self._proc is None:
            return
        self._proc.send_signal(signal.SIGTERM)
        self._proc.wait(timeout=5)
        self._proc = None

    def _wait_ready(self, timeout: float = 10):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                s = socket.create_connection((PROXY_HOST, self.port), timeout=0.5)
                s.close()
                return
            except OSError:
                time.sleep(0.2)
        raise TimeoutError(f"proxy not ready after {timeout}s")


def _endpoint(host: str = PROXY_HOST, port: int = PROXY_PORT,
              path: str = PROXY_PATH) -> LlmEndpoint:
    return LlmEndpoint(
        host=host.encode(),
        port=port,
        path=path.encode(),
        api_key=b"",
    )


# -- Public API: all calls go through C --

def build_request(model: str, system_prompt: str, user_prompt: str,
                  max_tokens: int = 512,
                  reasoning: str | None = None) -> str:
    """Build OpenRouter JSON request body using the firmware C code."""
    lib = _get_lib()
    buf = ctypes.create_string_buffer(LLM_MAX_REQUEST)
    n = lib.llm_build_request(
        buf, LLM_MAX_REQUEST,
        model.encode(), system_prompt.encode(), user_prompt.encode(),
        max_tokens,
        reasoning.encode() if reasoning else None,
    )
    if n == 0:
        raise ValueError("llm_build_request failed (prompt too long?)")
    return buf.value.decode()


def parse_response(json_text: str) -> str:
    """Extract content from response JSON using the firmware C code."""
    lib = _get_lib()
    raw = json_text.encode()
    buf = ctypes.create_string_buffer(LLM_MAX_RESPONSE)
    n = lib.llm_parse_response(buf, LLM_MAX_RESPONSE, raw, len(raw))
    if n == 0:
        raise ValueError(f"parse failed: {json_text[:200]}")
    return buf.value.decode()


def generate(model: str, system_prompt: str, user_prompt: str,
             max_tokens: int = 512,
             reasoning: str | None = None,
             host: str = PROXY_HOST, port: int = PROXY_PORT) -> str:
    """Full C roundtrip: build request, HTTP POST to proxy, parse response."""
    lib = _get_lib()
    ep = _endpoint(host, port)
    result = LlmResult()

    rc = lib.llm_generate(
        ctypes.byref(ep),
        model.encode(), system_prompt.encode(), user_prompt.encode(),
        max_tokens,
        reasoning.encode() if reasoning else None,
        ctypes.byref(result),
    )

    if rc != 0:
        raise RuntimeError(f"llm_generate failed: http_status={result.http_status}")

    return result.text.decode()


def agent_loop(model: str, system_prompt: str,
               max_tokens: int = 512, max_iterations: int = 3,
               prompt_fn=None, result_cb=None, ctx=None,
               host: str = PROXY_HOST, port: int = PROXY_PORT) -> int:
    """Run the C agent loop. Returns number of completed iterations."""
    lib = _get_lib()
    ep = _endpoint(host, port)

    c_prompt_fn = PROMPT_FN(prompt_fn) if prompt_fn else PROMPT_FN(0)
    c_result_cb = AGENT_CB(result_cb) if result_cb else AGENT_CB(0)

    return lib.llm_agent_loop(
        ctypes.byref(ep),
        model.encode(), system_prompt.encode(),
        max_tokens, max_iterations,
        c_prompt_fn, c_result_cb,
        ctx,
    )


# -- Convenience: hybrid mode (C build/parse, Python HTTPS) --
# For when the proxy isn't running (e.g. artist_inspiration cache miss)

def call(model: str, system_prompt: str, user_prompt: str,
         max_tokens: int = 512, api_key: str | None = None,
         reasoning: str | None = None) -> str:
    """Hybrid: C builds request + parses response, Python does HTTPS."""
    try:
        import requests
    except ImportError:
        raise SystemExit("requests not installed: pip install requests")

    api_key = api_key or os.environ.get("OPENROUTER_API_KEY", "")
    if not api_key:
        raise SystemExit("OPENROUTER_API_KEY not set")

    body = build_request(model, system_prompt, user_prompt, max_tokens,
                         reasoning)

    resp = requests.post(
        "https://openrouter.ai/api/v1/chat/completions",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "X-OpenRouter-Title": "modd FM-1",
        },
        data=body,
        timeout=55,
    )
    resp.raise_for_status()

    return parse_response(resp.text)
