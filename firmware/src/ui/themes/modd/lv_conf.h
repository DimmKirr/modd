/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dmitry Kireev (@DimmKirr) */
/* LVGL 9.6 configuration for the modd theme (CONFIG_UI_THEME_MODD).
 *
 * Found through -DLV_CONF_INCLUDE_SIMPLE and an include path (tools/build/build.py
 * lvgl_flags), by the LVGL library and by modd.c alike. An option not set here keeps
 * LVGL's default; names, defaults and what each one does:
 * firmware/vendor/lvgl/lv_conf_template.h and docs/lvgl/docs/src/integration/configuration.mdx.
 * Changing this file rebuilds the whole library. */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ------------------------------------------------------ memory, C library --- */
/* LVGL's own allocator, string and printf code: nothing from a C library is linked.
 * (modd supplies memset/memcpy in libc.c and 64-bit division in rt64.c.) */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

/* modd.c has no C library headers on its include path: the format macros come from the
 * theme's own header (the same one for the library, so both sides agree) */
#define LV_INTTYPES_INCLUDE "lvt_inttypes.h"
/* The heap every widget, style and label text comes from. LVGL's rule of thumb is
 * 4 kB + 150 bytes a widget; a page here has well under a hundred widgets and the
 * previous page is deleted when the next one is built. The DEBUG page shows the use. */
#define LV_MEM_SIZE (32 * 1024)
#define LV_MEM_ADR 0x0
/* The heap is LVGL's one large array. The 96 KB .data/.bss region is nearly full, so
 * it goes to .pool with the other big buffers (app.ld collects .pool*; not zeroed at
 * boot, which the allocator does not need). */
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY __attribute__((section(".pool_lvgl")))

/* ------------------------------------------------------------- rendering --- */
/* The ST7789 takes RGB565 high byte first. LVGL renders that format natively, so the
 * flush callback hands the buffer to the DMA as it is. */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565_SWAPPED
#define LV_DEF_REFR_PERIOD 33           /* ms: refresh, input read and animation step */
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE            /* everything LVGL runs from the main loop */

#define LV_USE_DRAW_SW 1                /* the software renderer; the SoC has no 2D unit */
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_SW_COMPLEX 1            /* rounded corners, arcs, shadows */
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 0
#define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
/* Formats the renderer can blend: the screen's, glyph masks (A8), and the two a
 * layer may use when a widget is drawn with whole-object opacity. */
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1
#define LV_DRAW_SW_SUPPORT_RGB565A8 1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0
/* A layer is allocated from the heap above: keep its chunk small. */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (4 * 1024)

#define LV_CACHE_DEF_SIZE 0             /* no decoded-image cache: there are no images */
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0
#define LV_GRADIENT_MAX_STOPS 2

/* --------------------------------------------------------------- checks --- */
#define LV_USE_LOG 0
#define LV_USE_CHECK_ARG 1              /* a bad argument returns instead of crashing */

/* ---------------------------------------------------------------- fonts --- */
/* LVGL's built-in Montserrat (4 bits a pixel, ASCII and the LV_SYMBOL_* icons). */
#define LV_FONT_MONTSERRAT_10 1         /* log lines, captions */
#define LV_FONT_MONTSERRAT_14 1         /* everything else */
#define LV_FONT_MONTSERRAT_20 1         /* the tempo */
#define LV_FONT_DEFAULT LV_FONT_DEFAULT_MONTSERRAT_14
#define LV_USE_FONT_PLACEHOLDER 0
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* -------------------------------------------------------------- widgets --- */
/* Only what the pages use; a widget switched off here costs no flash even where the
 * default theme would style it. */
#define LV_WIDGETS_HAS_DEFAULT_VALUE 0
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_DROPDOWN 1               /* requires label */
#define LV_USE_IMAGE 1                  /* requires label */
#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT 0
#define LV_USE_LED 1
#define LV_USE_SWITCH 1
#define LV_USE_3DTEXTURE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ARCLABEL 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LINE 1                   /* HOME: the envelope */
#define LV_USE_LIST 0
#define LV_USE_LOTTIE 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/* ----------------------------------------------------- themes, layouts --- */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0         /* no grow-on-press: nothing is touched here */
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* --------------------------------------------------------------- others --- */
#define LV_USE_OBSERVER 1               /* subjects: a value changes, bound widgets follow */
#define LV_USE_FLOAT 0
#define LV_USE_MATRIX 0
#define LV_USE_OBJ_ID 0
#define LV_USE_OBJ_NAME 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_GESTURE_RECOGNITION 0
#define LV_USE_SYSMON 0
#define LV_USE_PROFILER 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_GRIDNAV 0
#define LV_USE_TRANSLATION 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
