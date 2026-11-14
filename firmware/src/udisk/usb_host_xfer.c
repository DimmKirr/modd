/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio), Adrien Tibi (Recursive Field Instruments)
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: keremimo/melodee firmware/src/usb_host.c @ v1.6.1 (165d177a)
 *         https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/usb_host.c
 *         (uh_csr0 / uh_ctl: the EP0 control transfer and CSR0 wait sequence; reimplemented) */
/* USB host transfer engine: register-level SIE EP0 control + bulk IN/OUT. */

#define USB_XFER_TIMEOUT  500   /* ms, matches reference */
#define USB_BULK_FS       0x20u /* bulk protocol (speed=00: use detected) */

/* Poll CSR0 until want_set bits are set and want_clr bits are clear.
 * Returns CSR0 value on success, negative on error/stall/timeout. */
static int usb_host_csr0_wait(uint32_t want_set, uint32_t want_clr)
{
    uint32_t t = USB_XFER_TIMEOUT;
    while (t--) {
        uint32_t v = usb_host_sie_rd(SIE_CSR0);
        if (v & CSR0H_RxStall) {
            usb_host_sie_wr(SIE_CSR0, 0);
            return -2;
        }
        if (v & CSR0H_Error) {
            usb_host_sie_wr(SIE_CSR0, 0);
            return -1;
        }
        if ((v & want_set) == want_set && !(v & want_clr))
            return (int)v;
        fm1_delay_ms(1);
    }
    return -3;
}

/* "usb: ctl RT/RQ stS r=R csr=XX fa=NN": which request failed, at which
 * stage (0 setup, 1 data, 2 status), with CSR0 and FADDR as they were. */
static void usb_host_log_ctl_fail(uint8_t bmReq, uint8_t bReq, uint8_t stage, int r)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[MODD_LOG_LINE_LEN];
    uint32_t csr0 = usb_host_sie_rd(SIE_CSR0);
    uint32_t fa = usb_host_sie_rd(SIE_FADDR);
    int n = 0;
    const char *p = "usb: ctl ";
    while (*p) line[n++] = *p++;
    line[n++] = hex[bmReq >> 4]; line[n++] = hex[bmReq & 0xF];
    line[n++] = '/';
    line[n++] = hex[bReq >> 4]; line[n++] = hex[bReq & 0xF];
    p = " st"; while (*p) line[n++] = *p++;
    line[n++] = (char)('0' + stage);
    p = " r="; while (*p) line[n++] = *p++;
    if (r < 0) { line[n++] = '-'; r = -r; }
    line[n++] = (char)('0' + (r % 10));
    p = " csr="; while (*p) line[n++] = *p++;
    line[n++] = hex[(csr0 >> 4) & 0xF]; line[n++] = hex[csr0 & 0xF];
    p = " fa="; while (*p) line[n++] = *p++;
    line[n++] = hex[(fa >> 4) & 0xF]; line[n++] = hex[fa & 0xF];
    line[n] = 0;
    modd_log(line);
    uhost.ctl_rt = bmReq;
    uhost.ctl_rq = bReq;
    uhost.ctl_stage = stage;
    uhost.ctl_r = (int8_t)-r;
    uhost.ctl_csr0 = (uint8_t)csr0;
    uhost.ctl_faddr = (uint8_t)fa;
}

/* One control transfer: SETUP + optional IN data stage + status stage.
 * Register-level only, no SDK functions. */
static int usb_control_msg(uint8_t bmReq, uint8_t bReq, uint16_t wValue,
                           uint16_t wIndex, uint8_t *data, uint16_t wLength)
{
    int r;
    uint32_t got = 0;
    uint8_t stage = 0;

    /* Build setup packet in ep0buf */
    ep0buf[0] = bmReq;
    ep0buf[1] = bReq;
    ep0buf[2] = (uint8_t)(wValue & 0xFF);
    ep0buf[3] = (uint8_t)(wValue >> 8);
    ep0buf[4] = (uint8_t)(wIndex & 0xFF);
    ep0buf[5] = (uint8_t)(wIndex >> 8);
    ep0buf[6] = (uint8_t)(wLength & 0xFF);
    ep0buf[7] = (uint8_t)(wLength >> 8);

    /* SETUP stage: INDEX first (selects EP0 FIFO), then DMA, then CSR0 */
    usb_host_sie_wr(SIE_INDEX, 0);
    fm1_usb_ep0_send(ep0buf, 8);
    usb_host_sie_wr(SIE_CSR0, CSR0H_SetupPkt | CSR0H_TxPktRdy);

    r = usb_host_csr0_wait(0, CSR0H_TxPktRdy);
    if (r < 0) goto fail;

    /* DATA IN stage */
    if (wLength && data && (bmReq & 0x80u)) {
        stage = 1;
        while (got < wLength) {
            usb_host_sie_wr(SIE_CSR0, CSR0H_ReqPkt);
            r = usb_host_csr0_wait(CSR0H_RxPktRdy, 0);
            if (r < 0) goto fail;

            uint32_t cnt = usb_host_sie_rd(SIE_COUNT0);
            if (cnt > 64u) cnt = 64u;
            if (cnt > wLength - got) cnt = wLength - got;

            fm1_usb_rx_sync();
            uint32_t i;
            for (i = 0; i < cnt; i++)
                data[got + i] = ep0buf[i];
            got += cnt;

            usb_host_sie_wr(SIE_CSR0, 0);

            /* a packet shorter than the device's EP0 size ends the stage;
             * an 8-byte EP0 (C-Media audio, many HID) sends 8 at a time */
            if (cnt < uhost.ep0_mps) break;
        }
        /* Status OUT (zero-length OUT after IN data) */
        stage = 2;
        usb_host_sie_wr(SIE_CSR0, CSR0H_StatusPkt | CSR0H_TxPktRdy);
        r = usb_host_csr0_wait(0, CSR0H_TxPktRdy);
    } else {
        /* Status IN (zero-length IN after no-data or OUT data) */
        stage = 2;
        usb_host_sie_wr(SIE_CSR0, CSR0H_StatusPkt | CSR0H_ReqPkt);
        r = usb_host_csr0_wait(CSR0H_RxPktRdy, 0);
        if (r >= 0)
            usb_host_sie_wr(SIE_CSR0, 0);
    }
    if (r < 0) goto fail;
    return (int)got;

fail:
    usb_host_log_ctl_fail(bmReq, bReq, stage, r);
    return r;
}

/* ---- Bulk Transfers (register-level, matching reference uh_tx/rx) ---- */
/* Separate DMA buffers per the reference: ep0buf for TX, usb_enum_buf for RX.
 * EP0 control and EP1 bulk never overlap. */

/* host_ep: the SIE host endpoint (1 = disk, 2 = network), dma: its 64-byte
 * DMA buffer, max_packet: the target endpoint's size (64 at full speed). */
static int usb_bulk_out(uint8_t host_ep, uint8_t *dma, uint32_t max_packet,
                        uint8_t target_ep, const uint8_t *data, uint32_t len)
{
    uint32_t sent = 0;
    int ok;
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > max_packet) chunk = max_packet;
        uint32_t i;
        for (i = 0; i < chunk; i++) dma[i] = data[sent + i];
        fm1_usb_ep_send(host_ep, dma, chunk);
        usb_host_sie_wr(SIE_INDEX, host_ep);
        usb_host_sie_wr(SIE_TXTYPE, (uint32_t)(USB_BULK_FS | target_ep));
        usb_host_sie_wr(SIE_TXCSR1, 0x01u);
        ok = 0;
        uint32_t errs = 0;
        for (i = 0; i < USB_XFER_TIMEOUT; i++) {
            uint32_t csr = usb_host_sie_rd(SIE_TXCSR1);
            if (csr & 0x20u) {
                usb_host_log_hex8("usb: txstall csr=", csr);
                return -1;
            }
            if (csr & 0x04u) { errs++; usb_host_sie_wr(SIE_TXCSR1, 0x01u); continue; }
            if (!(csr & 0x01u)) { ok = 1; break; }
            fm1_delay_ms(1);
        }
        if (!ok) {
            usb_host_log_hex8("usb: txtmo csr=",
                              usb_host_sie_rd(SIE_TXCSR1));
            modd_log_int("usb: txtmo errs=", errs);
            usb_host_log_hex8("usb: txtmo fa=",
                              usb_host_sie_rd(SIE_FADDR));
            return -3;
        }
        sent += chunk;
    }
    return (int)sent;
}

static int usb_bulk_in(uint8_t host_ep, uint8_t *dma, uint32_t max_packet,
                       uint8_t target_ep, uint8_t *data, uint32_t len)
{
    uint32_t got = 0;
    int ok;
    fm1_usb_ep_rxbuf(host_ep, dma);
    while (got < len) {
        usb_host_sie_wr(SIE_INDEX, host_ep);
        usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(USB_BULK_FS | target_ep));
        usb_host_sie_wr(SIE_RXCSR1, 0x20u);
        ok = 0;
        for (uint32_t t = 0; t < USB_XFER_TIMEOUT; t++) {
            uint32_t csr = usb_host_sie_rd(SIE_RXCSR1);
            if (csr & 0x40u) {
                usb_host_log_hex8("usb: rxstall csr=", csr);
                return -1;
            }
            if (csr & 0x0Cu) {
                usb_host_sie_wr(SIE_RXCSR1, 0x20u);
                continue;
            }
            if (csr & 0x01u) { ok = 1; break; }
            fm1_delay_ms(1);
        }
        if (!ok) {
            usb_host_log_hex8("usb: rxtmo csr=",
                              usb_host_sie_rd(SIE_RXCSR1));
            return -3;
        }
        uint32_t n = usb_host_sie_rd(SIE_RXCOUNT1) |
                     (usb_host_sie_rd(SIE_RXCOUNT2) << 8);
        if (n > 64u) n = 64u;
        fm1_usb_rx_sync();
        uint32_t i;
        for (i = 0; i < n && got + i < len; i++)
            data[got + i] = dma[i];
        got += n;
        usb_host_sie_wr(SIE_RXCSR1, 0x10u);
        if (n < max_packet) break;
    }
    return (int)got;
}
