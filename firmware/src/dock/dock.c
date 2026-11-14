/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Joakim Eriksson (fm1-dock)
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: joakimeriksson/fm1-dock firmware/src/dock.c @ v0.8.2 (6199d250)
 *         https://github.com/joakimeriksson/fm1-dock/blob/6199d250c10376fca0270b917da2e79c7c604691/firmware/src/dock.c
 *         (the slot table dt_load / dt_write, check_module, add_slot, d_commit's placement and
 *         write-verify-publish order, start_module / stop_module, app_audio, the HOST table) */
/* Dock host: installs fm1-dock modules (.dock files, modd_dock.h) into flash and runs one at a
 * time through the audio ISR. The source of a file is a dock_src_t (dock_disk.c: a USB disk;
 * later: HTTP). The flash layout keeps fm1-dock's slot-table format in Melodee's main store,
 * which modd's storage stub leaves free:
 *   DOCK_TABLE_A 0x97000, DOCK_TABLE_B 0x98000   the deployed table, two copies, higher seq wins
 *   DOCK_LO 0x99000 .. DOCK_HI 0xE0000           modules, 4 KiB aligned, run in place (XIP)
 * A module's RAM is HB_MOD_RAM (app_dock.ld keeps it free) and the staging buffer for an install
 * is the same region: the running module is stopped first. Only render / note / cc are bridged;
 * screen, LEDs, banks and settings records are stubs (audio only). */

#define DOCK_TABLE_A 0x97000u
#define DOCK_TABLE_B 0x98000u
#define DOCK_LO      0x99000u
#define DOCK_HI      0xE0000u
#define DOCK_DT_MAGIC 0x54444248u               /* "HBDT", as fm1-dock's */
#define DOCK_SLOTS   CONFIG_DOCK_SLOTS
#define DOCK_NONE    0xFFFFFFFFu
#define DOCK_RX_BUF  ((uint8_t *)HB_MOD_RAM)
#define DOCK_RX_MAX  HB_MOD_RAM_SIZE

typedef struct { uint32_t magic, seq, n, crc; hb_slot_t s[DOCK_SLOTS]; } dock_table_t;
static dock_table_t dock_dt;                    /* the table in force */
static uint32_t dock_dt_at;                     /* its copy: DOCK_TABLE_A / B, 0: none */

typedef struct {
    const hb_file_t *file;
    const hb_module_t *desc;                    /* 0 unless state == DOCK_OK */
    uint8_t state;                              /* DOCK_OK, DOCK_BAD_*, DOCK_TOO_BIG, DOCK_INIT_FAILED */
    char name[24];
} dock_entry_t;
static dock_entry_t dock_mods[DOCK_SLOTS];
static uint32_t dock_n_mods;
static uint32_t dock_cur = DOCK_NONE;           /* the module running */
static const hb_module_t *volatile dock_active; /* what the audio ISR renders (0: modd's engines) */
static volatile uint32_t dock_blocks;
static int32_t dock_buf[2u * BLOCK];            /* the module's interleaved L R frames */
static void (*dock_progress)(const char *what, uint32_t pct);   /* the UI's, may be 0 */

static void dock_say(const char *what, uint32_t pct)
{
    if (dock_progress) dock_progress(what, pct);
}

/* ---- flash reads through the RAM driver (as modd_ble_vm_rd) ---- */
static int dock_fl_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    if (!modd_flash_ok) return -1;
    while (n) {
        uint32_t k = n > 256u ? 256u : n;
        uint32_t f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc) return rc;
        off += k; d += k; n -= k;
    }
    return 0;
}

/* ---- the slot table ---- */
static uint32_t dock_dt_crc(uint32_t seq, uint32_t n, const hb_slot_t *e)
{
    uint32_t h[2] = {seq, n};
    return dock_crc(dock_crc(0, (const uint8_t *)h, 8u), (const uint8_t *)e, 16u * n);
}

static void dock_dt_load(void)
{
    static dock_table_t t;
    uint32_t i, at[2] = {DOCK_TABLE_A, DOCK_TABLE_B};
    __builtin_memset(&dock_dt, 0, sizeof dock_dt);
    dock_dt_at = 0;
    for (i = 0; i < 2u; i++) {
        if (dock_fl_read(at[i], &t, 16u) || t.magic != DOCK_DT_MAGIC || t.n > DOCK_SLOTS ||
            dock_fl_read(at[i] + 16u, t.s, 16u * t.n) || dock_dt_crc(t.seq, t.n, t.s) != t.crc)
            continue;
        if (!dock_dt_at || (int32_t)(t.seq - dock_dt.seq) > 0) {
            __builtin_memcpy(&dock_dt, &t, sizeof dock_dt);
            dock_dt_at = at[i];
        }
    }
}

static uint32_t dock_dt_write(const hb_slot_t *e, uint32_t n)   /* into the other copy, the header last */
{
    static dock_table_t t;
    uint32_t to = dock_dt_at == DOCK_TABLE_A ? DOCK_TABLE_B : DOCK_TABLE_A, took;
    t.magic = DOCK_DT_MAGIC;
    t.seq = dock_dt.seq + 1u;
    t.n = n;
    __builtin_memcpy(t.s, e, 16u * n);
    t.crc = dock_dt_crc(t.seq, n, t.s);
    if (fl_erase4k(to, &took) || (n && fl_write(to + 16u, (const uint8_t *)t.s, 16u * n)) ||
        fl_write(to, (const uint8_t *)&t, 16u))
        return DOCK_FLASH;
    dock_dt_load();
    modd_log_int("dock: table seq ", (int32_t)t.seq);
    return dock_dt_at == to && dock_dt.seq == t.seq ? DOCK_OK : DOCK_VERIFY;
}

/* ---- the installed modules ---- */
static uint32_t dock_check_slot(const hb_slot_t *sl)
{
    const hb_file_t *f = (const hb_file_t *)sl->at;
    uint32_t rc;
    if (sl->at < FM1_XIP(DOCK_LO) || sl->at >= FM1_XIP(DOCK_HI) || sl->size < sizeof *f ||
        sl->size > FM1_XIP(DOCK_HI) - sl->at || (sl->at & 0xFFFu))
        return DOCK_BAD_HEAD;
    rc = dock_head_ok(f, sl->size);
    if (rc != DOCK_OK)
        return rc;
    if (f->crc != sl->crc || f->base != sl->at + dock_image_offset(f->n_relocs) || dock_file_size(f) > sl->size)
        return DOCK_BAD_HEAD;
    if (dock_file_crc(f) != f->crc)
        return DOCK_BAD_CRC;
    if (dock_desc_abi(f) != HB_ABI)
        return DOCK_BAD_HEAD;
    return DOCK_OK;
}

static void dock_str_cpy(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

static void dock_scan(void)
{
    uint32_t i;
    dock_n_mods = 0;
    dock_dt_load();
    for (i = 0; i < dock_dt.n && dock_n_mods < DOCK_SLOTS; i++) {
        const hb_slot_t *sl = &dock_dt.s[i];
        dock_entry_t *e = &dock_mods[dock_n_mods++];
        const hb_file_t *f = (const hb_file_t *)sl->at;
        e->state = (uint8_t)dock_check_slot(sl);
        e->file = f;
        e->desc = e->state == DOCK_OK ? (const hb_module_t *)(f->base + f->desc) : 0;
        dock_str_cpy(e->name, e->state != DOCK_BAD_HEAD ? f->name : "SLOT ?", sizeof e->name);
        modd_log(e->state == DOCK_OK ? "dock: slot ok" : "dock: slot refused");
        modd_log(e->name);
    }
}

static int dock_running(void) { return dock_active != 0; }

/* ---- the host table: time, log and the master level are real; the rest is audio-only stubs ---- */
static uint32_t dock_h_ms(void) { return fm1_ms; }
static uint32_t dock_h_ticks(void) { return fm1_ticks(); }
static void dock_h_log(const char *s) { modd_log(s); }
static void dock_h_log_u32(const char *s, uint32_t v) { modd_log_int(s, (int32_t)v); }
static void dock_h_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void dock_h_text(int32_t x, int32_t y, int32_t w, uint32_t big, const char *s, uint16_t fg, uint32_t center)
{ (void)x; (void)y; (void)w; (void)big; (void)s; (void)fg; (void)center; }
static void dock_h_cv_begin(int32_t w, int32_t h, uint16_t bg) { (void)w; (void)h; (void)bg; }
static void dock_h_cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void dock_h_cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) { (void)x0; (void)y0; (void)x1; (void)y1; (void)c; }
static void dock_h_cv_pixel(int32_t x, int32_t y, uint16_t c) { (void)x; (void)y; (void)c; }
static void dock_h_cv_text(int32_t x, int32_t y, uint32_t big, const char *s, uint16_t fg) { (void)x; (void)y; (void)big; (void)s; (void)fg; }
static void dock_h_cv_blit(int32_t x, int32_t y) { (void)x; (void)y; }
static void dock_h_led(uint32_t id, uint32_t on) { (void)id; (void)on; }
static uint32_t dock_h_rand(void) { return rng(); }
static int dock_h_load(void *p, uint32_t n) { (void)p; (void)n; return -1; }
static void dock_h_save(const void *p, uint32_t n) { (void)p; (void)n; }
static void dock_h_midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static uint32_t dock_h_master(void) { return (uint32_t)song.master_q12; }
static void dock_h_blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
static int32_t dock_h_text_w(uint32_t big, const char *s) { (void)big; (void)s; return 0; }
static int dock_h_bank_load(void *p, uint32_t n) { (void)p; (void)n; return -1; }
static int dock_h_bank_save(const void *p, uint32_t n) { (void)p; (void)n; return -1; }
static uint32_t dock_h_sense_keys(void) { return 0; }
static int dock_h_key_sense(uint32_t k, hb_key_sense_t *out) { (void)k; (void)out; return -1; }
static void dock_h_sense_set(uint32_t on, uint32_t drive, uint32_t curve, uint32_t ceil) { (void)on; (void)drive; (void)curve; (void)ceil; }
static int dock_h_bank_load_n(uint32_t idx, void *p, uint32_t n) { (void)idx; (void)p; (void)n; return -1; }
static int dock_h_bank_save_n(uint32_t idx, const void *p, uint32_t n) { (void)idx; (void)p; (void)n; return -1; }
static uint32_t dock_h_bank_free(void) { return 0; }
static int dock_h_c1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
static int dock_h_c1_wait(void) { return 0; }
static void dock_h_key_base(uint32_t n) { (void)n; }

static const hb_host_t DOCK_HOST = {
    HB_ABI, sizeof(hb_host_t), dock_h_ms, dock_h_ticks, dock_h_log, dock_h_log_u32,
    dock_h_fill, dock_h_text, dock_h_cv_begin, dock_h_cv_rect, dock_h_cv_line, dock_h_cv_pixel, dock_h_cv_text,
    dock_h_cv_blit, dock_h_led, dock_h_rand, dock_h_load, dock_h_save, dock_h_midi_send, dock_h_master,
    dock_h_blit, dock_h_text_w, dock_h_bank_load, dock_h_bank_save,
    dock_h_sense_keys, dock_h_key_sense, dock_h_sense_set, dock_h_bank_load_n, dock_h_bank_save_n,
    dock_h_bank_free, dock_h_c1_run, dock_h_c1_wait, HB_BOARD_FM1, dock_h_key_base,
};

/* ---- the sound: the audio ISR's mix_block hands the block over while a module runs ---- */
static void dock_render(int32_t *l, int32_t *r, uint32_t n)
{
    const hb_module_t *m = dock_active;
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        dock_buf[i] = 0;
    if (m)
        m->render(dock_buf, n);
    for (i = 0; i < n; i++) {                   /* Q15 frames into the mix buses */
        l[i] = dock_buf[2u * i] << (AUDIO_Q - 15);
        r[i] = dock_buf[2u * i + 1u] << (AUDIO_Q - 15);
    }
    dock_blocks++;
}

static void dock_note(uint32_t key, uint32_t vel)   /* MIDI: note(), every module has it */
{
    const hb_module_t *m = dock_active;
    if (m && m->note)
        m->note(0, key, vel);
}

/* a panel key k (0 = F3 .. 26 = G5), as fm1-dock's key scan delivers it: a panel module
 * (HB_F_PANEL, every shipped module) gets ui() events and plays them itself (velocity 0: not
 * sensed, the module picks its own); any other gets note() at the keyboard's base octave */
#define DOCK_KEY_BASE 53u
static void dock_key(uint32_t k, uint32_t down)
{
    const hb_module_t *m = dock_active;
    if (!m || k >= 27u)
        return;
    if ((m->flags & HB_F_PANEL) && m->ui)
        m->ui((down ? HB_EV_KEY_DOWN : HB_EV_KEY_UP) | k, 0);
    else if (m->note)
        m->note(0, DOCK_KEY_BASE + k, down ? 100u : 0u);
}

/* encoders and buttons, by fm1-dock's ids (modd's BTN_* / ENC_* are the same numbers) */
static void dock_enc(uint32_t id, int32_t clicks)
{
    const hb_module_t *m = dock_active;
    if (m && (m->flags & HB_F_PANEL) && m->ui)
        m->ui(HB_EV_ENC | (id & 0xFFu), clicks);
}

static void dock_btn(uint32_t id, uint32_t down)
{
    const hb_module_t *m = dock_active;
    if (m && (m->flags & HB_F_PANEL) && m->ui)
        m->ui((down ? HB_EV_BTN_DOWN : HB_EV_BTN_UP) | (id & 0xFFu), 0);
}

static void dock_cc(uint32_t num, uint32_t val)
{
    const hb_module_t *m = dock_active;
    if (m && m->cc)
        m->cc(0, num, val);
}

/* ---- switching ---- */
static void dock_stop(void)
{
    const hb_module_t *m = dock_active;
    uint32_t b = dock_blocks, t0 = fm1_ms;
    dock_active = 0;                            /* the ISR mixes modd's engines from the next block */
    while (dock_blocks == b && fm1_ms - t0 < 50u)   /* the block that may be running ends */
        fm1_wdt_feed();
    if (m && m->release)
        m->release();
    dock_cur = DOCK_NONE;
}

#ifdef CONFIG_USB_NET
static int usbnet_is_up(void);                  /* net/usbnet.c: its memory is the module's RAM (.pool_net) */
#define dock_net_busy() usbnet_is_up()
#else
#define dock_net_busy() 0
#endif

static uint32_t dock_start(uint32_t i)
{
    dock_entry_t *e;
    const hb_file_t *f;
    const uint8_t *img;
    if (dock_net_busy())
        return DOCK_BUSY;
    dock_stop();
    if (i >= dock_n_mods || dock_mods[i].state != DOCK_OK)
        return i < dock_n_mods ? dock_mods[i].state : DOCK_BAD_HEAD;
    e = &dock_mods[i];
    f = e->file;
    img = (const uint8_t *)f->base;
    __builtin_memcpy((void *)HB_MOD_RAM, img + f->code_size, f->data_size);
    __builtin_memset((uint8_t *)HB_MOD_RAM + f->data_size, 0, f->bss_size);
    __builtin_memcpy((void *)HB_MOD_RT, img + f->code_size + f->data_size, f->rt_size);
    if (e->desc->init(&DOCK_HOST)) {
        modd_log("dock: init failed");
        modd_log(e->name);
        e->state = DOCK_INIT_FAILED;
        return DOCK_INIT_FAILED;
    }
    dock_cur = i;
    dock_active = e->desc;
    modd_log("dock: started");
    modd_log(e->name);
    return DOCK_OK;
}

/* ---- installing: the file into the staging RAM, checked, relocated, written, verified, published ---- */
static uint32_t dock_find_name(const char *name)   /* the installed entry of that name, DOCK_NONE: none */
{
    uint32_t i, j;
    for (i = 0; i < dock_n_mods; i++) {
        for (j = 0; j < 24u && dock_mods[i].name[j] == name[j]; j++)
            if (!name[j]) return i;
    }
    return DOCK_NONE;
}

/* the lowest 4 KiB aligned place of `need` bytes in [DOCK_LO, DOCK_HI) beside the table's entries
 * (skipping `skip`), 0: none */
static uint32_t dock_place(uint32_t need, uint32_t skip)
{
    uint32_t at = DOCK_LO, i, moved;
    do {
        moved = 0;
        for (i = 0; i < dock_dt.n; i++) {
            uint32_t lo, hi;
            if (i == skip) continue;
            lo = dock_dt.s[i].at - FM1_XIP(0);
            hi = (lo + dock_dt.s[i].size + 0xFFFu) & ~0xFFFu;
            if (at < hi && at + need > lo) { at = hi; moved = 1; }
        }
    } while (moved && at + need <= DOCK_HI);
    return at + need <= DOCK_HI ? at : 0;
}

static uint32_t dock_install(const dock_src_t *src, uint32_t *idx)
{
    static hb_slot_t e[DOCK_SLOTS];
    hb_file_t *f = (hb_file_t *)DOCK_RX_BUF;
    uint32_t got, total, need, at, i, n, old, rc, took;
    hb_slot_t sl;
    *idx = DOCK_NONE;
    if (!modd_flash_ok)
        return DOCK_FLASH;
    if (src->size < sizeof *f || src->size > DOCK_RX_MAX)
        return DOCK_TOO_BIG;
    if (dock_net_busy())
        return DOCK_BUSY;                       /* (the staging buffer is the network's memory too) */
    dock_stop();                                /* the staging buffer is the module's RAM */
    dock_say("READING", 0);
    for (got = 0; got < src->size; got += n) {
        n = src->size - got < 4096u ? src->size - got : 4096u;
        if (src->read(src->ctx, DOCK_RX_BUF + got, n))
            return DOCK_READ;
        fm1_wdt_feed();
        dock_say("READING", got * 100u / src->size);
    }
    rc = dock_head_ok(f, src->size);
    if (rc != DOCK_OK)
        return rc;
    if (dock_file_crc(f) != f->crc)
        return DOCK_BAD_CRC;
    if (dock_desc_abi(f) != HB_ABI)
        return DOCK_BAD_HEAD;
    total = dock_file_size(f);
    need = (total + 0xFFFu) & ~0xFFFu;
    old = dock_find_name(f->name);              /* (its entry in dock_mods is its entry in the table) */
    if (old == DOCK_NONE && dock_dt.n >= DOCK_SLOTS)
        return DOCK_NO_SPACE;
    at = dock_place(need, old);
    if (!at)
        return DOCK_NO_SPACE;
    dock_relocate(f, FM1_XIP(at) + dock_image_offset(f->n_relocs));
    dock_say("WRITING", 0);
    for (i = 0; i < need; i += 0x1000u) {
        if (fl_erase4k(at + i, &took))
            return DOCK_FLASH;
        fm1_wdt_feed();
        dock_say("WRITING", i * 50u / need);
    }
    for (i = 0; i < total; i += 0x1000u) {
        if (fl_write(at + i, DOCK_RX_BUF + i, total - i < 0x1000u ? total - i : 0x1000u))
            return DOCK_FLASH;
        fm1_wdt_feed();
        dock_say("WRITING", 50u + i * 50u / total);
    }
    dock_say("VERIFY", 100);
    sl.magic = HB_SLOT_MAGIC;
    sl.at = FM1_XIP(at);
    sl.size = total;
    sl.crc = f->crc;
    if (fl_verify_xip(at, DOCK_RX_BUF, total) || dock_check_slot(&sl) != DOCK_OK)
        return DOCK_VERIFY;
    for (i = n = 0; i < dock_dt.n; i++)        /* the new one in the old one's place, else at the end */
        e[n++] = i == old ? sl : dock_dt.s[i];
    if (old == DOCK_NONE)
        e[n++] = sl;
    modd_log("dock: installed");
    modd_log(f->name);
    rc = dock_dt_write(e, n);
    dock_scan();
    if (rc == DOCK_OK)
        *idx = old == DOCK_NONE ? n - 1u : old;
    return rc;
}

static uint32_t dock_delete(uint32_t i)        /* the table without entry i; the running module stopped first */
{
    static hb_slot_t e[DOCK_SLOTS];
    uint32_t k, n, rc;
    if (i >= dock_dt.n)
        return DOCK_BAD_HEAD;
    dock_stop();
    for (k = n = 0; k < dock_dt.n; k++)
        if (k != i) e[n++] = dock_dt.s[k];
    rc = dock_dt_write(e, n);
    dock_scan();
    return rc;
}

static void dock_init(void)
{
    dock_active = 0;
    dock_cur = DOCK_NONE;
    dock_progress = 0;
    dock_scan();
    modd_log_int("dock: modules ", (int32_t)dock_n_mods);
}
