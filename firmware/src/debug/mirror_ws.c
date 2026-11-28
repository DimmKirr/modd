// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* LCD mirror over WebSocket: adapts fm1-base's mirror.c (MPL-2.0, Charles
 * Vestal) for WebSocket binary transport.  The pixel codec, segment hashing
 * and dirty-row recovery are upstream's; this file replaces the USB-MIDI
 * SysEx framing with raw binary WebSocket frames (8-bit clean, no 7-bit
 * packing).
 *
 * Wire format per WS binary message:
 *   byte 0      command (0x50..0x54, same as mirror.c's MR_*)
 *   byte 1-2    body length, u16 LE
 *   byte 3+     body (layout identical to mirror.c's protocol)
 *
 * Include order: this file must come AFTER rpc_ws.c (needs ws_state,
 * ws_send_bin) and AFTER the LVGL headers.  mirror_blit / mirror_fill
 * are called from lcd.c which is included earlier; modd.c forward-declares
 * them so the compiler sees them before lcd.c's calls. */

#ifdef CONFIG_MIRROR

/* mirror_on is forward-declared in modd.c (before lcd.c) so the LCD patch
 * can test it.  Do not redeclare it here. */

/* --- platform shims mirror.c reads (plat.h on fm1-base) --- */
#define plat_ms()       ((uint32_t)fm1_ms)
#define plat_buttons()  ((uint32_t)fm1_in.buttons)
#define plat_keys()     ((uint32_t)fm1_in.notes)
#define plat_master()   0u
#define NB              14
#define NKEYS           27
#define PLAT_POOL       __attribute__((section(".pool")))
#define MRQ             8192u

/* --- 7-bit SysEx unpack (inverse of mr_pack7 in mirror.c) --- */
static uint32_t mr_unpack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0, i;
    for (i = 0; i < n; i++) {
        acc |= (uint32_t)in[i] << nb;
        nb += 7u;
        while (nb >= 8u) {
            out[o++] = (uint8_t)(acc & 0xFFu);
            acc >>= 8;
            nb -= 8u;
        }
    }
    return o;
}

/* --- transport hooks (mirror.c lines 95-101) --- */
static uint32_t mr_free(void)  { return MRQ; }

/* mr_put receives SysEx-wrapped frames from upstream's mr_seal:
 *   [0xF0, 7-bit-packed(hdr[6] + body[n] + chk[1]), 0xF7]
 * We unpack, extract cmd and body, then send raw binary:
 *   [cmd, len_lo, len_hi, body...] */
static uint8_t  mr_unp[2048];
static void mr_put(const uint8_t *p, uint32_t n)
{
    if (n < 3 || p[0] != 0xF0 || p[n - 1] != 0xF7) return;
    uint32_t ulen = mr_unpack7(p + 1, n - 2, mr_unp);
    if (ulen < 7) return;
    uint32_t cmd  = mr_unp[2];
    uint32_t blen = (uint32_t)mr_unp[3] | ((uint32_t)mr_unp[4] << 8);
    if (6 + blen > ulen) return;
    static uint8_t raw[2048];
    raw[0] = (uint8_t)cmd;
    raw[1] = (uint8_t)blen;
    raw[2] = (uint8_t)(blen >> 8);
    uint32_t i;
    for (i = 0; i < blen && 3 + i < sizeof raw; i++)
        raw[3 + i] = mr_unp[6 + i];
    ws_send_bin(raw, 3 + blen);
}

static int      mr_link(void)  { return ws_state == WS_STATE_OPEN ? 1 : -1; }
static int      mr_busy(void)  { return ws_state != WS_STATE_OPEN; }
static int      mr_ctl_send(const uint8_t *p, uint32_t n)
{
    if (ws_state != WS_STATE_OPEN) return -1;
    mr_put(p, n);
    return 0;
}
static void mr_redraw(void)
{
#ifdef CONFIG_UI_SHELL_MODD
    lv_obj_invalidate(lv_screen_active());
#endif
}
static int mr_test(int on) { (void)on; return 0; }

/* upstream's mr_seal and mr_pack7 are used as-is (SysEx framing);
 * mr_put above strips the SysEx wrapper before sending over WebSocket */
#include "../../upstream/fm1-base/device/mirror.c"

/* --- incoming binary WS frames from the browser --- */
static void mirror_ws_recv(const uint8_t *data, uint32_t len)
{
    uint32_t cmd, blen;
    const uint8_t *body;
    if (len < 3) return;
    cmd  = data[0];
    blen = (uint32_t)data[1] | ((uint32_t)data[2] << 8);
    if (3 + blen > len) return;
    body = data + 3;
    if (cmd == MR_REQ && blen >= 1)
        mirror_cmd(body[0]);
    else if (cmd == MR_REMOTE && blen >= 5)
        mirror_remote(body);
}

/* Called from services_task alongside rpc_poll */
static void mirror_ws_step(void)
{
    mirror_step();
}

#endif /* CONFIG_MIRROR */
