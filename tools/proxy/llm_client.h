// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#define LLM_MAX_RESPONSE  2048
#define LLM_MAX_REQUEST   4096
#define LLM_MAX_PROMPT    1024

/* --- JSON building / parsing (no I/O, testable in isolation) --- */

/* Build an OpenRouter chat/completions JSON request body.
 * reasoning_effort: "none", "medium", or "high" (NULL defaults to "none").
 * Returns bytes written to dst (excluding NUL), 0 on error. */
size_t llm_build_request(char *dst, size_t dst_max,
                         const char *model,
                         const char *system_prompt,
                         const char *user_prompt,
                         int max_tokens,
                         const char *reasoning_effort);

/* Extract choices[0].message.content from an OpenRouter JSON response.
 * Writes the text into dst. Returns length, 0 on parse failure. */
size_t llm_parse_response(char *dst, size_t dst_max,
                          const char *json, size_t json_len);

/* --- HTTP transport (POSIX sockets, plain HTTP only) --- */

struct llm_endpoint {
    const char *host;     /* e.g. "127.0.0.1" */
    uint16_t    port;     /* e.g. 8484 */
    const char *path;     /* e.g. "/v1/chat/completions" */
    const char *api_key;  /* Bearer token */
};

/* POST json_body to endpoint, read response into resp_buf.
 * Returns HTTP status code (200 on success), -1 on socket/transport error. */
int llm_http_post(const struct llm_endpoint *ep,
                  const char *json_body, size_t body_len,
                  char *resp_buf, size_t resp_max,
                  size_t *resp_len_out);

/* --- High-level: one-shot generate --- */

struct llm_result {
    char text[LLM_MAX_RESPONSE];
    size_t text_len;
    int http_status;
    int ok;  /* 1 if text was extracted successfully */
};

/* Full roundtrip: build request, POST, parse response.
 * reasoning_effort: "none", "medium", or "high" (NULL defaults to "none"). */
int llm_generate(const struct llm_endpoint *ep,
                 const char *model,
                 const char *system_prompt,
                 const char *user_prompt,
                 int max_tokens,
                 const char *reasoning_effort,
                 struct llm_result *result);

/* --- Agent loop: repeated prompt-generate-consume cycle --- */

/* Callback invoked after each generation. Return 0 to continue, nonzero to stop. */
typedef int (*llm_agent_cb)(const struct llm_result *result,
                            int iteration, void *ctx);

/* Prompt generator: fills prompt_buf with the next prompt.
 * prev_response is NULL on first call.
 * Return 0 to generate, nonzero to stop. */
typedef int (*llm_prompt_fn)(char *prompt_buf, size_t prompt_max,
                             const char *prev_response, int iteration,
                             void *ctx);

/* Run a simple agent loop: generate prompts, send them, process results.
 * Returns number of iterations completed. */
int llm_agent_loop(const struct llm_endpoint *ep,
                   const char *model,
                   const char *system_prompt,
                   int max_tokens,
                   int max_iterations,
                   llm_prompt_fn prompt_fn,
                   llm_agent_cb result_cb,
                   void *ctx);

#endif /* LLM_CLIENT_H */
