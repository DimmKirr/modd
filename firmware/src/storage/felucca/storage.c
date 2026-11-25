/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca storage backend: upstream storage.c, storage_hw.c, project.c,
 * upreset.c, category.c, settings_persist.c included unchanged.
 * Origin: hugelton/Felucca firmware/src/{storage,storage_hw,project,upreset,
 *         category,settings_persist}.c @ v1.8.2 (6162de86) */

#define FELUCCA_FLASH 1

/* --- palette index math (from upstream's gen/ui_palettes.h; the constants
 * only, not the array — lvgl_palette.c provides that for the UI) --- */
#ifndef UI_NPALETTES
#define UI_NPALETTES 10u
#define UI_GREY_INDEX 0u
#define UI_PAL_TAG 64u
#endif
#ifndef NPALETTES
#define NPALETTES UI_NPALETTES
#endif
static uint32_t palette_from_stored(uint32_t v)
{
    return v >= UI_PAL_TAG && v - UI_PAL_TAG < NPALETTES ? v - UI_PAL_TAG : UI_GREY_INDEX;
}
static uint32_t palette_to_stored(uint32_t i) { return UI_PAL_TAG + i % NPALETTES; }

static void palette_apply(void) { }

/* --- panel.c: panel_t, settings global, hold/leds helpers --- */
#include "upstream/felucca/firmware/src/panel.c"

/* --- UI-layer stubs: upreset.c and project.c call into the UI during
 * load/save; modd's shell handles these differently --- */
static uint32_t up_gen;
static uint8_t sync_reload;
enum { UNDO_SOUND = 1, UNDO_PAT = 2, UNDO_MOT = 4 };
static struct {
    uint8_t trk, keep;
    uint32_t from_h;
    int16_t p[P_COUNT];
    step_t step[NSTEP];
} undo;
static uint32_t undo_depth;
static struct { uint8_t force, ppick, song_row; } ui;
static uint8_t pat_from[NTRK];
static uint32_t pat_sig[NTRK];
static uint32_t snd_said[NTRK];
static uint8_t drum_mute[NTRK];
static uint8_t ui_prefs;
#define PREF_RESTORE_OFF 128u

static void pat_from_set(uint32_t slot)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) pat_from[k] = (uint8_t)slot;
}
static void load_begin(track_t *t, uint32_t what) { (void)t; (void)what; }
static void load_end(track_t *t) { (void)t; }
static int param_kept(uint32_t i) { (void)i; return 0; }
static void fm4_apply(track_t *t, int16_t *p) { (void)t; (void)p; }
static uint32_t fm4_convert(int16_t *p, uint8_t *v) { (void)p; (void)v; return 0; }
static void phys_legacy(int16_t *e) { (void)e; }
static void load_pat16(track_t *t, const uint8_t *note, const uint8_t *flags) { (void)t; (void)note; (void)flags; }
static void load_grid16(track_t *t, const uint8_t *hit, const uint8_t *acc) { (void)t; (void)hit; (void)acc; }
static int transport_busy(void) { return 0; }
static int slice_page_on(void) { return 0; }
static void ui_message(const char *msg) { (void)msg; }
static void ui_say(const char *a, const char *b) { (void)a; (void)b; }
static int eng_ok(uint32_t e) { return e < NENGINES; }
static uint32_t step_lanes(const step_t *s) { (void)s; return 0; }
static uint32_t step_accents(const step_t *s) { (void)s; return 0; }
static void step_to_grid(step_t *s) { (void)s; }
static uint32_t track_cat(const track_t *t) { (void)t; return 0; }
static int chain_busy(void) { return 0; }
static void mom_back(const track_t *t) { (void)t; }
static void apply_preset_to(track_t *t, uint32_t pi) { (void)t; (void)pi; }
static void track_defaults_steps(track_t *t) { (void)t; }
static uint32_t trk_def_engine(uint32_t i) { return i < NENGINES ? i : 0; }
static uint32_t steps_sig(const track_t *t) { (void)t; return 0; }
#define NPATTERNS 0u
static const uint8_t TRK_DEF[NPART][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

/* --- forward declarations for functions defined later in the include chain --- */
static int project_info(uint32_t slot, char *b, uint16_t *len48, uint32_t *den, uint8_t *n);
static int project_used(uint32_t slot);

/* --- flash I/O --- */
#include "upstream/felucca/firmware/src/storage_hw.c"
#include "upstream/felucca/firmware/src/storage.c"

/* --- user presets (includes category.c, up_fm6.c, fm6_vbank.c) --- */
#include "upstream/felucca/firmware/src/upreset.c"

/* --- emulator: the 8 MiB flash has a different JEDEC ID than the 1 MiB parts
 * persist_boot checks; redirect to return the stock part's ID --- */
#ifdef CONFIG_EMU
static RAMFN uint32_t fl_jedec_ram_emu(void) { return 0x856014u; }
#define fl_jedec_ram fl_jedec_ram_emu
#endif

/* --- projects (includes settings_persist.c) --- */
#include "upstream/felucca/firmware/src/project.c"

#ifdef CONFIG_EMU
#undef fl_jedec_ram
#endif

/* --- modd_storage.h API: the names modd.c calls --- */
static void storage_init(void) { persist_boot(); }
static void settings_load(void) { panel_init(); }
