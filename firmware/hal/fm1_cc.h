/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Origin: hugelton/Felucca firmware/hal/fm1_cc.h @ v1.6.1 (a53ecf42)
 *         https://github.com/hugelton/Felucca/blob/a53ecf428c29c780108fd70a31b71a32a6b5c313/firmware/hal/fm1_cc.h */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))
