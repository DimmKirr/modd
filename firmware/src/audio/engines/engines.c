// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Engine registry: conditional includes based on CONFIG_ENGINE_*.
 * Each engine provides a static const engine_t and is added to ENGINES[]. */

#ifdef CONFIG_ENGINE_ANALOG
#include "upstream/felucca/firmware/engines/src/eng_analog.c"
#endif

#ifdef CONFIG_ENGINE_NOISE
#include "upstream/felucca/firmware/engines/src/eng_noise.c"
#endif

#ifdef CONFIG_ENGINE_LOFI
#include "upstream/felucca/firmware/engines/src/eng_lofi.c"
#endif

#ifdef CONFIG_ENGINE_TRIO
#include "upstream/felucca/firmware/engines/src/eng_trio.c"
#endif

#ifdef CONFIG_ENGINE_PHASE
#include "upstream/felucca/firmware/engines/src/eng_phase.c"
#endif

#ifdef CONFIG_ENGINE_TONEWHEEL
#include "upstream/felucca/firmware/engines/src/eng_wheel.c"
#endif

#if defined(CONFIG_ENGINE_GRANULAR) || defined(CONFIG_ENGINE_SLICE) || defined(CONFIG_ENGINE_ADPCM) || defined(CONFIG_ENGINE_FM6_FELUCCA)
/* ENG_MEM: per-part engine state pool used by GRAIN, SLICE, ADPCM and Felucca FM6.
 * Sized for the largest consumer (ADPCM's echo line when present). */
#ifdef CONFIG_ENGINE_ADPCM
#define ENG_MEM_PART 13344u
#else
#define ENG_MEM_PART 12888u
#endif
static union {
    uint64_t align;
    uint8_t b[ENG_MEM_PART];
} eng_mem[NPART] __attribute__((section(".pool")));
#define ENG_MEM(p) ((void *)eng_mem[p].b)
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")
#endif

#ifdef CONFIG_ENGINE_FM6
#if defined(CONFIG_ENGINE_FM6_FMLAB)
#include "../../../ports/fmlab/engines/fm6_fmlab.c"
#elif defined(CONFIG_ENGINE_FM6_FELUCCA)
#include "felucca/engines/fm6_felucca.c"
#elif defined(CONFIG_ENGINE_FM6_MELODEE)
#include "../../../ports/melodee/engines/fm6_melodee.c"
#endif
#endif

#ifdef CONFIG_ENGINE_SAMPLE
#define SMP_SET_PERC 4u
#include "fm1_flash_map.h"
#define SMP_USER_XIP(k) fm1_xip_ptr(FLASH_SMP_USER(k))
#ifdef CONFIG_ENGINE_SLICE
#define FELUCCA_SLICE 1
#endif
#include "felucca/engines/eng_sample_modd.c"
#endif

#ifdef CONFIG_ENGINE_GRANULAR
#include "upstream/felucca/firmware/src/eng_grain.c"
#endif

#ifdef CONFIG_ENGINE_SLICE
#include "felucca/engines/eng_slice_modd.c"
#endif

#ifdef CONFIG_ENGINE_ADPCM
#include "felucca/engines/eng_adpcm_modd.c"
#endif

#ifdef CONFIG_ENGINE_STUB
#include "eng_stub.c"
#endif

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
/* The Felucca stack indexes ENGINES[] by upstream's engine numbers (core.h: ENGI_PHYS 9, ENGI_DRUM 10, NENGINES 14 + 1;
 * upstream engines.c). The slots of engines this build does not have hold a silent engine of their own. */
#ifndef CONFIG_ENGINE_STUB
#include "eng_stub.c"
#endif
#define ABSENT_ENGINE(sym, label)                                                       \
    static const engine_t sym = {                                                       \
        .name = label, .page_title = {label, ""},                                       \
        .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},             \
                 {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},             \
                 {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},             \
                 {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},            \
        .presets = stub_presets, .npresets = NELEM(stub_presets), .knob = {0, 1, 2, 3}, \
        .poly = 8, .note_on = stub_note_on, .render = stub_render, }
ABSENT_ENGINE(ENG_FM4_GONE, "-");
ABSENT_ENGINE(ENG_FORMANT, "VOICE");
ABSENT_ENGINE(ENG_PHYS, "PHYS");
ABSENT_ENGINE(ENG_DRUM, "DRUM");
#ifndef CONFIG_ENGINE_ANALOG
ABSENT_ENGINE(ENG_ANALOG, "ANALOG");
#endif
#ifndef CONFIG_ENGINE_PHASE
ABSENT_ENGINE(ENG_PHASE, "PHASE");
#endif
#ifndef CONFIG_ENGINE_LOFI
ABSENT_ENGINE(ENG_LOFI, "LOFI");
#endif
#ifndef CONFIG_ENGINE_SAMPLE
ABSENT_ENGINE(ENG_SAMPLE, "SAMPLE");
#endif
#ifndef CONFIG_ENGINE_TRIO
ABSENT_ENGINE(ENG_TRIO, "TRIO");
#endif
#ifndef CONFIG_ENGINE_TONEWHEEL
ABSENT_ENGINE(ENG_WHEEL, "WHEEL");
#endif
#ifndef CONFIG_ENGINE_GRANULAR
ABSENT_ENGINE(ENG_GRAIN, "GRAIN");
#endif
#ifndef CONFIG_ENGINE_NOISE
ABSENT_ENGINE(ENG_NOISE, "NOISE");
#endif
#ifndef CONFIG_ENGINE_FM6
ABSENT_ENGINE(ENG_FM6, "FM6");
#endif
#if FELUCCA_SLICE && !defined(CONFIG_ENGINE_SLICE)
ABSENT_ENGINE(ENG_SLICE, "SLICE");
#endif
#if FELUCCA_ADPCM && !defined(CONFIG_ENGINE_ADPCM)
ABSENT_ENGINE(ENG_ADPCM, "ADPCM");
#endif
/* upstream engines.c ENGINES[]: the engine numbers the stores and the stack name (append-only) */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG, &ENG_FM4_GONE, &ENG_PHASE, &ENG_LOFI, &ENG_SAMPLE, &ENG_FORMANT, &ENG_TRIO, &ENG_WHEEL,
    &ENG_GRAIN, &ENG_PHYS, &ENG_DRUM, &ENG_NOISE, &ENG_FM6,
#if FELUCCA_SLICE
    &ENG_SLICE,
#endif
#if FELUCCA_ADPCM
    &ENG_ADPCM,
#endif
};
#else /* !CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */
/* registry: order = engine numbers stored in presets and projects */
static const engine_t *const ENGINES[] = {
#ifdef CONFIG_ENGINE_ANALOG
    &ENG_ANALOG,
#endif
#ifdef CONFIG_ENGINE_NOISE
    &ENG_NOISE,
#endif
#ifdef CONFIG_ENGINE_LOFI
    &ENG_LOFI,
#endif
#ifdef CONFIG_ENGINE_TRIO
    &ENG_TRIO,
#endif
#ifdef CONFIG_ENGINE_PHASE
    &ENG_PHASE,
#endif
#ifdef CONFIG_ENGINE_TONEWHEEL
    &ENG_WHEEL,
#endif
#ifdef CONFIG_ENGINE_FM6
    &ENG_FM6,
#endif
#ifdef CONFIG_ENGINE_SAMPLE
    &ENG_SAMPLE,
#endif
#ifdef CONFIG_ENGINE_GRANULAR
    &ENG_GRAIN,
#endif
#ifdef CONFIG_ENGINE_SLICE
    &ENG_SLICE,
#endif
#ifdef CONFIG_ENGINE_ADPCM
    &ENG_ADPCM,
#endif
#ifdef CONFIG_ENGINE_STUB
    &ENG_STUB,
#endif
};


#undef NENGINES
#define NENGINES (sizeof(ENGINES) / sizeof(ENGINES[0]))
#endif /* CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */
