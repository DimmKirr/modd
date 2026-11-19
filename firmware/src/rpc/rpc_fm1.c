// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* FM1.* RPC method handlers: firmware inspection and control. */

static void rpc_fm1_get_info(int32_t id, struct rpc_tx *tx)
{
    rpc_response_begin(tx, id);
    rpc_tx_key_str(tx, "firmware", "modd");
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "version", CONFIG_FIRMWARE_VERSION);
    rpc_tx_char(tx, ',');
#if defined(CONFIG_UI_FELUCCA)
    rpc_tx_key_str(tx, "lineage", "felucca");
#elif defined(CONFIG_UI_MELODEE)
    rpc_tx_key_str(tx, "lineage", "melodee");
#else
    rpc_tx_key_str(tx, "lineage", "core");
#endif
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "uptime_ms", fm1_ms);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "ip", net_get_ip());
    rpc_response_end(tx);
}

static void rpc_fm1_mem_read(const char *js, int ntokens,
                             const struct rpc_tok *tokens,
                             int32_t id, struct rpc_tx *tx)
{
    int ai = rpc_json_find(js, tokens, ntokens, "address");
    int li = rpc_json_find(js, tokens, ntokens, "length");
    if (ai < 0 || li < 0) {
        rpc_error(tx, id, -32602, "Missing address or length");
        return;
    }
    uint32_t addr = (uint32_t)rpc_tok_int(js, &tokens[ai]);
    uint32_t len  = (uint32_t)rpc_tok_int(js, &tokens[li]);
    if (len > 768) len = 768; /* keep response under WS_MAX_FRAME with base64 */

    rpc_response_begin(tx, id);
    rpc_tx_key_uint(tx, "address", addr);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "length", len);
    rpc_tx_char(tx, ',');
    rpc_tx_char(tx, '"');
    rpc_tx_raw(tx, "data");
    rpc_tx_raw(tx, "\":\"");
    {
        char b64[1100];
        base64_encode((const uint8_t *)addr, len, b64);
        rpc_tx_raw(tx, b64);
    }
    rpc_tx_char(tx, '"');
    rpc_response_end(tx);
}

static void rpc_fm1_mem_write(const char *js, int ntokens,
                              const struct rpc_tok *tokens,
                              int32_t id, struct rpc_tx *tx)
{
    int ai = rpc_json_find(js, tokens, ntokens, "address");
    int di = rpc_json_find(js, tokens, ntokens, "data");
    if (ai < 0 || di < 0) {
        rpc_error(tx, id, -32602, "Missing address or data");
        return;
    }
    uint32_t addr = (uint32_t)rpc_tok_int(js, &tokens[ai]);
    char data_str[1400];
    uint32_t dlen = rpc_tok_str(js, &tokens[di], data_str, sizeof(data_str));
    uint8_t decoded[1024];
    uint32_t written = base64_decode(data_str, dlen, decoded);

    uint32_t i;
    uint8_t *dst = (uint8_t *)addr;
    for (i = 0; i < written; i++) dst[i] = decoded[i];

    rpc_response_begin(tx, id);
    rpc_tx_key_uint(tx, "written", written);
    rpc_response_end(tx);
}

static void rpc_fm1_audio_metrics(int32_t id, struct rpc_tx *tx)
{
    rpc_response_begin(tx, id);
#ifdef CONFIG_AUDIO
    rpc_tx_key_uint(tx, "render_ticks_last", fm1_audio_irq_elapsed_ticks);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "sample_rate", 44100);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "block_size", 128);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "voice_count", (int32_t)NVOICE);
#else
    rpc_tx_key_str(tx, "error", "audio not configured");
#endif
    rpc_response_end(tx);
}

static void rpc_fm1_get_engine_state(int32_t id, struct rpc_tx *tx)
{
    rpc_response_begin(tx, id);
#ifdef CONFIG_AUDIO
    rpc_tx_key_int(tx, "engine_index", (int32_t)trk[song.sel].engine);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "voice_count", (int32_t)NVOICE);
#else
    rpc_tx_key_str(tx, "error", "audio not configured");
#endif
    rpc_response_end(tx);
}

static void rpc_fm1_send_midi(const char *js, int ntokens,
                              const struct rpc_tok *tokens,
                              int32_t id, struct rpc_tx *tx)
{
    /* params.data is an array of byte values: [status, data1, data2] */
    int di = rpc_json_find(js, tokens, ntokens, "data");
    if (di < 0 || tokens[di].type != 1) { /* type 1 = array */
        rpc_error(tx, id, -32602, "Missing data array");
        return;
    }

    /* parse the array contents (re-tokenize the array body) */
    uint32_t arr_start = tokens[di].start;
    uint32_t arr_end = tokens[di].end;
    const char *arr_js = js + arr_start;
    uint32_t arr_len = arr_end - arr_start;

    struct rpc_tok arr_toks[8];
    int an = rpc_json_parse(arr_js, arr_len, arr_toks, 8);

    /* first token is the array itself, values follow */
    uint8_t midi_msg[8];
    int midi_len = 0;
    int ti;
    for (ti = 1; ti < an && midi_len < 8; ti++) {
        if (arr_toks[ti].type == 3) { /* primitive (number) */
            midi_msg[midi_len++] = (uint8_t)rpc_tok_int(arr_js, &arr_toks[ti]);
        }
    }

    if (midi_len >= 2) {
        uint8_t status = midi_msg[0];
        uint8_t d1 = midi_msg[1];
        uint8_t d2 = midi_len > 2 ? midi_msg[2] : 0;
#ifdef CONFIG_AUDIO
        uint8_t type = status & 0xF0;
        if (type == 0x90 && d2 > 0) {
            trk_note_on(&trk[song.sel], d1, d2);
        } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
            trk_note_off(&trk[song.sel], d1);
        }
#endif
        (void)d1; (void)d2; (void)status;
    }

    rpc_response_begin(tx, id);
    rpc_tx_key_bool(tx, "ok", 1);
    rpc_response_end(tx);
}

static void rpc_fm1_press_button(const char *js, int ntokens,
                                 const struct rpc_tok *tokens,
                                 int32_t id, struct rpc_tx *tx)
{
    int bi = rpc_json_find(js, tokens, ntokens, "button");
    if (bi < 0) {
        rpc_error(tx, id, -32602, "Missing button");
        return;
    }
    int32_t btn_idx = rpc_tok_int(js, &tokens[bi]);

#ifdef CONFIG_INPUT
    /* simulate button press via input state */
    fm1_in.buttons |= (1u << (uint32_t)btn_idx);
#endif

    rpc_response_begin(tx, id);
    rpc_tx_key_bool(tx, "ok", 1);
    rpc_response_end(tx);
}

static void rpc_fm1_turn_encoder(const char *js, int ntokens,
                                 const struct rpc_tok *tokens,
                                 int32_t id, struct rpc_tx *tx)
{
    int ei = rpc_json_find(js, tokens, ntokens, "encoder");
    int ci = rpc_json_find(js, tokens, ntokens, "clicks");
    if (ei < 0 || ci < 0) {
        rpc_error(tx, id, -32602, "Missing encoder or clicks");
        return;
    }

    (void)ei; (void)ci; /* TODO: wire to fm1_enc when encoder injection API exists */

    rpc_response_begin(tx, id);
    rpc_tx_key_bool(tx, "ok", 1);
    rpc_response_end(tx);
}

static int rpc_streq_nocase(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; a[i] && b[i]; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void rpc_fm1_navigate(const char *js, int ntokens,
                             const struct rpc_tok *tokens,
                             int32_t id, struct rpc_tx *tx)
{
    int ui = rpc_json_find(js, tokens, ntokens, "url");
    if (ui < 0) {
        rpc_error(tx, id, -32602, "Missing url");
        return;
    }
    char url[128];
    rpc_tok_str(js, &tokens[ui], url, sizeof(url));

    /* strip modd:// prefix */
    const char *path = url;
    {
        const char *prefix = "modd://";
        uint32_t pi;
        int match = 1;
        for (pi = 0; prefix[pi]; pi++) {
            if (url[pi] != prefix[pi]) { match = 0; break; }
        }
        if (match) path = url + pi;
    }

    /* split path into page and sub: "settings/midi" -> page="settings", sub="midi" */
    char page[32], sub[32];
    {
        uint32_t i = 0, j = 0;
        while (path[i] && path[i] != '/' && j < 31) page[j++] = path[i++];
        page[j] = '\0';
        if (path[i] == '/') i++;
        j = 0;
        while (path[i] && j < 31) sub[j++] = path[i++];
        sub[j] = '\0';
    }

#ifdef CONFIG_UI_MODD
    /* find page by name */
    uint32_t i;
    int found = 0;
    for (i = 0; i < nav_npage; i++) {
        if (rpc_streq_nocase(nav_pages[i]->name, page)) {
            nav_set_page(i);
            found = 1;
            break;
        }
    }

    if (!found) {
        rpc_error(tx, id, -32602, "Unknown page");
        return;
    }

    /* handle sub-navigation */
    if (sub[0]) {
        if (rpc_streq_nocase(page, "menu")) {
            uint32_t t;
            for (t = 0; t < SMTAB_COUNT; t++) {
                if (rpc_streq_nocase(SMTAB_NAME[t], sub)) {
                    mset.tab = (uint8_t)t;
                    nav_dirty = 1;
                    break;
                }
            }
        }
        (void)sub;
    }

    rpc_response_begin(tx, id);
    rpc_tx_key_str(tx, "page", nav_pages[nav_cur]->name);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "index", nav_cur);
    rpc_response_end(tx);
#else
    rpc_error(tx, id, -32601, "UI not configured");
#endif
}

static void rpc_fm1_get_log(int32_t id, struct rpc_tx *tx)
{
    rpc_response_begin(tx, id);
#ifdef CONFIG_DEBUG_LOG_BUF
    rpc_tx_key_uint(tx, "count", modd_log_count);
    rpc_tx_char(tx, ',');
    rpc_tx_char(tx, '"');
    rpc_tx_raw(tx, "entries");
    rpc_tx_raw(tx, "\":[");
    {
        uint32_t n = modd_log_count < 10 ? modd_log_count : 10;
        uint32_t start = modd_log_count > n ? modd_log_count - n : 0;
        uint32_t i;
        int first = 1;
        for (i = start; i < modd_log_count; i++) {
            if (!first) rpc_tx_char(tx, ',');
            first = 0;
            rpc_tx_char(tx, '"');
            const char *line = modd_log_line(i);
            if (line) rpc_tx_raw(tx, line);
            rpc_tx_char(tx, '"');
        }
    }
    rpc_tx_char(tx, ']');
#else
    rpc_tx_key_str(tx, "error", "debug log not configured");
#endif
    rpc_response_end(tx);
}
