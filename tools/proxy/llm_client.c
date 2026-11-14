// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

#include "llm_client.h"

#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ---- JSON helpers (no dependencies beyond libc) ----------------------- */

static size_t json_escape(char *dst, size_t dst_max, const char *src)
{
    size_t di = 0;
    while (*src && di + 6 < dst_max) {
        unsigned char c = (unsigned char)*src;
        switch (c) {
        case '"':  dst[di++] = '\\'; dst[di++] = '"';  break;
        case '\\': dst[di++] = '\\'; dst[di++] = '\\'; break;
        case '\n': dst[di++] = '\\'; dst[di++] = 'n';  break;
        case '\r': dst[di++] = '\\'; dst[di++] = 'r';  break;
        case '\t': dst[di++] = '\\'; dst[di++] = 't';  break;
        default:
            if (c < 0x20) {
                di += (size_t)snprintf(dst + di, dst_max - di, "\\u%04x", c);
            } else {
                dst[di++] = (char)c;
            }
        }
        src++;
    }
    dst[di] = 0;
    return di;
}

size_t llm_build_request(char *dst, size_t dst_max,
                         const char *model,
                         const char *system_prompt,
                         const char *user_prompt,
                         int max_tokens,
                         const char *reasoning_effort)
{
    char esc_sys[1024];
    char esc_usr[LLM_MAX_PROMPT * 2];
    int n;
    const char *effort = (reasoning_effort && reasoning_effort[0])
                         ? reasoning_effort : "none";

    json_escape(esc_sys, sizeof(esc_sys), system_prompt ? system_prompt : "");
    json_escape(esc_usr, sizeof(esc_usr), user_prompt);

    if (system_prompt && system_prompt[0]) {
        n = snprintf(dst, dst_max,
            "{\"model\":\"%s\",\"max_tokens\":%d,"
            "\"reasoning\":{\"effort\":\"%s\"},"
            "\"messages\":["
            "{\"role\":\"system\",\"content\":\"%s\"},"
            "{\"role\":\"user\",\"content\":\"%s\"}"
            "]}",
            model, max_tokens, effort, esc_sys, esc_usr);
    } else {
        n = snprintf(dst, dst_max,
            "{\"model\":\"%s\",\"max_tokens\":%d,"
            "\"reasoning\":{\"effort\":\"%s\"},"
            "\"messages\":["
            "{\"role\":\"user\",\"content\":\"%s\"}"
            "]}",
            model, max_tokens, effort, esc_usr);
    }

    if (n < 0 || (size_t)n >= dst_max) return 0;
    return (size_t)n;
}

/* Scan for "key":"value" in JSON. Writes unquoted value into out.
 * Returns length, 0 on miss. Handles nested objects by tracking depth. */
static size_t json_extract_string(const char *json, size_t json_len,
                                  const char *key, char *out, size_t out_max)
{
    size_t klen = strlen(key);
    size_t i;

    if (out_max == 0) return 0;

    for (i = 0; i + klen + 3 < json_len; i++) {
        if (json[i] != '"') continue;

        if (memcmp(json + i + 1, key, klen) != 0) continue;
        if (json[i + 1 + klen] != '"') continue;

        /* skip past "key": */
        size_t j = i + 2 + klen;
        while (j < json_len && (json[j] == ':' || json[j] == ' ')) j++;
        if (j >= json_len) break;

        if (json[j] == '"') {
            /* string value: collect until unescaped quote */
            size_t start = j + 1;
            size_t oi = 0;
            size_t si = start;
            while (si < json_len && oi + 1 < out_max) {
                if (json[si] == '\\' && si + 1 < json_len) {
                    switch (json[si + 1]) {
                    case '"':  out[oi++] = '"';  si += 2; continue;
                    case '\\': out[oi++] = '\\'; si += 2; continue;
                    case 'n':  out[oi++] = '\n'; si += 2; continue;
                    case 'r':  out[oi++] = '\r'; si += 2; continue;
                    case 't':  out[oi++] = '\t'; si += 2; continue;
                    default:   out[oi++] = json[si + 1]; si += 2; continue;
                    }
                }
                if (json[si] == '"') break;
                out[oi++] = json[si++];
            }
            out[oi] = 0;
            return oi;
        }
    }
    out[0] = 0;
    return 0;
}

size_t llm_parse_response(char *dst, size_t dst_max,
                          const char *json, size_t json_len)
{
    size_t n;
    /* OpenRouter native: {"choices":[{"message":{"content":"..."}}]} */
    n = json_extract_string(json, json_len, "content", dst, dst_max);
    if (n > 0) return n;
    /* Proxy wrapper: {"text":"..."} */
    return json_extract_string(json, json_len, "text", dst, dst_max);
}

/* ---- POSIX socket HTTP POST ------------------------------------------- */

static int connect_tcp(const char *host, uint16_t port)
{
    struct addrinfo hints = {0}, *res = NULL;
    char port_str[8];
    int fd;

    snprintf(port_str, sizeof(port_str), "%u", port);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return -1;

    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

static int send_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, buf, len, 0);
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static ssize_t recv_all(int fd, char *buf, size_t max)
{
    size_t total = 0;
    while (total < max) {
        ssize_t n = recv(fd, buf + total, max - total, 0);
        if (n < 0) return -1;
        if (n == 0) break;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

int llm_http_post(const struct llm_endpoint *ep,
                  const char *json_body, size_t body_len,
                  char *resp_buf, size_t resp_max,
                  size_t *resp_len_out)
{
    char header[512];
    int hlen;
    int fd;
    ssize_t received;
    int status = -1;

    fd = connect_tcp(ep->host, ep->port);
    if (fd < 0) return -1;

    hlen = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Authorization: Bearer %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        ep->path, ep->host, ep->port, body_len,
        ep->api_key ? ep->api_key : "");

    if (send_all(fd, header, (size_t)hlen) != 0 ||
        send_all(fd, json_body, body_len) != 0) {
        close(fd);
        return -1;
    }

    received = recv_all(fd, resp_buf, resp_max - 1);
    close(fd);

    if (received <= 0) return -1;
    resp_buf[received] = 0;

    /* parse HTTP status from first line */
    {
        const char *sp = strchr(resp_buf, ' ');
        if (sp) status = atoi(sp + 1);
    }

    /* find body after \r\n\r\n */
    {
        const char *body = strstr(resp_buf, "\r\n\r\n");
        if (body) {
            body += 4;
            size_t body_off = (size_t)(body - resp_buf);
            size_t blen = (size_t)received - body_off;
            memmove(resp_buf, body, blen);
            resp_buf[blen] = 0;
            if (resp_len_out) *resp_len_out = blen;
        } else {
            if (resp_len_out) *resp_len_out = (size_t)received;
        }
    }

    return status;
}

/* ---- High-level generate ---------------------------------------------- */

int llm_generate(const struct llm_endpoint *ep,
                 const char *model,
                 const char *system_prompt,
                 const char *user_prompt,
                 int max_tokens,
                 const char *reasoning_effort,
                 struct llm_result *result)
{
    char req_body[LLM_MAX_REQUEST];
    char resp_buf[LLM_MAX_RESPONSE * 2];
    size_t req_len, resp_len = 0;

    memset(result, 0, sizeof(*result));

    req_len = llm_build_request(req_body, sizeof(req_body),
                                model, system_prompt, user_prompt,
                                max_tokens, reasoning_effort);
    if (req_len == 0) {
        result->http_status = -1;
        return -1;
    }

    result->http_status = llm_http_post(ep, req_body, req_len,
                                        resp_buf, sizeof(resp_buf),
                                        &resp_len);
    if (result->http_status != 200) return -1;

    result->text_len = llm_parse_response(result->text, sizeof(result->text),
                                          resp_buf, resp_len);
    result->ok = result->text_len > 0 ? 1 : 0;
    return result->ok ? 0 : -1;
}

/* ---- Agent loop ------------------------------------------------------- */

int llm_agent_loop(const struct llm_endpoint *ep,
                   const char *model,
                   const char *system_prompt,
                   int max_tokens,
                   int max_iterations,
                   llm_prompt_fn prompt_fn,
                   llm_agent_cb result_cb,
                   void *ctx)
{
    struct llm_result result;
    char prompt[LLM_MAX_PROMPT];
    const char *prev = NULL;
    int completed = 0;
    int i;

    for (i = 0; i < max_iterations; i++) {
        if (prompt_fn(prompt, sizeof(prompt), prev, i, ctx) != 0)
            break;

        if (llm_generate(ep, model, system_prompt, prompt,
                         max_tokens, NULL, &result) != 0)
            break;

        prev = result.text;
        completed++;

        if (result_cb && result_cb(&result, i, ctx) != 0)
            break;
    }

    return completed;
}
