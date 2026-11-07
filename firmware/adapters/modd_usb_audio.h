// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB Audio adapter: stubs when CONFIG_USB_AUDIO is off. */
#ifndef MODD_USB_AUDIO_H
#define MODD_USB_AUDIO_H
#include <stdint.h>

#ifndef CONFIG_USB_AUDIO
static void ua_init(void) {}
static void ua_service(void) {}
static void ua_start(void) {}
static void ua_stop(void) {}
#endif

#endif /* MODD_USB_AUDIO_H */
