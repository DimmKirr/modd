/* SPDX-License-Identifier: GPL-3.0-only */
/* Console adapter: CDC serial console stubs. */
#ifndef MODD_CONSOLE_H
#define MODD_CONSOLE_H
#include <stdint.h>

#ifndef CONFIG_CONSOLE
static void console_init(void) {}
static void console_poll(void) {}
#endif

#endif /* MODD_CONSOLE_H */
