// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Shared by the application and assembly startup. */
#ifndef MODD_BUILD_OPTIONS_H
#define MODD_BUILD_OPTIONS_H
#ifndef MODD_DUAL_CORE
#define MODD_DUAL_CORE 0
#endif
#ifndef MELODEE_DUAL_CORE
#define MELODEE_DUAL_CORE MODD_DUAL_CORE
#endif
#ifndef MELODEE_CORE1_FALLBACK
#define MELODEE_CORE1_FALLBACK 0
#endif
#endif
