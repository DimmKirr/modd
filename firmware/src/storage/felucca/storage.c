/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* Felucca storage backend (CONFIG_STORAGE_FELUCCA), simplified flash layout.
 * Not Felucca's sector layout: one 4 KiB sector per slot in the shared
 * FL_BANK_EXT region (0xE5000..0xFB000, 22 sectors):
 *   sectors 0..7   projects  (ST_NPROJ)
 *   sectors 8..21  user presets (ST_NUP)
 * A slot is erased + programmed whole, so there is no read-modify-write.
 * Settings persist through the settings adapter (sys_fl_*, modd_settings.h). */

#define ST_BASE   FL_BANK_EXT_LO
#define ST_SECT   4096u
#define ST_NPROJ  8u
#define ST_NUP    14u
#define ST_MAGIC_PROJ 0x4A525046u   /* "FPRJ" */
#define ST_MAGIC_UP   0x50555046u   /* "FUPP" */
#define ST_NAMELEN 12

typedef struct {
    uint32_t magic;
    char name[ST_NAMELEN];
    uint32_t bpm;
    int16_t p[NTRK][P_COUNT];
    uint8_t engine[NTRK], preset[NTRK];
    step_t step[NTRK][NSTEP];
} st_proj_t;

typedef struct {
    uint32_t magic;
    char name[ST_NAMELEN];
    uint8_t engine, cat, has_pat, pad;
    int16_t p[P_COUNT];
    step_t step[NSTEP];
} st_up_t;

typedef char st_proj_fits[sizeof(st_proj_t) <= ST_SECT ? 1 : -1];
typedef char st_up_fits[sizeof(st_up_t) <= ST_SECT ? 1 : -1];

static union { st_proj_t pr; st_up_t up; } st_buf;
static char st_cur_name[ST_NAMELEN + 1];

static uint32_t st_proj_off(uint32_t slot) { return ST_BASE + slot * ST_SECT; }
static uint32_t st_up_off(uint32_t k) { return ST_BASE + (ST_NPROJ + k) * ST_SECT; }

static int st_read(uint32_t off, void *dst, uint32_t n)
{
    return FL_FAR(fl_read_ram)(off, (uint8_t *)dst, n);
}

/* erase the slot sector and program st_buf (n bytes) */
static int st_store(uint32_t off, uint32_t n)
{
    uint32_t took = 0;
    if (fl_erase4k(off, &took)) return -1;
    return fl_write(off, (const uint8_t *)&st_buf, n) ? -1 : 0;
}

static void st_set_name(char *dst, const char *src)
{
    uint32_t i = 0;
    while (src && src[i] && i < ST_NAMELEN) { dst[i] = src[i]; i++; }
    while (i < ST_NAMELEN) dst[i++] = 0;
}

static int st_peek(uint32_t off, uint32_t magic, char *name, uint32_t *extra)
{
    uint32_t hdr[1 + ST_NAMELEN / 4];
    if (st_read(off, hdr, sizeof hdr)) return 0;
    if (hdr[0] != magic) return 0;
    if (name) { memcpy(name, &hdr[1], ST_NAMELEN); name[ST_NAMELEN] = 0; }
    (void)extra;
    return 1;
}

static void storage_init(void) { st_cur_name[0] = 0; }

/* --- projects --- */

static int project_used(uint32_t slot)
{
    return slot < ST_NPROJ && st_peek(st_proj_off(slot), ST_MAGIC_PROJ, 0, 0);
}

static int project_name(uint32_t slot, char *b)
{
    b[0] = 0;
    if (slot >= ST_NPROJ || !st_peek(st_proj_off(slot), ST_MAGIC_PROJ, b, 0)) return 0;
    return 1;
}

static const char *project_cur_name(void) { return st_cur_name; }

static int project_save_as(uint32_t slot, const char *name)
{
    uint32_t t;
    if (slot >= ST_NPROJ) return 0;
    memset(&st_buf, 0, sizeof st_buf);
    st_buf.pr.magic = ST_MAGIC_PROJ;
    st_set_name(st_buf.pr.name, name && name[0] ? name : st_cur_name);
    st_buf.pr.bpm = song.bpm;
    for (t = 0; t < NTRK; t++) {
        memcpy(st_buf.pr.p[t], trk[t].p, sizeof trk[t].p);
        st_buf.pr.engine[t] = trk[t].engine;
        st_buf.pr.preset[t] = trk[t].preset;
        memcpy(st_buf.pr.step[t], trk[t].step, sizeof trk[t].step);
    }
    if (st_store(st_proj_off(slot), sizeof st_buf.pr)) return 0;
    st_set_name(st_cur_name, st_buf.pr.name);
    return 1;
}

static int project_save(uint32_t slot) { return project_save_as(slot, 0); }

static void project_load(uint32_t slot)
{
    uint32_t t;
    if (slot >= ST_NPROJ || st_read(st_proj_off(slot), &st_buf.pr, sizeof st_buf.pr)) return;
    if (st_buf.pr.magic != ST_MAGIC_PROJ) return;
    if (st_buf.pr.bpm) song.bpm = st_buf.pr.bpm;
    for (t = 0; t < NTRK; t++) {
        memcpy(trk[t].p, st_buf.pr.p[t], sizeof trk[t].p);
        trk[t].engine = st_buf.pr.engine[t];
        trk[t].eng_req = st_buf.pr.engine[t];
        trk[t].preset = st_buf.pr.preset[t];
        memcpy(trk[t].step, st_buf.pr.step[t], sizeof trk[t].step);
    }
    st_buf.pr.name[ST_NAMELEN - 1] = 0;
    st_set_name(st_cur_name, st_buf.pr.name);
}

static void project_erase(uint32_t slot)
{
    uint32_t took = 0;
    if (slot < ST_NPROJ) (void)fl_erase4k(st_proj_off(slot), &took);
}

static void project_rename(uint32_t slot, const char *name)
{
    if (slot >= ST_NPROJ || st_read(st_proj_off(slot), &st_buf.pr, sizeof st_buf.pr)) return;
    if (st_buf.pr.magic != ST_MAGIC_PROJ) return;
    st_set_name(st_buf.pr.name, name);
    (void)st_store(st_proj_off(slot), sizeof st_buf.pr);
}

static int project_info(uint32_t slot, char *b)
{
    b[0] = 0;
    if (!project_used(slot)) return 0;
    return project_name(slot, b);
}

/* --- user presets --- */

static int up_used(uint32_t k)
{
    return k < ST_NUP && st_peek(st_up_off(k), ST_MAGIC_UP, 0, 0);
}

static uint32_t up_count(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < ST_NUP; k++) n += (uint32_t)up_used(k);
    return n;
}

static uint32_t up_nth(uint32_t n)
{
    uint32_t k;
    for (k = 0; k < ST_NUP; k++)
        if (up_used(k) && n-- == 0) return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)
{
    uint32_t k, r = 0;
    for (k = 0; k < slot && k < ST_NUP; k++) r += (uint32_t)up_used(k);
    return r;
}

static uint32_t up_engine(uint32_t k)
{
    st_up_t u;
    if (k >= ST_NUP || st_read(st_up_off(k), &u, sizeof u) || u.magic != ST_MAGIC_UP) return 0;
    return u.engine;
}

static uint32_t up_cat(uint32_t k)
{
    st_up_t u;
    if (k >= ST_NUP || st_read(st_up_off(k), &u, sizeof u) || u.magic != ST_MAGIC_UP) return 0;
    return u.cat;
}

static void up_name(uint32_t k, char *b)
{
    b[0] = 0;
    if (k < ST_NUP) (void)st_peek(st_up_off(k), ST_MAGIC_UP, b, 0);
}

static void up_slot_label(char *b, uint32_t k)
{
    b[0] = (char)('0' + (k / 10) % 10);
    b[1] = (char)('0' + k % 10);
    b[2] = 0;
}

static void up_auto_name(char *b, uint32_t e, uint32_t k)
{
    b[0] = 'U'; b[1] = (char)('0' + (e / 10) % 10); b[2] = (char)('0' + e % 10);
    b[3] = '-'; b[4] = (char)('0' + (k / 10) % 10); b[5] = (char)('0' + k % 10);
    b[6] = 0;
}

static int up_load(uint32_t k)
{
    track_t *t = &trk[song.sel < NTRK ? song.sel : 0];
    if (k >= ST_NUP || st_read(st_up_off(k), &st_buf.up, sizeof st_buf.up)) return 0;
    if (st_buf.up.magic != ST_MAGIC_UP) return 0;
    memcpy(t->p, st_buf.up.p, sizeof t->p);
    t->engine = st_buf.up.engine;
    t->eng_req = st_buf.up.engine;
    t->user = (uint8_t)(k + 1);
    return 1;
}

static void up_ui_named(uint32_t op, uint32_t k, const char *name, uint32_t cat)
{
    track_t *t = &trk[song.sel < NTRK ? song.sel : 0];
    uint32_t took = 0;
    if (k >= ST_NUP) return;
    if (op == 1) { (void)fl_erase4k(st_up_off(k), &took); return; }
    if (op != 2) return;
    memset(&st_buf, 0, sizeof st_buf);
    st_buf.up.magic = ST_MAGIC_UP;
    if (name && name[0]) st_set_name(st_buf.up.name, name);
    else { char a[8]; up_auto_name(a, t->engine, k); st_set_name(st_buf.up.name, a); }
    st_buf.up.engine = t->engine;
    st_buf.up.cat = (uint8_t)cat;
    st_buf.up.has_pat = 1;
    memcpy(st_buf.up.p, t->p, sizeof t->p);
    memcpy(st_buf.up.step, t->step, sizeof t->step);
    (void)st_store(st_up_off(k), sizeof st_buf.up);
}

static void up_ui(uint32_t op, uint32_t k) { up_ui_named(op, k, 0, 0); }

/* --- user-preset patterns --- */

static int up_has_pat(uint32_t k)
{
    st_up_t u;
    if (k >= ST_NUP || st_read(st_up_off(k), &u, sizeof u) || u.magic != ST_MAGIC_UP) return 0;
    return u.has_pat;
}

static uint32_t up_pat_count(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < ST_NUP; k++) n += (uint32_t)up_has_pat(k);
    return n;
}

static uint32_t up_pat_nth(uint32_t n)
{
    uint32_t k;
    for (k = 0; k < ST_NUP; k++)
        if (up_has_pat(k) && n-- == 0) return k;
    return 0;
}

static uint32_t up_pat_rank(uint32_t slot)
{
    uint32_t k, r = 0;
    for (k = 0; k < slot && k < ST_NUP; k++) r += (uint32_t)up_has_pat(k);
    return r;
}

static void up_pat_load(track_t *t, uint32_t k)
{
    if (k >= ST_NUP || st_read(st_up_off(k), &st_buf.up, sizeof st_buf.up)) return;
    if (st_buf.up.magic != ST_MAGIC_UP || !st_buf.up.has_pat) return;
    memcpy(t->step, st_buf.up.step, sizeof t->step);
}

/* --- settings: sys_settings persists through the settings adapter --- */

static void settings_load(void) {}
static void settings_save(void) { sys_settings_flush(); }
