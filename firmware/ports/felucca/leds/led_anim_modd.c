/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Felucca LED animations: wrapper.
 * Origin: hugelton/Felucca firmware/bsp/boards/fm1/fm1_led_anim.h @ v1.8.2 (6162de86)
 * (the file is in Felucca's felucca-bsp submodule, MPL-2.0)
 * Wrapper: includes upstream's picture code (the power-on sweep and the idle animation, pure functions)
 * unchanged, then the frame driver that draws it onto Melodee's LED arrays (led_anim_drv_modd.c). */

#include "upstream/felucca/firmware/bsp/boards/fm1/fm1_led_anim.h"
#include "led_anim_drv_modd.c"
