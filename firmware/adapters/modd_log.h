// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Debug log adapter: fixed-size ring buffer for diagnostic messages.
 * Any module can call modd_log() to push a line; the debug UI page
 * reads the ring to display recent messages on the LCD. */
#ifndef MODD_LOG_H
#define MODD_LOG_H
#include <stdint.h>

#ifdef CONFIG_DEBUG_LOG
#define MODD_LOG_LINES    CONFIG_DEBUG_LOG_LINES
#define MODD_LOG_LINE_LEN 48
#else
#define MODD_LOG_LINES    32
#define MODD_LOG_LINE_LEN 40
#endif

static char     modd_log_buf[MODD_LOG_LINES][MODD_LOG_LINE_LEN];
static uint32_t modd_log_ts[MODD_LOG_LINES];
static uint32_t modd_log_head;
static uint32_t modd_log_count;
static uint32_t modd_log_scroll;
static volatile uint32_t modd_log_ms;

static void modd_log_init(void)
{
    uint32_t i, j;
    for (i = 0; i < MODD_LOG_LINES; i++) {
        for (j = 0; j < MODD_LOG_LINE_LEN; j++)
            modd_log_buf[i][j] = 0;
        modd_log_ts[i] = 0;
    }
    modd_log_head = 0;
    modd_log_count = 0;
    modd_log_scroll = 0;
    modd_log_ms = 0;
}

static uint32_t modd_log_oldest_index(void)
{
    if (modd_log_count < MODD_LOG_LINES)
        return 0;
    return modd_log_head;
}

static const char *modd_log_line(uint32_t idx)
{
    uint32_t base = modd_log_oldest_index();
    return modd_log_buf[(base + idx) % MODD_LOG_LINES];
}

static uint32_t modd_log_time(uint32_t idx)
{
    uint32_t base = modd_log_oldest_index();
    return modd_log_ts[(base + idx) % MODD_LOG_LINES];
}

#ifdef CONFIG_DEBUG_LOG

static void modd_log(const char *msg)
{
    uint32_t slot = modd_log_head % MODD_LOG_LINES;
    char *dst = modd_log_buf[slot];
    uint32_t i;
    for (i = 0; i < MODD_LOG_LINE_LEN - 1 && msg[i]; i++)
        dst[i] = msg[i];
    dst[i] = 0;
    modd_log_ts[slot] = modd_log_ms;
    modd_log_head = (modd_log_head + 1) % MODD_LOG_LINES;
    if (modd_log_count < MODD_LOG_LINES)
        modd_log_count++;
#ifdef CONFIG_DEBUG_DAP
    {
        char b[12];
        uint32_t t = modd_log_ms, n = 0, j;
        do { b[n++] = (char)('0' + t % 10u); t /= 10u; } while (t);
        for (j = 0; j < n / 2u; j++) { char c = b[j]; b[j] = b[n - 1u - j]; b[n - 1u - j] = c; }
        b[n++] = ' ';
        b[n] = 0;
        modd_debug_put(b);
        modd_debug_put(msg);
        modd_debug_put("\n");
    }
#endif
}

static void modd_log_int(const char *prefix, int32_t val)
{
    char line[MODD_LOG_LINE_LEN];
    uint32_t i = 0;
    while (i < MODD_LOG_LINE_LEN - 12 && prefix[i]) {
        line[i] = prefix[i];
        i++;
    }
    if (val < 0) {
        line[i++] = '-';
        val = -val;
    }
    {
        char tmp[12];
        uint32_t j = 0;
        do {
            tmp[j++] = '0' + (char)(val % 10);
            val /= 10;
        } while (val && j < 11);
        while (j) line[i++] = tmp[--j];
    }
    line[i] = 0;
    modd_log(line);
}

static void modd_log_hex(const char *prefix, const uint8_t *data, uint32_t len)
{
    char line[MODD_LOG_LINE_LEN];
    uint32_t i = 0, j;
    static const char hex[] = "0123456789ABCDEF";
    while (i < MODD_LOG_LINE_LEN - 4 && prefix[i]) {
        line[i] = prefix[i];
        i++;
    }
    for (j = 0; j < len && i + 3 < MODD_LOG_LINE_LEN; j++) {
        line[i++] = hex[data[j] >> 4];
        line[i++] = hex[data[j] & 0x0F];
        if (j + 1 < len && i < MODD_LOG_LINE_LEN - 1)
            line[i++] = ' ';
    }
    line[i] = 0;
    modd_log(line);
}

#else /* !CONFIG_DEBUG_LOG */

static void modd_log(const char *msg) { (void)msg; }
static void modd_log_int(const char *prefix, int32_t val)
{
    (void)prefix; (void)val;
}
static void modd_log_hex(const char *prefix, const uint8_t *data, uint32_t len)
{
    (void)prefix; (void)data; (void)len;
}

#endif /* CONFIG_DEBUG_LOG */

#endif /* MODD_LOG_H */
