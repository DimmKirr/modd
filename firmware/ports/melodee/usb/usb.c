/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/usb.c @ v1.6.1 (165d177a)
 *         https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/usb.c */
/* USB MIDI + device driver: the upstream file, included through the patched copy
 * (patches/melodee/0003-usb-midi-build-hooks.patch; class PATCHED). modd supplies, through the hooks, the
 * USB strings, modd's HAL header and ep0buf (shared with the host stack), the USB event log, the identity
 * handshake, the settings-reset SysEx and the debug-DAP SysEx channel. */

/* EP0BUF_DEFINED comes from modd.c when the host stack owns ep0buf */
#define USB_HAL_HEADER "fm1_usb.h"               /* modd's HAL copy (same registers; already included once) */

/* --- strings: "Core FM1" --- */
#define USB_STRINGS_DEFINED
static const uint8_t STR1[] = {20, 3, 'C', 0, 'o', 0, 'r', 0, 'e', 0, ' ', 0, 'F', 0, 'M', 0, '1', 0, };
#ifdef MELODEE_OTA
static const uint8_t STR2[] = {32, 3, 'C', 0, 'o', 0, 'r', 0, 'e', 0, ' ', 0, 'F', 0, 'M', 0, '1', 0,
                               ' ', 0, 'U', 0, 'p', 0, 'd', 0, 'a', 0, 't', 0, 'e', 0};
#else
static const uint8_t STR2[] = {20, 3, 'C', 0, 'o', 0, 'r', 0, 'e', 0, ' ', 0, 'F', 0, 'M', 0, '1', 0};
#endif

#ifndef MODD_PACKAGE_ID
#define MODD_PACKAGE_ID "FM-1_900"
#endif

#ifdef CONFIG_SYS_MENU
static volatile uint8_t settings_reset_req;
#endif

/* Hooks defined after the include (they need the driver's statics). */
static void modd_usb_sysex_end(void);

#ifndef MELODEE_OTA
#define USB_HOOK_CONFIG() do { \
        if (usb.config) { modd_log("USB: device attached."); modd_log_hex("USB: id: ", DEV_DESC + 8, 4); } \
        else modd_log("USB: device detached."); } while (0)
#define USB_HOOK_SUSPEND() do { if (!usb.suspended) modd_log("USB: suspend"); } while (0)
#define USB_HOOK_RESUME() do { if (usb.suspended) modd_log("USB: resume"); } while (0)
#define USB_HOOK_RESET() modd_log_int("USB: reset #", (int32_t)(usb.resets + 1))
#define USB_HOOK_MIDI_IN(pkt) do { \
        uint8_t hx[3] = {(uint8_t)st, (uint8_t)((pkt) >> 16), (uint8_t)((pkt) >> 24)}; \
        modd_log_hex("MI< ", hx, cin == 0xCu || cin == 0xDu ? 2u : 3u); } while (0)
#endif
#ifdef CONFIG_DEBUG_DAP
#define USB_HOOK_SYSEX_BEGIN() do { modd_dbg_sx_on = !modd_dbg_sx_ready; modd_dbg_sx_pos = 0; } while (0)
#define USB_HOOK_SYSEX_BYTE(b) do { \
        if (modd_dbg_sx_on) { \
            if (modd_dbg_sx_pos < sizeof modd_dbg_sx) modd_dbg_sx[modd_dbg_sx_pos++] = (b); \
            else modd_dbg_sx_on = 0; } } while (0)
#define USB_HOOK_SYSEX_DONE() (modd_dbg_sx_on = 0)
#endif
#define USB_HOOK_SYSEX_END() modd_usb_sysex_end()

#include "upstream/melodee/firmware/src/usb.c"

/* Identity handshake: fm1_install.py sends F0 00 32 45 00 00 00 40 7F F7 (Felucca OTA cmd 0x11); we reply
 * with the package identity so the installer can find us. */
static const uint8_t HS_QUERY[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};

static void sx_push_pkt(uint32_t pkt)
{
    if (mo_w - mo_r < MQ) {
        midi_out_q[mo_w % MQ] = pkt;
        RING_PUBLISH();
        mo_w++;
    }
}

static void hs_reply_identity(void)
{
    static const char ID[] = MODD_PACKAGE_ID;
    uint8_t msg[34], wire[48];
    uint32_t i, s = 0, wn, wi;

    msg[0] = 0x00; msg[1] = 0x59; msg[2] = 0x11;
    msg[3] = 27; msg[4] = 0; msg[5] = 0;
    for (i = 0; i < 27u; i++) {
        msg[6 + i] = i < sizeof ID - 1u ? (uint8_t)ID[i] : 0;
        s += msg[6 + i];
    }
    msg[33] = (uint8_t)~s;

    /* pack7 */
    wire[0] = 0xF0;
    wn = 1;
    {
        uint32_t acc = 0, nb = 0;
        for (i = 0; i < 34u; i++) {
            acc |= (uint32_t)msg[i] << nb;
            nb += 8u;
            while (nb >= 7u) {
                wire[wn++] = (uint8_t)(acc & 0x7Fu);
                acc >>= 7u;
                nb -= 7u;
            }
        }
        if (nb)
            wire[wn++] = (uint8_t)(acc & 0x7Fu);
    }
    wire[wn++] = 0xF7;

    /* push USB-MIDI event packets into midi_out_q */
    for (wi = 0; wi < wn; ) {
        uint32_t remain = wn - wi, k, cin, pkt;
        uint8_t b0, b1, b2;
        if (remain >= 3u && wire[wi + 2u] != 0xF7u)
            { cin = 4u; k = 3u; }
        else if (remain >= 3u)
            { cin = 7u; k = 3u; }
        else if (remain == 2u)
            { cin = 6u; k = 2u; }
        else
            { cin = 5u; k = 1u; }
        b0 = wire[wi];
        b1 = k > 1u ? wire[wi + 1u] : 0u;
        b2 = k > 2u ? wire[wi + 2u] : 0u;
        pkt = cin | ((uint32_t)b0 << 8u) | ((uint32_t)b1 << 16u) | ((uint32_t)b2 << 24u);
        sx_push_pkt(pkt);
        wi += k;
    }
}

/* modd's SysEx commands, run when a message is complete (before upstream's own matching). A message taken
 * here is not handed on to the OTA / editor frame reader. */
static void modd_usb_sysex_end(void)
{
    uint32_t i, hm, taken = 0;
#ifndef MELODEE_OTA
#ifdef CONFIG_DEBUG_DAP
    if (!(usb.sx_len >= 4u && usb.sysex[1] == 0x7Du && usb.sysex[2] == 0x48u && usb.sysex[3] == 0x42u))
#endif
    {
        uint32_t show = usb.sx_len < 8u ? usb.sx_len : 8u;
        modd_log_hex("SX< ", usb.sysex, show);
        if (usb.sx_len > 8u)
            modd_log_int("  ..len ", (int32_t)usb.sx_len);
    }
#endif
#ifdef CONFIG_SYS_MENU
    if (usb.sx_len == 6u && usb.sysex[1] == 0x7Du && usb.sysex[2] == 0x46u &&
        usb.sysex[3] == 0x4Cu && usb.sysex[4] == 0x7Fu) {
        settings_reset_req = 1;
        taken = 1;
    }
#endif
#ifdef MODD_BLE_STUB
    if (!taken && usb.sx_len >= 4u && usb.sysex[1] == 0x7Du && usb.sysex[2] == 0x42u && usb.sysex[3] == 0x4Cu) {
        ble_hw_stub_set_peer(usb.sysex, usb.sx_len);
        taken = 1;
    }
#endif
    if (!taken && usb.sx_len == sizeof HS_QUERY) {
        hm = 1u;
        for (i = 0; hm && i < sizeof HS_QUERY; i++)
            hm = usb.sysex[i] == HS_QUERY[i];
        if (hm) {
            hs_reply_identity();
            taken = 1;
        }
    }
#ifdef CONFIG_DEBUG_DAP
    if (!taken && modd_dbg_sx_on && modd_dbg_sx_pos >= 5u &&
        modd_dbg_sx[0] == 0x7Du && modd_dbg_sx[1] == 0x48u && modd_dbg_sx[2] == 0x42u) {
        modd_dbg_sx_ready = 1;
        taken = 1;
    }
#endif
#if MELODEE_OTA
    if (taken)
        sx_collect = 0;
#endif
    (void)taken; (void)i; (void)hm;
}
