#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Tests for the C LLM client library (tools/proxy/llm_client.c).

Compiles the C code into a shared library, loads it via ctypes,
and exercises JSON building, parsing, HTTP transport, and the agent
loop against a local mock OpenRouter server.
"""
import ctypes
import json
import subprocess
import sys
import threading
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    byref,
    c_char,
    c_char_p,
    c_int,
    c_size_t,
    c_uint16,
    c_void_p,
    create_string_buffer,
)
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "tools" / "proxy" / "llm_client.c"
HDR = ROOT / "tools" / "proxy" / "llm_client.h"
BUILD_DIR = ROOT / "build" / "test"

LLM_MAX_RESPONSE = 2048
LLM_MAX_PROMPT = 1024


# ---- ctypes structures matching llm_client.h -------------------------


class LlmEndpoint(Structure):
    _fields_ = [
        ("host", c_char_p),
        ("port", c_uint16),
        ("path", c_char_p),
        ("api_key", c_char_p),
    ]


class LlmResult(Structure):
    _fields_ = [
        ("text", c_char * LLM_MAX_RESPONSE),
        ("text_len", c_size_t),
        ("http_status", c_int),
        ("ok", c_int),
    ]


# callback types
AGENT_CB = CFUNCTYPE(c_int, POINTER(LlmResult), c_int, c_void_p)
# prompt_fn: buf is writable (c_void_p to avoid ctypes auto-converting to bytes),
# prev_response is read-only (also c_void_p since it can be NULL)
PROMPT_FN = CFUNCTYPE(c_int, c_void_p, c_size_t, c_void_p, c_int, c_void_p)


# ---- compile the shared library --------------------------------------


@pytest.fixture(scope="session")
def lib():
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    so_path = BUILD_DIR / "llm_client.so"

    result = subprocess.run(
        [
            "cc",
            "-shared",
            "-fPIC",
            "-O1",
            "-Wall",
            "-Wextra",
            "-o",
            str(so_path),
            str(SRC),
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        pytest.fail(f"Failed to compile llm_client.c:\n{result.stderr}")

    lib = ctypes.CDLL(str(so_path))

    # llm_build_request
    lib.llm_build_request.restype = c_size_t
    lib.llm_build_request.argtypes = [
        c_char_p,
        c_size_t,
        c_char_p,
        c_char_p,
        c_char_p,
        c_int,
    ]

    # llm_parse_response
    lib.llm_parse_response.restype = c_size_t
    lib.llm_parse_response.argtypes = [c_char_p, c_size_t, c_char_p, c_size_t]

    # llm_http_post
    lib.llm_http_post.restype = c_int
    lib.llm_http_post.argtypes = [
        POINTER(LlmEndpoint),
        c_char_p,
        c_size_t,
        c_char_p,
        c_size_t,
        POINTER(c_size_t),
    ]

    # llm_generate
    lib.llm_generate.restype = c_int
    lib.llm_generate.argtypes = [
        POINTER(LlmEndpoint),
        c_char_p,
        c_char_p,
        c_char_p,
        c_int,
        POINTER(LlmResult),
    ]

    # llm_agent_loop
    lib.llm_agent_loop.restype = c_int
    lib.llm_agent_loop.argtypes = [
        POINTER(LlmEndpoint),
        c_char_p,
        c_char_p,
        c_int,
        c_int,
        PROMPT_FN,
        AGENT_CB,
        c_void_p,
    ]

    return lib


# ---- mock OpenRouter server ------------------------------------------


class MockOpenRouterHandler(BaseHTTPRequestHandler):
    """Mimics the OpenRouter /v1/chat/completions endpoint."""

    response_text = "C,Eb,G,Bb"
    fail_next = False

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else b""

        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            self._respond(401, {"error": "unauthorized"})
            return

        if self.fail_next:
            MockOpenRouterHandler.fail_next = False
            self._respond(500, {"error": "internal"})
            return

        try:
            req = json.loads(body)
        except json.JSONDecodeError:
            self._respond(400, {"error": "bad json"})
            return

        model = req.get("model", "unknown")
        messages = req.get("messages", [])
        user_msg = ""
        for m in messages:
            if m.get("role") == "user":
                user_msg = m.get("content", "")

        # store for test assertions
        MockOpenRouterHandler.last_request = req
        MockOpenRouterHandler.last_user_message = user_msg

        response = {
            "id": "test-123",
            "model": model,
            "choices": [
                {
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": self.response_text,
                    },
                    "finish_reason": "stop",
                }
            ],
            "usage": {"prompt_tokens": 10, "completion_tokens": 5},
        }
        self._respond(200, response)

    def _respond(self, code, body):
        payload = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        pass  # silence during tests


@pytest.fixture(scope="session")
def mock_server():
    server = HTTPServer(("127.0.0.1", 0), MockOpenRouterHandler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield server, port
    server.shutdown()


@pytest.fixture()
def endpoint(mock_server):
    _, port = mock_server
    ep = LlmEndpoint()
    ep.host = b"127.0.0.1"
    ep.port = port
    ep.path = b"/v1/chat/completions"
    ep.api_key = b"sk-test-key-1234"
    return ep


# ---- tests: JSON building -------------------------------------------


class TestBuildRequest:
    def test_basic_request(self, lib):
        buf = create_string_buffer(4096)
        n = lib.llm_build_request(
            buf, 4096, b"gpt-4o-mini", b"You are helpful.", b"Hello", 100
        )
        assert n > 0
        req = json.loads(buf.value)
        assert req["model"] == "gpt-4o-mini"
        assert req["max_tokens"] == 100
        assert len(req["messages"]) == 2
        assert req["messages"][0]["role"] == "system"
        assert req["messages"][1]["role"] == "user"
        assert req["messages"][1]["content"] == "Hello"

    def test_no_system_prompt(self, lib):
        buf = create_string_buffer(4096)
        n = lib.llm_build_request(buf, 4096, b"gpt-4o-mini", None, b"Hi", 50)
        assert n > 0
        req = json.loads(buf.value)
        assert len(req["messages"]) == 1
        assert req["messages"][0]["role"] == "user"

    def test_empty_system_prompt(self, lib):
        buf = create_string_buffer(4096)
        n = lib.llm_build_request(buf, 4096, b"gpt-4o-mini", b"", b"Hi", 50)
        assert n > 0
        req = json.loads(buf.value)
        assert len(req["messages"]) == 1

    def test_json_escaping(self, lib):
        buf = create_string_buffer(4096)
        n = lib.llm_build_request(
            buf, 4096, b"m", b"sys", b'quote "here" and\\back', 10
        )
        assert n > 0
        req = json.loads(buf.value)
        assert req["messages"][1]["content"] == 'quote "here" and\\back'

    def test_newlines_escaped(self, lib):
        buf = create_string_buffer(4096)
        n = lib.llm_build_request(buf, 4096, b"m", b"sys", b"line1\nline2", 10)
        assert n > 0
        req = json.loads(buf.value)
        assert req["messages"][1]["content"] == "line1\nline2"

    def test_buffer_too_small(self, lib):
        buf = create_string_buffer(10)
        n = lib.llm_build_request(buf, 10, b"model", b"sys", b"prompt", 100)
        assert n == 0


# ---- tests: JSON parsing --------------------------------------------


class TestParseResponse:
    def test_standard_response(self, lib):
        resp = json.dumps(
            {
                "choices": [
                    {"message": {"role": "assistant", "content": "C,Eb,G,Bb"}}
                ]
            }
        ).encode()
        buf = create_string_buffer(2048)
        n = lib.llm_parse_response(buf, 2048, resp, len(resp))
        assert n > 0
        assert buf.value == b"C,Eb,G,Bb"

    def test_escaped_content(self, lib):
        resp = json.dumps(
            {"choices": [{"message": {"content": 'say "hello"\nworld'}}]}
        ).encode()
        buf = create_string_buffer(2048)
        n = lib.llm_parse_response(buf, 2048, resp, len(resp))
        assert n > 0
        assert buf.value == b'say "hello"\nworld'

    def test_no_content_field(self, lib):
        resp = b'{"choices":[{"message":{"role":"assistant"}}]}'
        buf = create_string_buffer(2048)
        n = lib.llm_parse_response(buf, 2048, resp, len(resp))
        assert n == 0

    def test_empty_json(self, lib):
        buf = create_string_buffer(2048)
        n = lib.llm_parse_response(buf, 2048, b"{}", 2)
        assert n == 0


# ---- tests: HTTP transport -------------------------------------------


class TestHttpPost:
    def test_successful_post(self, lib, endpoint, mock_server):
        body = b'{"model":"test","messages":[{"role":"user","content":"hi"}]}'
        resp = create_string_buffer(4096)
        resp_len = c_size_t(0)

        status = lib.llm_http_post(
            byref(endpoint), body, len(body), resp, 4096, byref(resp_len)
        )
        assert status == 200
        assert resp_len.value > 0
        parsed = json.loads(resp.value[: resp_len.value])
        assert "choices" in parsed

    def test_missing_auth(self, lib, mock_server):
        _, port = mock_server
        ep = LlmEndpoint()
        ep.host = b"127.0.0.1"
        ep.port = port
        ep.path = b"/v1/chat/completions"
        ep.api_key = b""  # no Bearer prefix content

        body = b'{"model":"test","messages":[]}'
        resp = create_string_buffer(4096)
        resp_len = c_size_t(0)

        # server checks "Bearer " prefix, empty key still sends "Bearer "
        status = lib.llm_http_post(
            byref(ep), body, len(body), resp, 4096, byref(resp_len)
        )
        # accepts "Bearer " with empty key (server just checks prefix)
        assert status in (200, 401)

    def test_connection_refused(self, lib):
        ep = LlmEndpoint()
        ep.host = b"127.0.0.1"
        ep.port = 1  # unlikely to be listening
        ep.path = b"/"
        ep.api_key = b"key"

        body = b"{}"
        resp = create_string_buffer(256)
        resp_len = c_size_t(0)

        status = lib.llm_http_post(
            byref(ep), body, 2, resp, 256, byref(resp_len)
        )
        assert status == -1


# ---- tests: high-level generate -------------------------------------


class TestGenerate:
    def test_full_roundtrip(self, lib, endpoint):
        MockOpenRouterHandler.response_text = "Cm7,Fm7,Bb7,EbMaj7"
        result = LlmResult()

        rc = lib.llm_generate(
            byref(endpoint),
            b"gpt-4o-mini",
            b"You generate jazz chords.",
            b"Give me a ii-V-I-IV in Eb major",
            128,
            byref(result),
        )
        assert rc == 0
        assert result.ok == 1
        assert result.http_status == 200
        assert b"Cm7" in result.text
        assert result.text_len > 0

    def test_request_reaches_server(self, lib, endpoint):
        MockOpenRouterHandler.response_text = "ok"
        result = LlmResult()

        lib.llm_generate(
            byref(endpoint),
            b"test-model",
            b"sys",
            b"what scale fits Am?",
            64,
            byref(result),
        )
        assert MockOpenRouterHandler.last_user_message == "what scale fits Am?"
        assert MockOpenRouterHandler.last_request["model"] == "test-model"

    def test_server_error(self, lib, endpoint):
        MockOpenRouterHandler.fail_next = True
        result = LlmResult()

        rc = lib.llm_generate(
            byref(endpoint),
            b"model",
            b"sys",
            b"prompt",
            64,
            byref(result),
        )
        assert rc == -1
        assert result.ok == 0


# ---- tests: agent loop -----------------------------------------------


class TestAgentLoop:
    def test_three_iterations(self, lib, endpoint):
        MockOpenRouterHandler.response_text = "C,E,G"
        collected = []

        @AGENT_CB
        def on_result(result_ptr, iteration, ctx):
            r = result_ptr.contents
            collected.append((iteration, r.text[: r.text_len].decode()))
            return 0  # continue

        prompts = [b"chord 1", b"chord 2", b"chord 3"]

        @PROMPT_FN
        def gen_prompt(buf, buf_max, prev, iteration, ctx):
            if iteration >= len(prompts):
                return 1  # stop
            p = prompts[iteration]
            ctypes.memmove(buf, p, len(p) + 1)
            return 0

        n = lib.llm_agent_loop(
            byref(endpoint),
            b"z-ai/glm-5.3-flash",
            b"music helper",
            128,
            10,
            gen_prompt,
            on_result,
            None,
        )
        assert n == 3
        assert len(collected) == 3
        for i, (idx, text) in enumerate(collected):
            assert idx == i
            assert text == "C,E,G"

    def test_early_stop_from_callback(self, lib, endpoint):
        MockOpenRouterHandler.response_text = "stop"

        @AGENT_CB
        def on_result(result_ptr, iteration, ctx):
            return 1  # stop after first

        @PROMPT_FN
        def gen_prompt(buf, buf_max, prev, iteration, ctx):
            p = b"go"
            ctypes.memmove(buf, p, len(p) + 1)
            return 0

        n = lib.llm_agent_loop(
            byref(endpoint),
            b"model",
            b"sys",
            64,
            100,
            gen_prompt,
            on_result,
            None,
        )
        assert n == 1

    def test_prompt_uses_previous_response(self, lib, endpoint):
        MockOpenRouterHandler.response_text = "Dm7,G7,CMaj7"
        seen_prev = []

        @AGENT_CB
        def on_result(result_ptr, iteration, ctx):
            return 0

        @PROMPT_FN
        def gen_prompt(buf, buf_max, prev, iteration, ctx):
            if iteration >= 2:
                return 1
            if prev:
                seen_prev.append(ctypes.string_at(prev).decode())
            else:
                seen_prev.append(None)
            p = b"next"
            ctypes.memmove(buf, p, len(p) + 1)
            return 0

        lib.llm_agent_loop(
            byref(endpoint), b"m", b"s", 64, 5, gen_prompt, on_result, None
        )
        assert seen_prev[0] is None  # first iteration, no prev
        assert seen_prev[1] == "Dm7,G7,CMaj7"  # second gets first response
