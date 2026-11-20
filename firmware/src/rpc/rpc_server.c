// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* RPC server: TCP listen -> WebSocket upgrade -> JSON-RPC dispatch.
 * Called from the main loop via rpc_poll(). */

#ifndef CONFIG_RPC_PORT
#define CONFIG_RPC_PORT 9222
#endif

static uint8_t rpc_active;

static void rpc_reset_session(void)
{
    modd_log("rpc: session reset");
    ws_init();
}

static void rpc_init(void)
{
    if (rpc_active) return;
    ws_init();
    rpc_on_disconnect = rpc_reset_session;
    rpc_tcp_listen(CONFIG_RPC_PORT);
    rpc_active = 1;
    modd_log("rpc: listening");
}

static void rpc_dispatch(const char *js, uint32_t len, struct rpc_tx *tx)
{
    struct rpc_tok tokens[RPC_JSON_MAX_TOKENS];
    int ntokens = rpc_json_parse(js, len, tokens, RPC_JSON_MAX_TOKENS);
    if (ntokens < 3) {
        rpc_error(tx, 0, -32700, "Parse error");
        return;
    }

    /* extract id and method */
    int id_idx = rpc_json_find(js, tokens, ntokens, "id");
    int meth_idx = rpc_json_find(js, tokens, ntokens, "method");
    if (id_idx < 0 || meth_idx < 0) {
        rpc_error(tx, 0, -32600, "Missing id or method");
        return;
    }

    int32_t id = rpc_tok_int(js, &tokens[id_idx]);
    char method[64];
    rpc_tok_str(js, &tokens[meth_idx], method, sizeof(method));

    /* find params sub-object (optional) */
    int params_idx = rpc_json_find(js, tokens, ntokens, "params");
    const char *params_js = js;
    const struct rpc_tok *params_toks = tokens;
    int params_ntokens = ntokens;

    /* re-tokenize params if present */
    struct rpc_tok ptokens[RPC_JSON_MAX_TOKENS];
    int pntokens = 0;
    if (params_idx >= 0 && tokens[params_idx].type == 0) {
        const char *pjs = js + tokens[params_idx].start;
        uint32_t plen = tokens[params_idx].end - tokens[params_idx].start;
        pntokens = rpc_json_parse(pjs, plen, ptokens, RPC_JSON_MAX_TOKENS);
        params_js = pjs;
        params_toks = ptokens;
        params_ntokens = pntokens;
    }

    /* dispatch by method name */
    if (method[0] == 'F' && method[1] == 'M' && method[2] == '1' && method[3] == '.') {
        const char *sub = method + 4;
        uint32_t i;
        int match = 1;

        /* FM1.getInfo */
        {
            const char *cmp = "getInfo";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_get_info(id, tx); return; }
        }
        /* FM1.memoryRead */
        {
            const char *cmp = "memoryRead";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_mem_read(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.memoryWrite */
        {
            const char *cmp = "memoryWrite";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_mem_write(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.getAudioMetrics */
        {
            const char *cmp = "getAudioMetrics";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_audio_metrics(id, tx); return; }
        }
        /* FM1.getEngineState */
        {
            const char *cmp = "getEngineState";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_get_engine_state(id, tx); return; }
        }
        /* FM1.sendMidi */
        {
            const char *cmp = "sendMidi";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_send_midi(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.pressButton */
        {
            const char *cmp = "pressButton";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_press_button(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.turnEncoder */
        {
            const char *cmp = "turnEncoder";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_turn_encoder(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.getLog */
        {
            const char *cmp = "getLog";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_get_log(id, tx); return; }
        }
        /* FM1.navigate */
        {
            const char *cmp = "navigate";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { rpc_fm1_navigate(params_js, params_ntokens, params_toks, id, tx); return; }
        }
    }

    /* standard-ish domains (minimal stubs) */

    /* Target.getTargetInfo (Playwright probes this) */
    {
        const char *cmp = "Target.getTargetInfo";
        uint32_t i;
        int match = 1;
        for (i = 0; cmp[i]; i++) { if (method[i] != cmp[i]) { match = 0; break; } }
        if (match && method[i] == '\0') {
            rpc_response_begin(tx, id);
            rpc_tx_raw(tx, "\"targetInfo\":{");
            rpc_tx_key_str(tx, "targetId", "fm1-0");
            rpc_tx_char(tx, ',');
            rpc_tx_key_str(tx, "type", "other");
            rpc_tx_char(tx, ',');
            rpc_tx_key_str(tx, "title", "FM-1 Synthesizer");
            rpc_tx_char(tx, ',');
            rpc_tx_key_str(tx, "url", "fm1://local");
            rpc_tx_char(tx, ',');
            rpc_tx_key_bool(tx, "attached", 1);
            rpc_tx_char(tx, '}');
            rpc_response_end(tx);
            return;
        }
    }

    rpc_error(tx, id, -32601, "Method not found");
}

/* --- HTTP response helpers --- */

static void rpc_http_respond(struct rpc_tx *tx, int status)
{
    const char *status_line = (status == 200) ? "200 OK" :
                              (status == 400) ? "400 Bad Request" :
                              (status == 404) ? "404 Not Found" :
                              "500 Internal Server Error";
    static char hdr[192];
    uint32_t hi = 0;
    const char *p;
    p = "HTTP/1.1 ";       while (*p) hdr[hi++] = *p++;
    p = status_line;        while (*p) hdr[hi++] = *p++;
    p = "\r\nContent-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "Content-Length: ";
    while (*p && hi < sizeof(hdr) - 1) hdr[hi++] = *p++;
    {
        char cl[12];
        uint32_t v = tx->len, ci = 0;
        if (v == 0) { cl[ci++] = '0'; }
        else { char tmp[12]; uint32_t ti = 0;
               while (v) { tmp[ti++] = '0' + (v % 10); v /= 10; }
               while (ti) cl[ci++] = tmp[--ti]; }
        uint32_t j;
        for (j = 0; j < ci && hi < sizeof(hdr) - 1; j++) hdr[hi++] = cl[j];
    }
    p = "\r\n\r\n";
    while (*p && hi < sizeof(hdr) - 1) hdr[hi++] = *p++;
    if (!rpc_conn_pcb) return;
    err_t e = tcp_write(rpc_conn_pcb, hdr, (uint16_t)hi,
                        TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    if (e == ERR_OK && tx->len > 0)
        e = tcp_write(rpc_conn_pcb, tx->buf, (uint16_t)tx->len,
                      TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK) tcp_output(rpc_conn_pcb);
    rpc_tcp_close();
}

static void rpc_http_json_ok(struct rpc_tx *tx)
{
    rpc_tx_init(tx);
    rpc_tx_char(tx, '{');
}

static void rpc_http_json_end(struct rpc_tx *tx)
{
    rpc_tx_char(tx, '}');
    rpc_http_respond(tx, 200);
}

static void rpc_http_error(int status, const char *msg)
{
    struct rpc_tx *tx = &rpc_tx_static;
    rpc_tx_init(tx);
    rpc_tx_raw(tx, "{\"error\":\"");
    rpc_tx_raw(tx, msg);
    rpc_tx_raw(tx, "\"}");
    rpc_http_respond(tx, status);
}

/* --- HTTP path parser --- */

static uint32_t rpc_http_parse_path(const uint8_t *data, uint32_t len,
                                     char *path, uint32_t path_max)
{
    /* skip "GET " */
    uint32_t i = 0;
    while (i < len && data[i] != ' ') i++;
    i++;
    uint32_t j = 0;
    while (i < len && data[i] != ' ' && data[i] != '?' && j < path_max - 1)
        path[j++] = (char)data[i++];
    path[j] = '\0';
    return j;
}

/* Handle HTTP request: REST API, WebSocket upgrade, or /json/version */
static int rpc_handle_http(const uint8_t *data, uint32_t len)
{
    /* check if this is a WebSocket upgrade request */
    uint32_t i;
    int is_upgrade = 0;
    {
        const char *needle = "Upgrade: websocket";
        uint32_t nlen = 18;
        for (i = 0; i + nlen <= len; i++) {
            uint32_t j, m = 1;
            for (j = 0; j < nlen; j++) {
                char a = (char)data[i+j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { m = 0; break; }
            }
            if (m) { is_upgrade = 1; break; }
        }
    }

    if (is_upgrade) {
        if (ws_handle_upgrade(data, len) == 0) {
            ws_state = WS_STATE_OPEN;
            return 1;
        }
        return -1;
    }

    /* every other request: a REST route (api_ctl.c), which also answers unknown paths */
    static char path[128];
    rpc_http_parse_path(data, len, path, sizeof(path));
    api_ctl_route(path);
    return 0;
}

/* Main poll: called from firmware main loop */
static void rpc_poll(void)
{
    if (!rpc_active) return;

    api_ctl_tick();             /* timed releases of keys pressed through the API */

    /* read TCP data */
    static uint8_t tmp[512];
    uint32_t n = rpc_tcp_read(tmp, sizeof(tmp));
    if (n == 0) return;

    if (ws_state == WS_STATE_HTTP) {
        /* accumulate HTTP request */
        uint32_t i;
        for (i = 0; i < n && ws_rx_len < WS_MAX_FRAME; i++)
            ws_rx_buf[ws_rx_len++] = tmp[i];

        /* check for complete HTTP request (ends with \r\n\r\n) */
        if (ws_rx_len >= 4 &&
            ws_rx_buf[ws_rx_len-4] == '\r' && ws_rx_buf[ws_rx_len-3] == '\n' &&
            ws_rx_buf[ws_rx_len-2] == '\r' && ws_rx_buf[ws_rx_len-1] == '\n') {
            rpc_handle_http(ws_rx_buf, ws_rx_len);
            ws_rx_len = 0;
        }
        return;
    }

    if (ws_state == WS_STATE_OPEN) {
        /* accumulate WebSocket frames */
        uint32_t i;
        for (i = 0; i < n && ws_rx_len < WS_MAX_FRAME; i++)
            ws_rx_buf[ws_rx_len++] = tmp[i];

        /* try to decode a complete frame */
        while (ws_rx_len > 0) {
            uint8_t opcode;
            uint8_t *payload;
            uint32_t payload_len;
            int consumed = ws_decode_frame(ws_rx_buf, ws_rx_len,
                                           &opcode, &payload, &payload_len);
            if (consumed < 0) break; /* incomplete */

            if (opcode == WS_OP_TEXT) {
                struct rpc_tx *tx = &rpc_tx_static;
                rpc_dispatch((const char *)payload, payload_len, tx);
                if (tx->len > 0)
                    ws_send_text((const uint8_t *)tx->buf, tx->len);
            } else if (opcode == WS_OP_PING) {
                ws_send_pong(payload, payload_len);
            } else if (opcode == WS_OP_CLOSE) {
                uint8_t close_frame[4] = {0x88, 0x02, 0x03, 0xE8};
                rpc_tcp_send(close_frame, 4);
                rpc_tcp_close();
                ws_init();
                return;
            }

            /* shift consumed bytes out of rx buffer */
            uint32_t rem = ws_rx_len - (uint32_t)consumed;
            for (i = 0; i < rem; i++)
                ws_rx_buf[i] = ws_rx_buf[(uint32_t)consumed + i];
            ws_rx_len = rem;
        }
    }
}
