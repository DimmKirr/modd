/* SPDX-License-Identifier: GPL-3.0-only */
/* Origin: hugelton/Felucca firmware/src/eng_adpcm.c @ v1.8.2 (d0260074)
 * Wrapper: adapts adp_post signature (Felucca: 3-arg uint32_t return;
 * modd engine_t: 4-arg void) and rebuilds the ENG_ADPCM vtable. */

#ifdef CONFIG_AUDIO_FELUCCA_STACK_INCLUDED
/* the stack's engine_t is upstream's: its ENG_ADPCM and adp_post fit as they are */
#include "upstream/felucca/firmware/src/eng_adpcm.c"
#else /* !CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */
/* Rename the upstream function and vtable, include the source */
#define adp_post  _felucca_adp_post
#define ENG_ADPCM _FELUCCA_ENG_ADPCM

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#include "upstream/felucca/firmware/src/eng_adpcm.c"
#pragma clang diagnostic pop

#undef adp_post
#undef ENG_ADPCM

static void adp_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr)
{
    (void)nr;
    _felucca_adp_post(t, out, n);
}

static const engine_t ENG_ADPCM = {
    .name = "ADPCM",
    .page_title = {"VOICE", "ECHO"},
    .edit = {
        {"INST", F_ENUM, 0, ADP_NINST - 1, 0, ADP_INST_NAMES, 0},
        {"TUNE", F_SEMI, -24, 24, 0, 0, 0},
        {"SR", F_INT, 0, 31, 0, 0, 0},
        {"NOISE", F_INT, 0, 31, 0, 0, 0},
        {"ECHO", F_PCT, 0, 127, 0, 0, 0},
        {"DELAY", F_ENUM, 0, ADP_EDL_MAX, 4, N_ADP_EDL, "ms"},
        {"FDBK", F_BIPCT, -64, 63, 0, 0, 0},
        {"FIR", F_ENUM, 0, 3, 1, N_ADP_FIR, 0},
    },
    .presets = ADP_PRESET_TABLE,
    .npresets = NELEM(ADP_PRESET_TABLE),
    .note_on = adp_note_on,
    .render = adp_render,
    .knob = {P_E0, P_E2, P_ATK, P_REL},
    .sampled = 1,
    .block = adp_block,
    .ownenv = 1,
    .done = adp_done,
    .post = adp_post,
    .mem = sizeof(adp_part_t),
};
#endif /* CONFIG_AUDIO_FELUCCA_STACK_INCLUDED */
