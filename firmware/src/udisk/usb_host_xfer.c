// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB host transfer engine: register-level SIE EP0 control + bulk IN/OUT.
 * Matches the reference fm1-base/bsp/chip/usb_host.c uh_ctl() sequence. */

#define USB_XFER_TIMEOUT  500  /* ms, matches reference */
#define USB_EP0_MAXP      64

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

/* One control transfer: SETUP + optional IN data stage + status stage.
 * Register-level only, no SDK functions. Matches reference uh_ctl(). */
static int usb_control_msg(uint8_t bmReq, uint8_t bReq, uint16_t wValue,
                           uint16_t wIndex, uint8_t *data, uint16_t wLength)
{
    int r;
    uint32_t got = 0;

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
    if (r < 0) return r;

    /* DATA IN stage */
    if (wLength && data && (bmReq & 0x80u)) {
        while (got < wLength) {
            usb_host_sie_wr(SIE_CSR0, CSR0H_ReqPkt);
            r = usb_host_csr0_wait(CSR0H_RxPktRdy, 0);
            if (r < 0) return r;

            uint32_t cnt = usb_host_sie_rd(SIE_COUNT0);
            if (cnt > 64u) cnt = 64u;
            if (cnt > wLength - got) cnt = wLength - got;

            fm1_usb_rx_sync();
            uint32_t i;
            for (i = 0; i < cnt; i++)
                data[got + i] = ep0buf[i];
            got += cnt;

            usb_host_sie_wr(SIE_CSR0, 0);

            if (cnt < USB_EP0_MAXP) break;
        }
        /* Status OUT (zero-length OUT after IN data) */
        usb_host_sie_wr(SIE_CSR0, CSR0H_StatusPkt | CSR0H_TxPktRdy);
        r = usb_host_csr0_wait(0, CSR0H_TxPktRdy);
    } else {
        /* Status IN (zero-length IN after no-data or OUT data) */
        usb_host_sie_wr(SIE_CSR0, CSR0H_StatusPkt | CSR0H_ReqPkt);
        r = usb_host_csr0_wait(CSR0H_RxPktRdy, 0);
        if (r >= 0)
            usb_host_sie_wr(SIE_CSR0, 0);
    }

    return r < 0 ? r : (int)got;
}

/* ---- Bulk Transfers (register-level, matching reference uh_tx/rx) ---- */
/* Separate DMA buffers per the reference: ep0buf for TX, usb_enum_buf for RX.
 * EP0 control and EP1 bulk never overlap. */

static int usb_bulk_out(uint8_t target_ep, const uint8_t *data, uint32_t len)
{
    uint32_t sent = 0;
    int ok;
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > uhost.max_packet) chunk = uhost.max_packet;
        uint32_t i;
        for (i = 0; i < chunk; i++) ep0buf[i] = data[sent + i];
        fm1_usb_ep_send(USB_BULK_HOST_EP, ep0buf, chunk);
        usb_host_sie_wr(SIE_INDEX, USB_BULK_HOST_EP);
        usb_host_sie_wr(SIE_TXTYPE, (uint32_t)(0x20u | target_ep));
        usb_host_sie_wr(SIE_TXCSR1, 0x01u);
        ok = 0;
        for (i = 0; i < USB_XFER_TIMEOUT; i++) {
            uint32_t csr = usb_host_sie_rd(SIE_TXCSR1);
            if (csr & 0x20u) return -1;
            if (csr & 0x04u) { usb_host_sie_wr(SIE_TXCSR1, 0x01u); continue; }
            if (!(csr & 0x01u)) { ok = 1; break; }
            fm1_delay_ms(1);
        }
        if (!ok) return -3;
        sent += chunk;
    }
    return (int)sent;
}

static int usb_bulk_in(uint8_t target_ep, uint8_t *data, uint32_t len)
{
    uint32_t got = 0;
    int ok;
    fm1_usb_ep_rxbuf(USB_BULK_HOST_EP, usb_host_rxbuf);
    while (got < len) {
        usb_host_sie_wr(SIE_INDEX, USB_BULK_HOST_EP);
        usb_host_sie_wr(SIE_RXTYPE, (uint32_t)(0x20u | target_ep));
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
            data[got + i] = usb_host_rxbuf[i];
        got += n;
        usb_host_sie_wr(SIE_RXCSR1, 0x10u);
        if (n < uhost.max_packet) break;
    }
    return (int)got;
}
