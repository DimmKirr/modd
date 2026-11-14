// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

/* Async LLM inference client: sends a prompt to a local HTTP proxy
 * that forwards to OpenRouter. Non-blocking, callback-driven,
 * follows the same pattern as wifi_weather.c. */

#ifdef CONFIG_LLM

/* ---- LLM response data ------------------------------------------------- */

#define LLM_TEXT_MAX  1024

struct llm_data {
    char     text[LLM_TEXT_MAX];
    uint32_t text_len;
    uint32_t updated_ms;
    uint8_t  valid;
    uint8_t  error;
};

/* ---- state machine ------------------------------------------------------ */

#define LLM_IDLE     0
#define LLM_FETCH    1
#define LLM_WAIT     2
#define LLM_DONE     3

#define LLM_TIMEOUT_MS       60000u
#define LLM_PROXY_PORT       8484

static struct llm_data llm_dat;
static uint8_t  llm_state;
static uint32_t llm_start_ms;

static char llm_proxy_host[48];
static char llm_prompt_buf[192];

/* ---- URL-encode a string into dst, returns length written --------------- */

static uint32_t llm_urlencode(char *dst, uint32_t dst_max,
                              const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t di = 0;
    while (*src && di + 3 < dst_max) {
        uint8_t c = (uint8_t)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = (char)c;
        } else if (c == ' ') {
            dst[di++] = '+';
        } else {
            dst[di++] = '%';
            dst[di++] = hex[c >> 4];
            dst[di++] = hex[c & 0x0F];
        }
        src++;
    }
    dst[di] = 0;
    return di;
}

/* ---- response callback -------------------------------------------------- */

static void llm_response_cb(int status, const uint8_t *body,
                             uint32_t body_len, void *ctx)
{
    (void)ctx;

    if (status != 200 || !body || body_len == 0) {
        modd_log_int("wifi: llm failed status=", (int32_t)status);
        llm_dat.error = 1;
        llm_state = LLM_IDLE;
        return;
    }

    /* proxy returns {"text":"..."} -- extract the text value */
    {
        char tmp[LLM_TEXT_MAX];
        uint32_t n = mini_json_extract(body, body_len, "text", tmp, sizeof(tmp));
        if (n > 0) {
            uint32_t i;
            uint32_t lim = n < LLM_TEXT_MAX - 1 ? n : LLM_TEXT_MAX - 1;
            for (i = 0; i < lim; i++)
                llm_dat.text[i] = tmp[i];
            llm_dat.text[lim] = 0;
            llm_dat.text_len = lim;
            llm_dat.valid = 1;
            llm_dat.error = 0;
            llm_dat.updated_ms = fm1_ms;
            modd_log_int("wifi: llm ok len=", (int32_t)lim);
        } else {
            modd_log("wifi: llm response missing text field");
            llm_dat.error = 1;
        }
    }

    llm_state = LLM_DONE;
}

/* ---- public API --------------------------------------------------------- */

static void llm_init(void)
{
    llm_state = LLM_IDLE;
    llm_dat.valid = 0;
    llm_dat.error = 0;
    llm_dat.text[0] = 0;
    llm_dat.text_len = 0;
    llm_proxy_host[0] = 0;
}

static void llm_set_proxy(const char *host)
{
    uint32_t i;
    for (i = 0; i < sizeof(llm_proxy_host) - 1 && host[i]; i++)
        llm_proxy_host[i] = host[i];
    llm_proxy_host[i] = 0;
}

/* Start an LLM request. prompt is URL-safe (alphanumeric + basic punctuation).
 * Returns 0 on success, -1 if busy or not configured. */
static int llm_generate(const char *prompt)
{
    if (llm_state == LLM_FETCH || llm_state == LLM_WAIT)
        return -1;
    if (!llm_proxy_host[0])
        return -1;

    {
        uint32_t i;
        for (i = 0; i < sizeof(llm_prompt_buf) - 1 && prompt[i]; i++)
            llm_prompt_buf[i] = prompt[i];
        llm_prompt_buf[i] = 0;
    }

    llm_state = LLM_FETCH;
    llm_start_ms = fm1_ms;
    llm_dat.error = 0;
    return 0;
}

static void llm_poll(void)
{
    switch (llm_state) {

    case LLM_FETCH: {
        char path[384];
        uint32_t pi = 0;
        char encoded[256];
        const char *s;

        if (http_busy()) break;

        /* /generate?prompt=<url-encoded prompt> */
        s = "/generate?prompt=";
        while (*s) path[pi++] = *s++;

        llm_urlencode(encoded, sizeof(encoded), llm_prompt_buf);
        s = encoded;
        while (*s && pi < sizeof(path) - 1) path[pi++] = *s++;
        path[pi] = 0;

        if (http_get(llm_proxy_host, path, llm_response_cb, 0) == 0) {
            llm_state = LLM_WAIT;
        } else {
            llm_state = LLM_IDLE;
            llm_dat.error = 1;
        }
        break;
    }

    case LLM_WAIT:
        if (fm1_ms - llm_start_ms > LLM_TIMEOUT_MS) {
            modd_log("wifi: llm timeout");
            llm_dat.error = 1;
            llm_state = LLM_IDLE;
        }
        break;

    case LLM_DONE:
    case LLM_IDLE:
        break;
    }
}

static int llm_busy(void)
{
    return llm_state == LLM_FETCH || llm_state == LLM_WAIT;
}

static const struct llm_data *llm_get(void)
{
    return &llm_dat;
}

#else /* !CONFIG_LLM */

#define LLM_TEXT_MAX 1

struct llm_data {
    char     text[LLM_TEXT_MAX];
    uint32_t text_len;
    uint32_t updated_ms;
    uint8_t  valid;
    uint8_t  error;
};

static struct llm_data llm_dat;
static void llm_init(void) {}
static void llm_set_proxy(const char *host) { (void)host; }
static int  llm_generate(const char *prompt) { (void)prompt; return -1; }
static void llm_poll(void) {}
static int  llm_busy(void) { return 0; }
static const struct llm_data *llm_get(void) { return &llm_dat; }

#endif /* CONFIG_LLM */
