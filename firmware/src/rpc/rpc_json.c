// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Minimal JSON parser for RPC messages.
 * Only handles the subset needed: {"id":N,"method":"X","params":{...}}
 * No malloc, no recursion, no floats. */

#define RPC_JSON_MAX_TOKENS 32

struct rpc_tok {
    uint16_t start;
    uint16_t end;
    uint8_t  type; /* 0=obj, 1=arr, 2=str, 3=prim */
};

/* Minimal tokenizer: finds strings, numbers, objects, arrays.
 * Returns token count or -1 on error. */
static int rpc_json_parse(const char *js, uint32_t len,
                          struct rpc_tok *tokens, int max_tokens)
{
    int count = 0;
    uint32_t i = 0;

    while (i < len && count < max_tokens) {
        char c = js[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == ':') {
            i++;
            continue;
        }
        if (c == '{' || c == '[') {
            tokens[count].start = (uint16_t)i;
            tokens[count].type = (c == '{') ? 0 : 1;
            /* find matching close */
            int depth = 1;
            uint32_t j = i + 1;
            while (j < len && depth > 0) {
                if (js[j] == '{' || js[j] == '[') depth++;
                else if (js[j] == '}' || js[j] == ']') depth--;
                else if (js[j] == '"') {
                    j++;
                    while (j < len && js[j] != '"') {
                        if (js[j] == '\\') j++;
                        j++;
                    }
                }
                j++;
            }
            tokens[count].end = (uint16_t)j;
            count++;
            i++;
            continue;
        }
        if (c == '"') {
            tokens[count].start = (uint16_t)(i + 1);
            tokens[count].type = 2; /* string */
            i++;
            while (i < len && js[i] != '"') {
                if (js[i] == '\\') i++;
                i++;
            }
            tokens[count].end = (uint16_t)i;
            count++;
            i++; /* skip closing quote */
            continue;
        }
        /* number, true, false, null */
        tokens[count].start = (uint16_t)i;
        tokens[count].type = 3; /* primitive */
        while (i < len && js[i] != ',' && js[i] != '}' && js[i] != ']' &&
               js[i] != ' ' && js[i] != '\t' && js[i] != '\n' && js[i] != '\r')
            i++;
        tokens[count].end = (uint16_t)i;
        count++;
        continue;
    }
    return count;
}

/* Compare token string content */
static int rpc_tok_eq(const char *js, const struct rpc_tok *t, const char *s)
{
    uint32_t tlen = t->end - t->start;
    uint32_t slen = 0;
    while (s[slen]) slen++;
    if (tlen != slen) return 0;
    uint32_t i;
    for (i = 0; i < tlen; i++)
        if (js[t->start + i] != s[i]) return 0;
    return 1;
}

/* Extract integer value from a primitive token */
static int32_t rpc_tok_int(const char *js, const struct rpc_tok *t)
{
    int32_t val = 0;
    int neg = 0;
    uint32_t i = t->start;
    if (i < t->end && js[i] == '-') { neg = 1; i++; }
    if (i + 1 < t->end && js[i] == '0' && (js[i+1] == 'x' || js[i+1] == 'X')) {
        i += 2;
        while (i < t->end) {
            char c = js[i];
            if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
            else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
            else break;
            i++;
        }
    } else {
        while (i < t->end) {
            char c = js[i];
            if (c < '0' || c > '9') break;
            val = val * 10 + (c - '0');
            i++;
        }
    }
    return neg ? -val : val;
}

/* Extract string token content into a buffer (not null-terminated unless fits) */
static uint32_t rpc_tok_str(const char *js, const struct rpc_tok *t,
                            char *buf, uint32_t buf_size)
{
    uint32_t tlen = t->end - t->start;
    uint32_t n = tlen < buf_size ? tlen : buf_size;
    uint32_t i;
    for (i = 0; i < n; i++) buf[i] = js[t->start + i];
    if (n < buf_size) buf[n] = '\0';
    return n;
}

/* Find a key in a flat object (tokens[0] must be the object).
 * Returns token index of the value, or -1. */
static int rpc_json_find(const char *js, const struct rpc_tok *tokens,
                         int ntokens, const char *key)
{
    int i;
    for (i = 1; i + 1 < ntokens; i++) {
        if (tokens[i].type == 2 && rpc_tok_eq(js, &tokens[i], key))
            return i + 1;
    }
    return -1;
}

/* --- JSON response builder --- */

/* scratch buffers of the RPC server: in the pool, not in .bss (96 KiB RAM, full) or on a task stack */
#define RPC_SCRATCH __attribute__((section(".pool")))

#define RPC_TX_MAX 2048

struct rpc_tx {
    char buf[RPC_TX_MAX];
    uint32_t len;
};

static struct rpc_tx rpc_tx_static;

static void rpc_tx_init(struct rpc_tx *tx)
{
    tx->len = 0;
}

static void rpc_tx_raw(struct rpc_tx *tx, const char *s)
{
    while (*s && tx->len < RPC_TX_MAX - 1)
        tx->buf[tx->len++] = *s++;
}

static void rpc_tx_char(struct rpc_tx *tx, char c)
{
    if (tx->len < RPC_TX_MAX - 1)
        tx->buf[tx->len++] = c;
}

static void rpc_tx_int(struct rpc_tx *tx, int32_t v)
{
    char tmp[12];
    int neg = 0, i = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { tmp[i++] = '0'; }
    else { while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; } }
    if (neg) rpc_tx_char(tx, '-');
    while (i > 0) rpc_tx_char(tx, tmp[--i]);
}

static void rpc_tx_uint(struct rpc_tx *tx, uint32_t v)
{
    char tmp[12];
    int i = 0;
    if (v == 0) { tmp[i++] = '0'; }
    else { while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; } }
    while (i > 0) rpc_tx_char(tx, tmp[--i]);
}

static void rpc_tx_key_str(struct rpc_tx *tx, const char *key, const char *val)
{
    rpc_tx_char(tx, '"');
    rpc_tx_raw(tx, key);
    rpc_tx_raw(tx, "\":\"");
    rpc_tx_raw(tx, val);
    rpc_tx_char(tx, '"');
}

static void rpc_tx_key_int(struct rpc_tx *tx, const char *key, int32_t val)
{
    rpc_tx_char(tx, '"');
    rpc_tx_raw(tx, key);
    rpc_tx_raw(tx, "\":");
    rpc_tx_int(tx, val);
}

static void rpc_tx_key_uint(struct rpc_tx *tx, const char *key, uint32_t val)
{
    rpc_tx_char(tx, '"');
    rpc_tx_raw(tx, key);
    rpc_tx_raw(tx, "\":");
    rpc_tx_uint(tx, val);
}

static void rpc_tx_key_bool(struct rpc_tx *tx, const char *key, int val)
{
    rpc_tx_char(tx, '"');
    rpc_tx_raw(tx, key);
    rpc_tx_raw(tx, "\":");
    rpc_tx_raw(tx, val ? "true" : "false");
}

static void rpc_response_begin(struct rpc_tx *tx, int32_t id)
{
    rpc_tx_init(tx);
    rpc_tx_raw(tx, "{\"id\":");
    rpc_tx_int(tx, id);
    rpc_tx_raw(tx, ",\"result\":{");
}

static void rpc_response_end(struct rpc_tx *tx)
{
    rpc_tx_raw(tx, "}}");
}

static void rpc_error(struct rpc_tx *tx, int32_t id, int32_t code, const char *msg)
{
    rpc_tx_init(tx);
    rpc_tx_raw(tx, "{\"id\":");
    rpc_tx_int(tx, id);
    rpc_tx_raw(tx, ",\"error\":{\"code\":");
    rpc_tx_int(tx, code);
    rpc_tx_raw(tx, ",\"message\":\"");
    rpc_tx_raw(tx, msg);
    rpc_tx_raw(tx, "\"}}");
}

static void rpc_event_begin(struct rpc_tx *tx, const char *method)
{
    rpc_tx_init(tx);
    rpc_tx_raw(tx, "{\"method\":\"");
    rpc_tx_raw(tx, method);
    rpc_tx_raw(tx, "\",\"params\":{");
}

static void rpc_event_end(struct rpc_tx *tx)
{
    rpc_tx_raw(tx, "}}");
}
