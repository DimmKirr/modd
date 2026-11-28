// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* modd effects host: ONE stereo master slot after the mix, before the master volume (audio.c).
 * Main loop (fx_host_poll): bytes -> arena get -> zero -> prepare -> gate open. Audio (fx_host_run):
 * runs the unit while the gate is open, else leaves the block alone (bit-identical dry). Off: the unit's
 * release starts the tail, the gate stays open until quiet() or FX_TAIL_BLOCKS blocks, then the main loop
 * closes the gate and returns the block. No allocation in the audio interrupt. A change of unit, type or
 * amount while on is off, then on. */

enum { FXS_OFF, FXS_PLACED, FXS_GATED, FXS_REFUSED };    /* PLACED: running; GATED: tail after off */
#define FX_TAIL_BLOCKS 16u                               /* 16 x 128 samples, far longer than the unit's fade */
#define FX_ARENA_ID 0u

static const fx_unit_t *const FX_UNITS[] = {
#ifdef CONFIG_FX_UNIT_SLOOP_INSERT
    &FX_UNIT_SLOOP_INSERT,
#endif
#ifdef CONFIG_FX_UNIT_EXGAIN
    &FX_UNIT_EXGAIN,
#endif
    0,
};
#define FX_NUNITS ((uint32_t)(sizeof FX_UNITS / sizeof FX_UNITS[0]) - 1u)

static uint8_t fx_slot_state[FX_STATE_BYTES] __attribute__((aligned(8)));
static void *fx_slot_mem;
static const fx_unit_t *fx_slot_unit;
static uint32_t fx_slot_type, fx_slot_bytes;
static int32_t fx_slot_amount;
static volatile uint8_t fx_gate;                         /* audio runs the unit while this is 1 */
static volatile uint8_t fx_state_v = FXS_OFF;
static volatile uint32_t fx_blocks;                      /* audio blocks run with the gate open */
static uint32_t fx_tail_start;
/* requests from the API (any context) to the main loop */
static volatile uint8_t fx_req_on, fx_req_off;
static volatile uint32_t fx_req_unit, fx_req_type;
static volatile int32_t fx_req_amount;

static void fx_host_run(int32_t *l, int32_t *r, uint32_t n)
{
    if (!fx_gate) return;
    fx_slot_unit->run(fx_slot_state, fx_slot_mem, l, r, n);
    fx_blocks++;
}

static void fx_host_free(void)
{
    fx_gate = 0;
    if (fx_slot_mem) modd_arena_put(FX_ARENA_ID);
    fx_slot_mem = 0;
    fx_slot_bytes = 0;
}

static void fx_host_start(void)
{
    const fx_unit_t *u = fx_req_unit < FX_NUNITS ? FX_UNITS[fx_req_unit] : 0;
    uint32_t type = fx_req_type, bytes;
    if (!u || type >= u->ntypes) { fx_state_v = FXS_REFUSED; return; }
    bytes = u->bytes(type);
    fx_slot_mem = 0;
    if (bytes) {
        fx_slot_mem = modd_arena_get(FX_ARENA_ID, bytes);
        if (!fx_slot_mem) { fx_slot_unit = u; fx_slot_type = type; fx_slot_bytes = 0; fx_state_v = FXS_REFUSED; return; }
        memset(fx_slot_mem, 0, bytes);
    }
    fx_slot_unit = u;
    fx_slot_type = type;
    fx_slot_bytes = bytes;
    fx_slot_amount = fx_req_amount;
    u->prepare(fx_slot_state, fx_slot_mem, type, fx_slot_amount);
    fx_blocks = 0;
    fx_gate = 1;
    fx_state_v = FXS_PLACED;
}

static void fx_host_poll(void)                           /* main loop */
{
    uint8_t st = fx_state_v;
    if (fx_req_off) {
        fx_req_off = 0;
        if (st == FXS_PLACED) {
            if (fx_slot_unit->release) fx_slot_unit->release(fx_slot_state);
            fx_tail_start = fx_blocks;
            fx_state_v = st = FXS_GATED;
        } else if (st == FXS_REFUSED) {
            fx_state_v = st = FXS_OFF;
        }
    }
    if (st == FXS_GATED) {
        int done = fx_slot_unit->quiet ? fx_slot_unit->quiet(fx_slot_state, fx_slot_mem) : 0;
        if (done || fx_blocks - fx_tail_start >= FX_TAIL_BLOCKS) {
            fx_host_free();
            fx_state_v = st = FXS_OFF;
        }
    }
    if (fx_req_on && st != FXS_GATED) {                  /* a running slot goes off first; the on waits for the tail */
        if (st == FXS_PLACED) {
            if (fx_slot_unit->release) fx_slot_unit->release(fx_slot_state);
            fx_tail_start = fx_blocks;
            fx_state_v = FXS_GATED;
        } else {
            fx_req_on = 0;
            fx_host_start();
        }
    }
}

/* The effect page (modd_pages.h): cards TYPE (0 = OFF, then every unit's types in one flat list) and AMOUNT. Set
 * stores the card and files a request; the main loop above does the work. */
static uint32_t fx_page_type;
static int32_t fx_page_amount = 64;

static uint32_t fx_page_ntypes(void)
{
    uint32_t u, n = 0;
    for (u = 0; u < FX_NUNITS; u++) n += FX_UNITS[u]->ntypes;
    return n;
}

static void fx_page_apply(void)
{
    uint32_t u, t = fx_page_type;
    if (!t) { fx_req_off = 1; return; }
    t--;
    for (u = 0; u < FX_NUNITS && t >= FX_UNITS[u]->ntypes; u++) t -= FX_UNITS[u]->ntypes;
    fx_req_unit = u;
    fx_req_type = t;
    fx_req_amount = fx_page_amount;
    fx_req_on = 1;
}

static uint32_t fx_host_page_read(modd_card_t *out, uint32_t max)
{
    uint32_t u, t = fx_page_type, j = 0;
    const char *nm = "OFF";
    if (max < 2u) return 0;
    if (t) {
        t--;
        for (u = 0; u < FX_NUNITS && t >= FX_UNITS[u]->ntypes; u++) t -= FX_UNITS[u]->ntypes;
        nm = FX_UNITS[u]->type_name(t);
    }
    out[0].label = "TYPE";
    out[0].value = (int32_t)fx_page_type;
    out[0].min = 0;
    out[0].max = (int32_t)fx_page_ntypes();
    for (; nm[j] && j < 11u; j++) out[0].text[j] = nm[j];
    out[0].text[j] = 0;
    out[1].label = "AMOUNT";
    out[1].value = fx_page_amount;
    out[1].min = 0;
    out[1].max = 127;
    {
        char d[3];
        uint32_t k = 0;
        t = (uint32_t)fx_page_amount;
        do { d[k++] = (char)('0' + t % 10u); t /= 10u; } while (t);
        for (j = 0; k; ) out[1].text[j++] = d[--k];
        out[1].text[j] = 0;
    }
    return 2;
}

static int fx_host_page_set(uint32_t slot, int32_t value)
{
    if (slot == 0) {
        if (value < 0 || value > (int32_t)fx_page_ntypes()) return 1;
        fx_page_type = (uint32_t)value;
    } else if (slot == 1) {
        if (value < 0 || value > 127) return 1;
        fx_page_amount = value;
        if (!fx_page_type) return 0;           /* OFF: nothing runs, the amount waits for a type */
    } else {
        return 1;
    }
    fx_page_apply();
    return 0;
}
