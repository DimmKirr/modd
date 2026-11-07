/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor adapter: SysEx web editor protocol stubs. */
#ifndef MODD_EDITOR_H
#define MODD_EDITOR_H
#include <stdint.h>

#ifndef CONFIG_EDITOR
static void editor_frame(const uint8_t *data, uint32_t len)
{
    (void)data; (void)len;
}
static void editor_poll(void) {}
static void editor_init(void) {}
#endif

#endif /* MODD_EDITOR_H */
