// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* The REST API: every route, its handler and its parameters.
 *
 * GET only, path segments, so curl and hurl work. rpc_server.c hands every HTTP
 * request that is not a WebSocket upgrade to api_ctl_route(), and calls
 * api_ctl_tick() on every poll. Included after api_pages.c, before rpc_server.c.
 *
 * API_ROUTES[] (at the end) is the list of routes. tools/codegen/gen_openapi.py
 * reads this file and api_pages.c to generate the OpenAPI document that
 * `task build` writes to build/<defconfig>/openapi.json and GET /openapi.json
 * serves. It reads:
 *   - the routes and their #ifdef guards from API_ROUTES[],
 *   - each {param} from the api_arg_*() call that checks it (name, bounds, names),
 *   - the reply from the JSON the handler writes: rpc_tx_key_*(), api_stream_*(),
 *     literal rpc_tx_raw()/rpc_tx_char(), and the helpers they are in.
 * Rules for a handler it can read, and what to do when the build stops on one:
 * docs/TESTING.md, "Writing a REST handler". Prose (summaries, units) lives in
 * docs/api/openapi.overlay.yaml.
 */

static void rpc_http_error(int status, const char *msg);   /* rpc_server.c */
static void rpc_http_json_ok(struct rpc_tx *tx);
static void rpc_http_json_end(struct rpc_tx *tx);

/* ---------------------------------------------------- streamed replies --- */

static char api_sbuf[512] RPC_SCRATCH;
static uint32_t api_slen;
static int api_sfail;

static void api_stream_flush(void)
{
    if (!api_sfail && api_slen && rpc_tcp_send((const uint8_t *)api_sbuf, api_slen) < 0)
        api_sfail = 1;
    api_slen = 0;
}

static void api_stream_put(const char *s, uint32_t n)
{
    while (n--) {
        if (api_slen == sizeof api_sbuf)
            api_stream_flush();
        api_sbuf[api_slen++] = *s++;
    }
}

static void api_stream_str(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    api_stream_put(s, n);
}

static void api_stream_uint(uint32_t v)
{
    char tmp[11];
    uint32_t i = sizeof tmp;
    do { tmp[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    api_stream_put(tmp + i, sizeof tmp - i);
}

static void api_stream_int(int32_t v)
{
    if (v < 0) { api_stream_put("-", 1); api_stream_uint((uint32_t)-v); }
    else api_stream_uint((uint32_t)v);
}

/* a JSON string, quoted and escaped */
static void api_stream_jstr(const char *s)
{
    static const char hex[] = "0123456789abcdef";
    api_stream_put("\"", 1);
    for (; s && *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == '"' || c == '\\') {
            char e[2] = {'\\', (char)c};
            api_stream_put(e, 2);
        } else if (c < 0x20u || c >= 0x7fu) {
            char e[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15u]};
            api_stream_put(e, 6);
        } else {
            api_stream_put((const char *)&c, 1);
        }
    }
    api_stream_put("\"", 1);
}

static void api_stream_b64(const uint8_t *p, uint32_t n)   /* n a multiple of 3 except at the end */
{
    static char b64[520] RPC_SCRATCH;   /* off the stack: rpc_tcp_send drains lwIP below this frame */
    while (n) {
        uint32_t chunk = n > 384u ? 384u : n;
        api_stream_put(b64, base64_encode(p, chunk, b64));
        p += chunk;
        n -= chunk;
    }
}

static void api_stream_begin(void)
{
    static const char hdr[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n";
    api_slen = 0;
    api_sfail = rpc_tcp_send((const uint8_t *)hdr, sizeof hdr - 1) < 0;
}

static void api_stream_end(void)
{
    api_stream_flush();
    rpc_tcp_close();
}

/* a JSON string value into a buffered reply (rpc_tx_key_str has the key too) */
static void api_tx_jstr(struct rpc_tx *tx, const char *s)
{
    rpc_tx_char(tx, '"');
    rpc_tx_raw(tx, s ? s : "");
    rpc_tx_char(tx, '"');
}

/* --------------------------------------------------- number parsing --- */

static int api_dec(const char *s, int32_t *out)
{
    int neg = 0;
    int32_t v = 0;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10 + (*s - '0');
        if (v > 100000000) return 0;
    }
    *out = neg ? -v : v;
    return 1;
}

static int api_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ---------------------------------------------------- route arguments --- */

/* a[i] is the i-th {param} of the route that matched, NULL past the route's last
 * one. Each api_arg_*() checks one: on a bad value it replies 400 (404 for a name
 * that names nothing) and returns 0, and the handler returns. gen_openapi.py takes
 * the parameter's schema from these calls: keep the name a string literal and the
 * bounds literals or #defines. */

static void api_arg_fail(int status, const char *name)
{
    static char msg[48];
    static const char tail[] = " (see /openapi.json)";
    uint32_t n = 0, i;
    const char *p = "bad ";
    while (*p) msg[n++] = *p++;
    for (i = 0; name[i] && n < sizeof msg - sizeof tail; i++) msg[n++] = name[i];
    for (i = 0; tail[i]; i++) msg[n++] = tail[i];
    msg[n] = '\0';
    rpc_http_error(status, msg);
}

static const char *api_arg_str(const char *s, const char *name)
{
    (void)name;
    return s ? s : "";
}

static int api_arg_int(const char *s, const char *name, int32_t lo, int32_t hi, int32_t *out)
{
    if (s && api_dec(s, out) && *out >= lo && *out <= hi)
        return 1;
    api_arg_fail(400, name);
    return 0;
}

/* an integer lo..hi, or `word` (its value: word_value) */
static int api_arg_int_or(const char *s, const char *name, int32_t lo, int32_t hi,
                          const char *word, int32_t word_value, int32_t *out)
{
    if (s && rpc_streq_nocase(s, word)) {
        *out = word_value;
        return 1;
    }
    if (s && api_dec(s, out) && *out >= lo && *out <= hi)
        return 1;
    api_arg_fail(404, name);
    return 0;
}

/* a name from table[] (case ignored) or its index */
static int api_arg_enum(const char *s, const char *name, const char *const *table, uint32_t n,
                        int32_t *out)
{
    uint32_t i;
    for (i = 0; s && i < n; i++)
        if (rpc_streq_nocase(table[i], s)) {
            *out = (int32_t)i;
            return 1;
        }
    if (s && api_dec(s, out) && *out >= 0 && (uint32_t)*out < n)
        return 1;
    api_arg_fail(404, name);
    return 0;
}

/* up to 8 hex digits, 0x optional */
static int api_arg_hex32(const char *s, const char *name, uint32_t *out)
{
    uint32_t v = 0, n = 0;
    if (s && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; s && *s; s++, n++) {
        int h = api_hexval(*s);
        if (h < 0 || n >= 8u) break;
        v = v << 4 | (uint32_t)h;
    }
    if (s && !*s && n) {
        *out = v;
        return 1;
    }
    api_arg_fail(400, name);
    return 0;
}

/* 1..max bytes as pairs of hex digits, into buf */
static int api_arg_hexbytes(const char *s, const char *name, uint8_t *buf, uint32_t max, uint32_t *n)
{
    uint32_t k = 0;
    while (s && s[0] && s[1] && k < max) {
        int h = api_hexval(s[0]), l = api_hexval(s[1]);
        if (h < 0 || l < 0) break;
        buf[k++] = (uint8_t)(h << 4 | l);
        s += 2;
    }
    if (s && !*s && k) {
        *n = k;
        return 1;
    }
    api_arg_fail(400, name);
    return 0;
}

/* MIDI note keys "60" or "60,64,67" -> note-key mask (bit n = MIDI 53 + n) */
static int api_arg_notes(const char *s, const char *name, uint32_t *mask)
{
    uint32_t m = 0;
    while (s && *s) {
        int32_t note = 0, digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 3) { note = note * 10 + (*s++ - '0'); digits++; }
        if (!digits || note < 53 || note > 79 || (*s && *s != ',')) { m = 0; break; }
        m |= 1u << (note - 53);
        if (*s == ',') s++;
    }
    if (m) {
        *mask = m;
        return 1;
    }
    api_arg_fail(404, name);
    return 0;
}

/* ------------------------------------------------------------- system --- */

static void api_info(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    (void)a;
    rpc_http_json_ok(tx);
    rpc_tx_key_str(tx, "firmware", "modd");
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "version", CONFIG_FIRMWARE_VERSION);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "uptime_ms", fm1_ms);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "ip", net_get_ip());
    rpc_http_json_end(tx);
}

/* also the reply to any path outside /api and /modd (discovery) */
static void api_json_version(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    (void)a;
    rpc_http_json_ok(tx);
    rpc_tx_key_str(tx, "Browser", "FM-1/modd");
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "Protocol-Version", "1.0");
    rpc_http_json_end(tx);
}

#ifdef CONFIG_RPC_OPENAPI
#include "openapi_json.h"   /* build/<defconfig>/gen: gen_openapi.py, from this file */

static void api_openapi(const char *const *a)
{
    (void)a;
    api_stream_begin();
    api_stream_put(modd_openapi_json, MODD_OPENAPI_JSON_LEN);
    api_stream_end();
}
#endif

/* -------------------------------------------------------------- pages --- */

static void api_pages(const char *const *a)
{
    uint32_t i;
    (void)a;
    api_stream_begin();
    api_stream_str("{\"count\":");
    api_stream_uint(nav_npage);
    api_stream_str(",\"current\":");
    api_stream_uint(nav_cur);
    api_stream_str(",\"pages\":[");
    for (i = 0; i < nav_npage; i++) {
        if (i) api_stream_put(",", 1);
        api_stream_str("{\"index\":");
        api_stream_uint(i);
        api_stream_str(",\"name\":");
        api_stream_jstr(nav_pages[i]->name);
        api_stream_str(",\"title\":");
        api_stream_jstr(nav_pages[i]->title);
        api_stream_str(",\"view\":");
        api_stream_uint(nav_pages[i]->view);
        api_stream_put("}", 1);
    }
    api_stream_str("]}");
    api_stream_end();
}

static void api_page(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    (void)a;
    if (!nav_npage) {
        rpc_http_error(400, "no pages");
        return;
    }
    rpc_http_json_ok(tx);
    rpc_tx_key_bool(tx, "modal", nav_modal != 0);
    rpc_tx_char(tx, ',');
    api_page_fields_json(tx, nav_cur);
    rpc_http_json_end(tx);
}

/* Set one card of a page by slot, without the keys or the encoders. Only the effect page (INSERT) has cards that
 * can be set so far (modd_pages.h); its data is in GET /api/page. */
static void api_page_set(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    const char *page = api_arg_str(a[0], "page");
    int32_t slot, value;
    modd_card_t c[4];
    if (!api_arg_int(a[1], "slot", 0, 7, &slot)) return;
    if (!api_arg_int(a[2], "value", -32768, 32767, &value)) return;
    if (!rpc_streq_nocase(page, "INSERT") || !modd_fx_page_read(c, 4u)) {
        rpc_http_error(404, "page has no cards that can be set");
        return;
    }
    if (modd_fx_page_set((uint32_t)slot, value)) {
        rpc_http_error(400, "slot or value out of range");
        return;
    }
    rpc_http_json_ok(tx);
    rpc_tx_key_str(tx, "page", "INSERT");
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "slot", slot);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "value", value);
    rpc_http_json_end(tx);
}

/* /modd/{page} and /modd/{page}/{sub} */
static void api_goto(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    const char *src = api_arg_str(a[0], "page");
    const char *sub = a[1] ? api_arg_str(a[1], "sub") : "";
    char page[24];
    uint32_t i = 0, n = 0;
    int r;
    for (; *src && n < sizeof page - 1u; src++, n++) {    /* "env%20dest", "env-dest", "env_dest" */
        if (src[0] == '%' && src[1] == '2' && src[2] == '0') { src += 2; page[n] = ' '; }
        else page[n] = (*src == '-' || *src == '_') ? ' ' : *src;
    }
    page[n] = 0;
    r = api_page_goto(page, sub, &i);
    if (r == API_GOTO_NO_PAGE) {
        rpc_http_error(404, "Unknown page");
        return;
    }
    if (r == API_GOTO_NO_SUB) {
        rpc_http_error(404, "Unknown sub-page");
        return;
    }
    rpc_http_json_ok(tx);
    rpc_tx_key_bool(tx, "ok", 1);
    rpc_tx_char(tx, ',');
    api_page_fields_json(tx, i);
    if (sub[0]) {
        rpc_tx_char(tx, ',');
        rpc_tx_key_str(tx, "sub", sub);
    }
    rpc_http_json_end(tx);
}

/* -------------------------------------------------------------- debug --- */

static void api_log(const char *const *a)
{
    (void)a;
    api_stream_begin();
#ifdef CONFIG_DEBUG_LOG_BUF
    {
        uint32_t n = modd_log_count < MODD_LOG_LINES ? modd_log_count : MODD_LOG_LINES;
        uint32_t i, start = modd_log_count - n;
        api_stream_str("{\"count\":");              /* lines ever logged, not the ring's fill */
        api_stream_uint(modd_log_total);
        api_stream_str(",\"entries\":[");
        for (i = start; i < start + n; i++) {
            if (i != start) api_stream_put(",", 1);
            api_stream_jstr(modd_log_line(i));
        }
        api_stream_str("]}");
    }
#else
    api_stream_str("{\"error\":\"debug log not configured\"}");
#endif
    api_stream_end();
}

#ifdef CONFIG_DEBUG_FRAMEBUFFER
/* the shadow framebuffer (emulator builds) */
static void api_screenshot(const char *const *a)
{
    (void)a;
    api_stream_begin();
    api_stream_str("{\"width\":240,\"height\":240,\"format\":\"rgb565\",\"data\":\"");
    api_stream_b64((const uint8_t *)lcd_shadow, LCDFB_W * LCDFB_H * 2u);
    api_stream_str("\"}");
    api_stream_end();
}
#endif

/* -------------------------------------------------------------- audio --- */

static void api_audio(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    (void)a;
    rpc_http_json_ok(tx);
#ifdef CONFIG_AUDIO
    rpc_tx_key_uint(tx, "render_ticks_last", fm1_audio_irq_elapsed_ticks);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "sample_rate", 44100);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "block_size", 128);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "voice_count", (int32_t)NVOICE);
#ifdef CONFIG_RPC_AUDIO_TAP
    {
        uint32_t rms, peak;
        audio_tap_level(&rms, &peak);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "rms", rms);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "peak", peak);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "tap_pos", audio_tap_pos);
    }
#endif
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "master_q12", song.master_q12);
#else
    rpc_tx_key_str(tx, "error", "audio not configured");
#endif
    rpc_http_json_end(tx);
}

#ifdef CONFIG_FX_HOST
/* The effects host's one slot (audio/effects/host/fx_host.c): the main loop applies a request. */
static void api_fx_json(void)
{
    struct rpc_tx *tx = &rpc_tx_static;
    static const char *const st[4] = {"off", "placed", "gated", "refused"};
    const fx_unit_t *u = fx_slot_unit;
    rpc_http_json_ok(tx);
    rpc_tx_key_str(tx, "unit", u ? u->name : "");
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "type", (int32_t)fx_slot_type);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "type_name", u ? u->type_name(fx_slot_type) : "");
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "amount", fx_slot_amount);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "state", st[fx_state_v & 3u]);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "bytes", fx_slot_bytes);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "arena_used", modd_arena_used());
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "refuse", modd_arena_refuse);
    rpc_http_json_end(tx);
}

static void api_fx_slot(const char *const *a)
{
    (void)a;
    api_fx_json();
}

static void api_fx_off(const char *const *a)
{
    (void)a;
    fx_req_off = 1;
    api_fx_json();
}

static void api_fx_on(const char *const *a)
{
    int32_t type, amount;
    if (!api_arg_int(a[0], "type", 0, 7, &type)) return;
    if (!api_arg_int(a[1], "amount", 0, 127, &amount)) return;
    fx_req_unit = 0;
    fx_req_type = (uint32_t)type;
    fx_req_amount = amount;
    fx_req_on = 1;
    api_fx_json();
}

static void api_fx_unit(const char *const *a)
{
    int32_t unit, type, amount;
    if (!api_arg_int(a[0], "unit", 0, 7, &unit)) return;
    if (!api_arg_int(a[1], "type", 0, 7, &type)) return;
    if (!api_arg_int(a[2], "amount", 0, 127, &amount)) return;
    fx_req_unit = (uint32_t)unit;
    fx_req_type = (uint32_t)type;
    fx_req_amount = amount;
    fx_req_on = 1;
    api_fx_json();
}

static void api_fx_refuse(const char *const *a)
{
    int32_t v;
    if (!api_arg_int(a[0], "value", 0, 1, &v)) return;
    modd_arena_refuse = (uint8_t)v;
    api_fx_json();
}
#endif /* CONFIG_FX_HOST */

#ifdef CONFIG_ARENA_MELODEE
/* The one arena (mem/arena_melodee_modd.c): every live block of the resource table, and the last probe's result. The
 * handlers run in the main loop, which is the only context that mutates the table; the audio interrupt never reads it
 * (the fx host's block is reached through a pointer the gate publishes after the get), so no interrupt guard is needed. */
static void api_arena_json(uint32_t ok, uint32_t addr)
{
    struct rpc_tx *tx = &rpc_tx_static;
    uint32_t k, n = 0;
    rpc_http_json_ok(tx);
    rpc_tx_key_uint(tx, "used", resource_used());
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "capacity", RESOURCE_CAPACITY);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "peak", resource_peak);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "failures", resource_failures);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "ok", ok != 0);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "addr", addr);
    rpc_tx_char(tx, ',');
    rpc_tx_raw(tx, "\"blocks\":[");
    for (k = 0; k < RES_COUNT; k++) {
        if (!resource[k].size) continue;
        if (n++) rpc_tx_char(tx, ',');
        rpc_tx_raw(tx, "{");
        rpc_tx_key_uint(tx, "id", k);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "addr", (uint32_t)(uintptr_t)resource[k].ptr);
        rpc_tx_char(tx, ',');
        rpc_tx_key_uint(tx, "size", resource[k].size);
        rpc_tx_char(tx, '}');
    }
    rpc_tx_char(tx, ']');
    rpc_http_json_end(tx);
}

static void api_arena(const char *const *a)
{
    (void)a;
    api_arena_json(0, 0);
}

static const char *const API_ARENA_STYLE[2] = {"melodee", "sloop"};

/* a Melodee-style (resource_get(RES_ENGINE0 + part)) or SLOOP-style (eng_arena_of) request for `bytes` on a part */
static void api_arena_probe(const char *const *a)
{
    int32_t style, part, bytes;
    void *p;
    if (!api_arg_enum(a[0], "style", API_ARENA_STYLE, 2u, &style)) return;
    if (!api_arg_int(a[1], "part", 0, 3, &part)) return;
    if (!api_arg_int(a[2], "bytes", 0, 4194304, &bytes)) return;
    p = style ? modd_arena_probe_sloop((uint32_t)part, 1u, (uint32_t)bytes)
              : modd_arena_probe_melodee((uint32_t)part, (uint32_t)bytes);
    api_arena_json(p != 0, (uint32_t)(uintptr_t)p);
}

static void api_arena_release(const char *const *a)
{
    int32_t part;
    if (!api_arg_int(a[0], "part", 0, 3, &part)) return;
    modd_arena_probe_release((uint32_t)part);
    api_arena_json(1, 0);
}
#endif /* CONFIG_ARENA_MELODEE */

static void api_engine(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    (void)a;
    rpc_http_json_ok(tx);
#ifdef CONFIG_AUDIO
    rpc_tx_key_int(tx, "engine_index", (int32_t)modd_engine_of(&trk[song.sel]));
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "voice_count", (int32_t)NVOICE);
#else
    rpc_tx_key_str(tx, "error", "audio not configured");
#endif
    rpc_http_json_end(tx);
}

#ifdef CONFIG_RPC_AUDIO_TAP
/* the last AUDIO_TAP_N output samples, oldest first; the ring stops while it is read */
static void api_audio_capture(const char *const *a)
{
    uint32_t pos, first;
    (void)a;
    audio_tap_frozen = 1;
    pos = audio_tap_pos;
    first = pos & (AUDIO_TAP_N - 1u);           /* the oldest sample */
    api_stream_begin();
    api_stream_str("{\"rate\":44100,\"format\":\"s16le\",\"channels\":1,\"n\":8192,\"pos\":");
    api_stream_uint(pos);
    api_stream_str(",\"ms\":");
    api_stream_uint(fm1_ms);
#ifdef CONFIG_RPC_INPUT
    api_stream_str(",\"notes\":");              /* note keys down while the ring was taken */
    api_stream_uint(fm1_in.notes);
#endif
    api_stream_str(",\"data\":\"");
    /* the ring from its oldest sample, through a buffer whose size is a multiple of 3
     * so the base64 runs on without padding in the middle */
    {
        static uint8_t b[384] RPC_SCRATCH;
        uint32_t bn = 0, k;
        const uint8_t *ring = (const uint8_t *)audio_tap_ring;
        for (k = 0; k < AUDIO_TAP_N * 2u; k++) {
            b[bn++] = ring[((first * 2u) + k) % (AUDIO_TAP_N * 2u)];
            if (bn == sizeof b) { api_stream_b64(b, bn); bn = 0; }
        }
        if (bn) api_stream_b64(b, bn);
    }
    api_stream_str("\"}");
    api_stream_end();
    audio_tap_frozen = 0;
}
typedef char api_tap_n_is_8192[AUDIO_TAP_N == 8192u ? 1 : -1];   /* "n":8192 above */
#endif

/* -------------------------------------------------------------- input --- */

#ifdef CONFIG_RPC_INPUT
static const char *const API_BTN[14] = {
    "oct-", "oct+", "fx", "scl", "env", "lfo", "edit", "glo", "home", "save", "arp", "seq", "play", "rec"
};
static const char *const API_ENC[7] = {
    "select", "algorithm", "knob1", "knob2", "knob3", "knob4", "presets"
};

#define API_REL_N 8u
/* timed releases: kind 0 = free, 1 buttons, 2 note keys; mask = the keys it lets go */
static struct { uint8_t kind; uint32_t mask, due; } api_rel[API_REL_N];

static void api_rel_cancel(uint8_t kind, uint32_t mask)
{
    uint32_t i;
    for (i = 0; i < API_REL_N; i++)
        if (api_rel[i].kind == kind) {
            api_rel[i].mask &= ~mask;
            if (!api_rel[i].mask)
                api_rel[i].kind = 0;
        }
}

/* press or let go of several keys at once: one store, so the scan sees them on the same frame */
static void api_hold(uint8_t kind, uint32_t mask, int down)
{
    api_rel_cancel(kind, mask);
    if (kind == 1u)
        fm1_inject_buttons = down ? (fm1_inject_buttons | mask) : (fm1_inject_buttons & ~mask);
    else
        fm1_inject_notes = down ? (fm1_inject_notes | mask) : (fm1_inject_notes & ~mask);
}

static int api_tap(uint8_t kind, uint32_t mask, uint32_t ms)
{
    uint32_t i;
    api_hold(kind, mask, 1);
    for (i = 0; i < API_REL_N; i++)
        if (!api_rel[i].kind) {
            api_rel[i].kind = kind;
            api_rel[i].mask = mask;
            api_rel[i].due = fm1_ms + ms;
            return 1;
        }
    api_hold(kind, mask, 0);
    return 0;
}

static void api_ctl_tick(void)
{
    uint32_t i;
    for (i = 0; i < API_REL_N; i++)
        if (api_rel[i].kind && (int32_t)(fm1_ms - api_rel[i].due) >= 0) {
            uint8_t kind = api_rel[i].kind;
            uint32_t mask = api_rel[i].mask;
            api_rel[i].kind = 0;
            api_hold(kind, mask, 0);
        }
}

static void api_input_ok(const char *what, int32_t v)
{
    struct rpc_tx *tx = &rpc_tx_static;
    rpc_http_json_ok(tx);
    rpc_tx_key_bool(tx, "ok", 1);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "input", what);
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "value", v);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "ms", fm1_ms);
    rpc_http_json_end(tx);
}

static void api_input_state(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    uint32_t i, pending = 0, enc = 0;
    (void)a;
    for (i = 0; i < API_REL_N; i++)
        if (api_rel[i].kind) pending++;
    for (i = 0; i < FM1_NENC; i++)
        if (fm1_in.enc_steps[i]) enc++;
    rpc_http_json_ok(tx);
    rpc_tx_key_uint(tx, "ms", fm1_ms);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "buttons", fm1_in.buttons);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "notes", fm1_in.notes);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "inject_buttons", fm1_inject_buttons);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "inject_notes", fm1_inject_notes);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "pending", pending);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "enc_pending", enc);
#ifdef CONFIG_AUDIO
    rpc_tx_char(tx, ',');
    rpc_tx_key_int(tx, "pot", master_pot_override);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "master_q12", song.master_q12);
#endif
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "idle", !pending && !enc && fm1_in.buttons == fm1_inject_buttons &&
                                fm1_in.notes == fm1_inject_notes);
    rpc_http_json_end(tx);
}

/* tap, tap/{ms} and hold/{ms}: press, release after ms (80) of firmware time */
static void api_btn_tap(const char *const *a)
{
    int32_t b, ms = 80;
    if (!api_arg_enum(a[0], "button", API_BTN, 14u, &b)) return;
    if (a[1] && !api_arg_int(a[1], "ms", 10, 60000, &ms)) return;
    if (!api_tap(1u, 1u << b, (uint32_t)ms)) {
        rpc_http_error(400, "too many pending releases");
        return;
    }
    api_input_ok(API_BTN[b], ms);
}

static void api_btn_down(const char *const *a)
{
    int32_t b;
    if (!api_arg_enum(a[0], "button", API_BTN, 14u, &b)) return;
    api_hold(1u, 1u << b, 1);
    api_input_ok(API_BTN[b], 0);
}

static void api_btn_up(const char *const *a)
{
    int32_t b;
    if (!api_arg_enum(a[0], "button", API_BTN, 14u, &b)) return;
    api_hold(1u, 1u << b, 0);
    api_input_ok(API_BTN[b], 0);
}

static void api_enc(const char *const *a)
{
    int32_t e, clicks;
    if (!api_arg_enum(a[0], "encoder", API_ENC, 7u, &e)) return;
    if (!api_arg_int(a[1], "clicks", -1000, 1000, &clicks)) return;
    fm1_enc_inject((uint32_t)e, clicks);
    api_input_ok(API_ENC[e], clicks);
}

/* one note key or a chord (60,64,67): one store, so they land on one scan frame;
 * the reply's value is the note-key mask */
static void api_key_tap(const char *const *a)
{
    uint32_t mask;
    int32_t ms = 80;
    if (!api_arg_notes(a[0], "notes", &mask)) return;
    if (a[1] && !api_arg_int(a[1], "ms", 10, 60000, &ms)) return;
    if (!api_tap(2u, mask, (uint32_t)ms)) {
        rpc_http_error(400, "too many pending releases");
        return;
    }
    api_input_ok("keys", (int32_t)mask);
}

static void api_key_down(const char *const *a)
{
    uint32_t mask;
    if (!api_arg_notes(a[0], "notes", &mask)) return;
    api_hold(2u, mask, 1);
    api_input_ok("keys", (int32_t)mask);
}

static void api_key_up(const char *const *a)
{
    uint32_t mask;
    if (!api_arg_notes(a[0], "notes", &mask)) return;
    api_hold(2u, mask, 0);
    api_input_ok("keys", (int32_t)mask);
}

#ifdef CONFIG_AUDIO
static void api_pot(const char *const *a)
{
    int32_t v;
    if (!api_arg_int(a[0], "value", 0, 1023, &v)) return;
    master_pot_override = v;
    api_input_ok("pot", v);
}

static void api_pot_release(const char *const *a)
{
    (void)a;
    master_pot_override = -1;
    api_input_ok("pot", -1);
}

static void api_track_json(uint32_t k)
{
    struct rpc_tx *tx = &rpc_tx_static;
    uint32_t e = modd_engine_of(&trk[k]);
    rpc_http_json_ok(tx);
    rpc_tx_key_uint(tx, "track", k);
    rpc_tx_char(tx, ',');
    rpc_tx_key_bool(tx, "selected", k == song.sel);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "engine_id", e);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "engine", e < NENGINES && ENGINES[e]->name ? ENGINES[e]->name : "");
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "preset", trk[k].preset);
    rpc_tx_char(tx, ',');
    rpc_tx_raw(tx, "\"engines\":[");
    for (e = 0; e < NENGINES; e++) {
        if (e) rpc_tx_char(tx, ',');
        api_tx_jstr(tx, ENGINES[e]->name);
    }
    rpc_tx_char(tx, ']');
    rpc_http_json_end(tx);
}

static void api_track(const char *const *a)
{
    int32_t k;
    if (!api_arg_int_or(a[0], "track", 0, NTRK - 1, "sel", (int32_t)song.sel, &k)) return;
    api_track_json((uint32_t)k);
}

/* switch a track's engine as the panel does (modd_engine_select) */
static void api_track_engine(const char *const *a)
{
    int32_t k, e = -1;
    uint32_t i;
    const char *name;
    if (!api_arg_int_or(a[0], "track", 0, NTRK - 1, "sel", (int32_t)song.sel, &k)) return;
    name = api_arg_str(a[1], "engine");
    for (i = 0; i < NENGINES; i++)
        if (ENGINES[i]->name && rpc_streq_nocase(ENGINES[i]->name, name)) e = (int32_t)i;
    if (e < 0 && (!api_dec(name, &e) || e < 0 || e >= (int32_t)NENGINES)) {
        rpc_http_error(404, "unknown engine");
        return;
    }
    modd_engine_select(&trk[k], (uint32_t)e);
    nav_dirty = 1;
    api_track_json((uint32_t)k);
}
#endif /* CONFIG_AUDIO */
#endif /* CONFIG_RPC_INPUT */

/* ------------------------------------------------------------- memory --- */

#ifdef CONFIG_RPC_MEM
extern char _etext[];
#define API_RAM_LO 0x01C00000u
#define API_RAM_HI 0x01C80000u
#define API_XIP_LO 0x02001000u
#define API_NEEDLE_MAX 32u

static int api_mem_ok(uint32_t a, uint32_t n)
{
    uint32_t xip_hi = (uint32_t)(uintptr_t)_etext;
    if (a >= API_RAM_LO && a < API_RAM_HI && n <= API_RAM_HI - a) return 1;
    if (a >= API_XIP_LO && a < xip_hi && n <= xip_hi - a) return 1;
    return 0;
}

static uint8_t api_needle[API_NEEDLE_MAX];

static uint32_t api_mem_scan(uint32_t lo, uint32_t hi, uint32_t n)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)lo;
    uint32_t i, j, len = hi - lo;
    if (len < n) return 0;
    for (i = 0; i + n <= len; i++) {
        if (m[i] != api_needle[0]) continue;
        if ((uintptr_t)(m + i) == (uintptr_t)api_needle) continue;   /* the needle itself */
        for (j = 1; j < n && m[i + j] == api_needle[j]; j++) ;
        if (j == n) return lo + i;
    }
    return 0;
}

/* RAM 0x01C00000..0x01C80000 or the mapped image 0x02001000.._etext */
static void api_mem_read(const char *const *a)
{
    uint32_t addr;
    int32_t len;
    if (!api_arg_hex32(a[0], "addr", &addr)) return;
    if (!api_arg_int(a[1], "length", 1, 4096, &len)) return;
    if (!api_mem_ok(addr, (uint32_t)len)) {
        rpc_http_error(400, "mem: outside RAM and the firmware image");
        return;
    }
    api_stream_begin();
    api_stream_str("{\"addr\":");
    api_stream_uint(addr);
    api_stream_str(",\"length\":");
    api_stream_int(len);
    api_stream_str(",\"data\":\"");
    api_stream_b64((const uint8_t *)(uintptr_t)addr, (uint32_t)len);
    api_stream_str("\"}");
    api_stream_end();
}

/* the first address of a byte string: the firmware image, then RAM */
static void api_mem_find(const char *const *a)
{
    struct rpc_tx *tx = &rpc_tx_static;
    uint32_t n, hit;
    const char *region = "xip";
    if (!api_arg_hexbytes(a[0], "hex", api_needle, API_NEEDLE_MAX, &n)) return;
    hit = api_mem_scan(API_XIP_LO, (uint32_t)(uintptr_t)_etext, n);
    if (!hit) {
        region = "ram";
        hit = api_mem_scan(API_RAM_LO, API_RAM_HI, n);
    }
    rpc_http_json_ok(tx);
    rpc_tx_key_bool(tx, "found", hit != 0);
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "addr", hit);
    rpc_tx_char(tx, ',');
    rpc_tx_key_str(tx, "region", hit ? region : "");
    rpc_tx_char(tx, ',');
    rpc_tx_key_uint(tx, "length", n);
    rpc_http_json_end(tx);
}
#endif /* CONFIG_RPC_MEM */

/* ------------------------------------------------------------- routes --- */

/* {name} matches one path segment, handed to the handler as a[i] in order; literal
 * segments match case-insensitively; the first route that matches wins (so a
 * literal like .../release goes before .../{value}). */
static const struct {
    const char *path;
    void (*fn)(const char *const *a);
} API_ROUTES[] = {
    { "/api/info",                                api_info },
    { "/json/version",                            api_json_version },
#ifdef CONFIG_RPC_OPENAPI
    { "/openapi.json",                            api_openapi },
#endif
    { "/api/pages",                               api_pages },
    { "/api/page",                                api_page },
    { "/api/page/{page}/set/{slot}/{value}",      api_page_set },
    { "/modd/{page}",                             api_goto },
    { "/modd/{page}/{sub}",                       api_goto },
    { "/api/log",                                 api_log },
#ifdef CONFIG_DEBUG_FRAMEBUFFER
    { "/api/screenshot",                          api_screenshot },
#endif
    { "/api/audio",                               api_audio },
    { "/api/engine",                              api_engine },
#ifdef CONFIG_RPC_AUDIO_TAP
    { "/api/audio/capture",                       api_audio_capture },
#endif
#ifdef CONFIG_FX_HOST
    { "/api/fx/slot",                             api_fx_slot },
    { "/api/fx/slot/off",                         api_fx_off },
    { "/api/fx/slot/on/{type}/{amount}",          api_fx_on },
    { "/api/fx/slot/unit/{unit}/{type}/{amount}", api_fx_unit },
    { "/api/fx/refuse/{value}",                   api_fx_refuse },
#endif
#ifdef CONFIG_ARENA_MELODEE
    { "/api/arena",                               api_arena },
    { "/api/arena/release/{part}",                api_arena_release },
    { "/api/arena/probe/{style}/{part}/{bytes}",  api_arena_probe },
#endif
#ifdef CONFIG_RPC_INPUT
    { "/api/input",                               api_input_state },
    { "/api/input/button/{button}/tap",           api_btn_tap },
    { "/api/input/button/{button}/tap/{ms}",      api_btn_tap },
    { "/api/input/button/{button}/hold/{ms}",     api_btn_tap },
    { "/api/input/button/{button}/down",          api_btn_down },
    { "/api/input/button/{button}/up",            api_btn_up },
    { "/api/input/enc/{encoder}/{clicks}",        api_enc },
    { "/api/input/key/{notes}/tap",               api_key_tap },
    { "/api/input/key/{notes}/tap/{ms}",          api_key_tap },
    { "/api/input/key/{notes}/down",              api_key_down },
    { "/api/input/key/{notes}/up",                api_key_up },
#ifdef CONFIG_AUDIO
    { "/api/input/pot/master/release",            api_pot_release },
    { "/api/input/pot/master/{value}",            api_pot },
    { "/api/track/{track}",                       api_track },
    { "/api/track/{track}/engine/{engine}",       api_track_engine },
#endif
#endif
#ifdef CONFIG_RPC_MEM
    { "/api/mem/find/{hex}",                      api_mem_find },
    { "/api/mem/{addr}/{length}",                 api_mem_read },
#endif
};

#define API_MAXSEG 8u

static int api_seg_eq(const char *seg, const char *lit, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        char x = seg[i], y = lit[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return seg[n] == '\0';
}

/* Reply to one GET: a route, 404 for an unknown path under /api or /modd, else the
 * discovery reply (/json/version). */
static void api_ctl_route(const char *path)
{
    static char buf[128];
    const char *seg[API_MAXSEG], *arg[API_MAXSEG] = {0};
    uint32_t nseg = 0, i, r;

    for (i = 0; path[i] && i < sizeof buf - 1; i++)
        buf[i] = path[i] == '/' ? '\0' : path[i];
    buf[i] = '\0';
    {
        uint32_t len = i;
        for (i = 0; i < len && nseg < API_MAXSEG; i++)
            if (buf[i] && (i == 0 || !buf[i - 1]))
                seg[nseg++] = &buf[i];
    }

    for (r = 0; r < sizeof API_ROUTES / sizeof API_ROUTES[0]; r++) {
        const char *t = API_ROUTES[r].path;
        uint32_t k = 0, na = 0;
        int ok = 1;
        while (ok) {
            const char *s;
            while (*t == '/') t++;
            if (!*t) break;
            s = t;
            while (*t && *t != '/') t++;
            if (k >= nseg) ok = 0;
            else if (s[0] == '{') arg[na++] = seg[k++];
            else ok = api_seg_eq(seg[k++], s, (uint32_t)(t - s));
        }
        if (ok && k == nseg) {
            for (i = na; i < API_MAXSEG; i++) arg[i] = 0;
            API_ROUTES[r].fn(arg);
            return;
        }
    }
    if (nseg && (api_seg_eq(seg[0], "api", 3) || api_seg_eq(seg[0], "modd", 4))) {
        rpc_http_error(404, "no such route (see /openapi.json)");
        return;
    }
    api_json_version(arg);
}

#ifndef CONFIG_RPC_INPUT
static void api_ctl_tick(void) {}
#endif
