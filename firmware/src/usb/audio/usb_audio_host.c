// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* USB Audio Class 1 host driver: stream audio to an external USB DAC.
 * Lifecycle follows usbnet.c: init / parse+attach / poll / detach. */

#ifdef CONFIG_USB_AUDIO_HOST

/* ---- UAC1 descriptor constants ---- */
#define UAC_SUBCLASS_CONTROL    0x01
#define UAC_SUBCLASS_STREAMING  0x02
#define UAC_DT_CS_INTERFACE     0x24
#define UAC_DT_CS_ENDPOINT      0x25
#define UAC_AS_FORMAT_TYPE      0x02
#define UAC_SET_CUR             0x01
#define UAC_SAMPLING_FREQ_CTL   0x01

/* ---- Ring buffer ---- */
#define UAC_RING_MAX_PACKET  1024
#ifdef CONFIG_EMU
#define UAC_RING_FRAMES      64
static uint8_t uac_ring[UAC_RING_FRAMES * UAC_RING_MAX_PACKET] __attribute__((section(".framebuffer")));
#else
#define UAC_RING_FRAMES      4
static uint8_t uac_ring[UAC_RING_FRAMES * UAC_RING_MAX_PACKET] __attribute__((section(".pool_uac")));
#endif
#define UAC_RING_SIZE        (UAC_RING_FRAMES * UAC_RING_MAX_PACKET)
static volatile uint32_t uac_ring_wr, uac_ring_rd;
static volatile uint8_t uac_ring_frozen;   /* set by /api/usb/audio/ring: feed stops writing */
static uint32_t uac_frame_bytes;

/* 44.1 kHz fractional accumulator: 44.1 samples/frame = 44 + 0.1,
 * so every 10th frame sends 45 samples instead of 44. */
static uint32_t uac_frac_accum;

/* SRC 44.1 -> 48 kHz (linear interpolation, integer only) */
static uint32_t uac_src_phase;    /* Q16 fractional position in source */
static int32_t  uac_src_prev[2];  /* last L, R of previous buffer for cross-boundary interpolation */
#define UAC_SRC_STEP  60211u      /* 44100/48000 in Q16 = 0.91875 * 65536 */

static uint8_t uac_dma_tx[UAC_RING_MAX_PACKET] __attribute__((aligned(4), section(".pool_uac")));

static uint8_t uac_wanted;
static uint8_t uac_cfg_value;
static uint8_t uac_test_mode;

/* ---- Helpers ---- */

static uint32_t uac_read24le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static void uac_select(void)
{
    usb_host_sie_wr(SIE_FADDR, uhost.uac_addr);
    FM1_EP_TXFUNCADDR(USB_AUDIO_HOST_EP) = uhost.uac_addr;
    uhost.ep0_mps = uhost.uac_ep0_mps;
}

static int uac_rate_supported(const uint8_t *fmt, uint32_t rate)
{
    uint8_t n = fmt[7];
    if (n == 0) {
        uint32_t lo = uac_read24le(&fmt[8]);
        uint32_t hi = uac_read24le(&fmt[11]);
        return rate >= lo && rate <= hi;
    }
    uint8_t i;
    for (i = 0; i < n; i++) {
        if (uac_read24le(&fmt[8 + 3 * i]) == rate)
            return 1;
    }
    return 0;
}

static uint32_t uac_samples_per_frame(uint32_t rate)
{
    return rate / 1000;
}

static void uac_compute_frame_bytes(void)
{
    uint32_t spf = uac_samples_per_frame(uhost.uac_sample_rate);
    uint32_t bytes_per_sample = (uhost.uac_bit_depth + 7u) / 8u;
    uac_frame_bytes = spf * uhost.uac_channels * bytes_per_sample;
}

/* ---- Descriptor parsing ---- */

/* Score a (rate, depth) pair against Kconfig preferences.
 * Higher = better.  0 = not enabled in Kconfig. */
static int uac_mode_score(uint32_t rate, uint8_t depth)
{
    /* Priority: native render rate (44100) first, then higher bit depth */
#ifdef CONFIG_USB_AUDIO_HOST_44K_16
    if (rate == 44100 && depth == 16) return 100;
#endif
#ifdef CONFIG_USB_AUDIO_HOST_48K_24
    if (rate == 48000 && depth == 24) return 90;
#endif
#ifdef CONFIG_USB_AUDIO_HOST_48K_32
    if (rate == 48000 && depth == 32) return 80;
#endif
#ifdef CONFIG_USB_AUDIO_HOST_96K_24
    if (rate == 96000 && depth == 24) return 70;
#endif
    return 0;
}

static int usb_audio_host_parse_config(uint8_t addr,
                                       const usb_config_desc_t *cfg,
                                       uint16_t total)
{
    uint32_t pos = cfg->bLength;
    uint8_t ac_found = 0;
    uint8_t in_as = 0, as_iface = 0, as_alt = 0;
    uint8_t iso_ep = 0;
    uint16_t iso_maxp = 0;

    /* Best candidate found so far */
    int best_score = 0;
    uint8_t best_alt = 0, best_ep = 0, best_iface = 0;
    uint16_t best_maxp = 0;
    uint8_t best_channels = 0, best_depth = 0;
    uint32_t best_rate = 0;

    /* Current alt's format info (from FORMAT_TYPE descriptor) */
    const uint8_t *cur_fmt = 0;
    uint8_t cur_channels = 0, cur_depth = 0;

    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;

        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            usb_interface_desc_t *ifc = (usb_interface_desc_t *)&usb_enum_buf[pos];
            if (ifc->bInterfaceClass == USB_CLASS_AUDIO) {
                if (ifc->bInterfaceSubClass == UAC_SUBCLASS_CONTROL) {
                    ac_found = 1;
                } else if (ifc->bInterfaceSubClass == UAC_SUBCLASS_STREAMING &&
                           ifc->bAlternateSetting >= 1) {
                    in_as = 1;
                    as_iface = ifc->bInterfaceNumber;
                    as_alt = ifc->bAlternateSetting;
                    iso_ep = 0;
                    iso_maxp = 0;
                    cur_fmt = 0;
                    cur_channels = 0;
                    cur_depth = 0;
                } else {
                    in_as = 0;
                }
            } else {
                in_as = 0;
            }
        }

        if (in_as && dtype == UAC_DT_CS_INTERFACE && dlen >= 8) {
            uint8_t subtype = usb_enum_buf[pos + 2];
            if (subtype == UAC_AS_FORMAT_TYPE) {
                cur_fmt = &usb_enum_buf[pos];
                cur_channels = usb_enum_buf[pos + 4];
                cur_depth = usb_enum_buf[pos + 6];
            }
        }

        if (in_as && dtype == USB_DT_ENDPOINT && dlen >= 7) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&usb_enum_buf[pos];
            /* ISO OUT endpoint */
            if ((ep->bmAttributes & 0x03) == 0x01 &&
                !(ep->bEndpointAddress & USB_EP_DIR_IN)) {
                iso_ep = ep->bEndpointAddress & 0x0F;
                iso_maxp = ep->wMaxPacketSize & 0x07FFu;
            }
        }

        /* When we have a complete alt setting (format + endpoint), score it */
        if (in_as && iso_ep && cur_fmt && cur_channels >= 2) {
            static const uint32_t try_rates[] = { 44100, 48000, 96000 };
            uint32_t ri;
            for (ri = 0; ri < 3; ri++) {
                uint32_t rate = try_rates[ri];
                if (!uac_rate_supported(cur_fmt, rate)) continue;
                int sc = uac_mode_score(rate, cur_depth);
                if (sc == 0) {
                    /* Fallback: accept any rate at 16-bit if nothing else matched */
                    if (best_score == 0 && cur_depth >= 16)
                        sc = 1;
                    else
                        continue;
                }
                if (sc > best_score) {
                    best_score = sc;
                    best_alt = as_alt;
                    best_ep = iso_ep;
                    best_iface = as_iface;
                    best_maxp = iso_maxp;
                    best_channels = cur_channels;
                    best_depth = cur_depth;
                    best_rate = rate;
                }
            }
            iso_ep = 0;
        }

        pos += dlen;
    }

    if (!ac_found || !best_ep) return 0;

    uhost.uac_ep_out = best_ep;
    uhost.uac_max_packet = best_maxp;
    uhost.uac_alt = best_alt;
    uhost.uac_if_streaming = best_iface;
    uhost.uac_addr = addr;
    uhost.uac_ep0_mps = uhost.ep0_mps;
    uhost.uac_channels = best_channels > 2 ? 2 : best_channels;
    uhost.uac_bit_depth = best_depth;
    uhost.uac_sample_rate = best_rate;

    modd_log_int("uac: rate=", (int32_t)best_rate);
    modd_log_int("uac: depth=", best_depth);
    modd_log_int("uac: ch=", best_channels);
    modd_log_int("uac: alt=", best_alt);
    modd_log_int("uac: ep=", best_ep);
    modd_log_int("uac: maxp=", best_maxp);
    return 1;
}

/* ---- Attach ---- */

static int usb_audio_host_attach(void)
{
    int r;
    uac_select();

    /* SET_CONFIGURATION */
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_CONFIG, uac_cfg_value, 0, 0, 0);
    if (r < 0) { modd_log_int("uac: set cfg fail ", r); return r; }
    usb_settle_ms(50);

    /* SET_INTERFACE: activate the streaming alt setting */
    r = usb_control_msg(0x01, 0x0B, uhost.uac_alt, uhost.uac_if_streaming, 0, 0);
    if (r < 0) { modd_log_int("uac: set iface fail ", r); return r; }
    usb_settle_ms(10);

    /* SET_CUR Sampling Frequency on the ISO endpoint */
    {
        uint8_t freq[3];
        freq[0] = (uint8_t)(uhost.uac_sample_rate & 0xFF);
        freq[1] = (uint8_t)((uhost.uac_sample_rate >> 8) & 0xFF);
        freq[2] = (uint8_t)((uhost.uac_sample_rate >> 16) & 0xFF);
        r = usb_control_msg(0x22, UAC_SET_CUR,
                            (UAC_SAMPLING_FREQ_CTL << 8),
                            uhost.uac_ep_out, freq, 3);
        if (r < 0) modd_log_int("uac: set freq fail ", r);
        /* Some DACs ignore SET_CUR freq; not fatal */
    }

    /* Configure SIE EP3 for isochronous TX */
    fm1_usb_ep_txbuf(USB_AUDIO_HOST_EP, uac_dma_tx);
    usb_host_sie_wr(SIE_INDEX, USB_AUDIO_HOST_EP);
    usb_host_sie_wr(SIE_TXCSR1, 0);
    usb_host_sie_wr(SIE_TXCSR1, 0x48u);
    usb_host_sie_wr(SIE_TXCSR2, 0x40u);
    usb_host_sie_wr(SIE_TXTYPE, (uint32_t)(USB_ISO_FS | uhost.uac_ep_out));
    usb_host_sie_wr(SIE_TXINTERVAL, 1);
    fm1_usb_ep_enable((1u << USB_BULK_HOST_EP) | (1u << USB_AUDIO_HOST_EP));

    uac_compute_frame_bytes();
    uac_ring_wr = 0;
    uac_ring_rd = 0;
    uac_frac_accum = 0;

    uhost.state = UHOST_UAC_READY;
    modd_log("uac: attached");
    toast_status("USB DAC on");
    return 0;
}

/* ---- Detach ---- */

static void usb_audio_host_detach(void)
{
    if (uhost.state != UHOST_UAC_READY) return;
    uac_ring_wr = 0;
    uac_ring_rd = 0;
    uac_src_phase = 0;
    uac_src_prev[0] = 0;
    uac_src_prev[1] = 0;
    uhost.uac_ep_out = 0;
    modd_log("uac: detached");
    toast_status("USB DAC off");
}

/* ---- Init ---- */

static void usb_audio_host_init(void)
{
    uac_ring_wr = 0;
    uac_ring_rd = 0;
    uac_frame_bytes = 0;
    uac_frac_accum = 0;
    uac_src_phase = 0;
    uac_src_prev[0] = 0;
    uac_src_prev[1] = 0;
    uac_wanted = sys_settings.usb_dac_mode > 0;
}

/* ---- ISR feed hook ---- */

/* Pack one stereo frame (int32 samples) into the ring at wr; returns new wr. */
static uint32_t uac_pack_frame(uint32_t wr, int32_t l, int32_t r, uint32_t bps, uint32_t ch)
{
    if (bps == 2) {
            int16_t sl = (int16_t)(l >> 8);
            int16_t sr = (int16_t)(r >> 8);
            uac_ring[wr & (UAC_RING_SIZE - 1)] = (uint8_t)(sl & 0xFF);
            uac_ring[(wr + 1) & (UAC_RING_SIZE - 1)] = (uint8_t)((sl >> 8) & 0xFF);
            if (ch >= 2) {
                uac_ring[(wr + 2) & (UAC_RING_SIZE - 1)] = (uint8_t)(sr & 0xFF);
                uac_ring[(wr + 3) & (UAC_RING_SIZE - 1)] = (uint8_t)((sr >> 8) & 0xFF);
            }
            wr += bps * ch;
        } else if (bps == 3) {
            uac_ring[wr & (UAC_RING_SIZE - 1)] = (uint8_t)(l & 0xFF);
            uac_ring[(wr + 1) & (UAC_RING_SIZE - 1)] = (uint8_t)((l >> 8) & 0xFF);
            uac_ring[(wr + 2) & (UAC_RING_SIZE - 1)] = (uint8_t)((l >> 16) & 0xFF);
            if (ch >= 2) {
                uac_ring[(wr + 3) & (UAC_RING_SIZE - 1)] = (uint8_t)(r & 0xFF);
                uac_ring[(wr + 4) & (UAC_RING_SIZE - 1)] = (uint8_t)((r >> 8) & 0xFF);
                uac_ring[(wr + 5) & (UAC_RING_SIZE - 1)] = (uint8_t)((r >> 16) & 0xFF);
            }
            wr += bps * ch;
        } else if (bps == 4) {
            uac_ring[wr & (UAC_RING_SIZE - 1)] = (uint8_t)(l & 0xFF);
            uac_ring[(wr + 1) & (UAC_RING_SIZE - 1)] = (uint8_t)((l >> 8) & 0xFF);
            uac_ring[(wr + 2) & (UAC_RING_SIZE - 1)] = (uint8_t)((l >> 16) & 0xFF);
            uac_ring[(wr + 3) & (UAC_RING_SIZE - 1)] = (uint8_t)((l >> 24) & 0xFF);
            if (ch >= 2) {
                uac_ring[(wr + 4) & (UAC_RING_SIZE - 1)] = (uint8_t)(r & 0xFF);
                uac_ring[(wr + 5) & (UAC_RING_SIZE - 1)] = (uint8_t)((r >> 8) & 0xFF);
                uac_ring[(wr + 6) & (UAC_RING_SIZE - 1)] = (uint8_t)((r >> 16) & 0xFF);
                uac_ring[(wr + 7) & (UAC_RING_SIZE - 1)] = (uint8_t)((r >> 24) & 0xFF);
            }
            wr += bps * ch;
        }
    return wr;
}

static void usb_audio_host_feed(const int32_t *o, uint32_t frames)
{
    if (uhost.state != UHOST_UAC_READY || !uac_frame_bytes) return;
    if (uac_ring_frozen) return;

    uint32_t wr = uac_ring_wr;
    uint32_t bps = (uhost.uac_bit_depth + 7u) / 8u;
    uint32_t ch = uhost.uac_channels;
    uint32_t i;

    if (uhost.uac_sample_rate == 44100) {
        for (i = 0; i < frames; i++)
            wr = uac_pack_frame(wr, o[2u * i], o[2u * i + 1], bps, ch);
    } else {
        /* Linear-interpolation SRC 44.1 kHz -> 48 kHz (integer, Q16 phase).
         * uac_src_prev[] holds the last sample of the previous buffer so we
         * can interpolate across the boundary without a glitch. */
        uint32_t end = frames << 16;
        while (uac_src_phase < end) {
            uint32_t idx = uac_src_phase >> 16;
            uint32_t frac = uac_src_phase & 0xFFFFu;
            int32_t s0l, s0r, s1l, s1r;
            if (idx == 0 && uac_src_phase < 0x10000u) {
                s0l = uac_src_prev[0]; s0r = uac_src_prev[1];
                s1l = o[0];            s1r = o[1];
            } else {
                uint32_t next = (idx + 1u < frames) ? idx + 1u : idx;
                s0l = o[2u * idx];       s0r = o[2u * idx + 1u];
                s1l = o[2u * next];      s1r = o[2u * next + 1u];
            }
            int32_t l = s0l + (int32_t)(((int64_t)(s1l - s0l) * frac) >> 16);
            int32_t r = s0r + (int32_t)(((int64_t)(s1r - s0r) * frac) >> 16);
            wr = uac_pack_frame(wr, l, r, bps, ch);
            uac_src_phase += UAC_SRC_STEP;
        }
        uac_src_prev[0] = o[2u * (frames - 1u)];
        uac_src_prev[1] = o[2u * (frames - 1u) + 1u];
        uac_src_phase -= end;
    }
    uac_ring_wr = wr;
}

/* ---- Main-loop poll: dequeue one frame, send via ISO OUT ---- */

static void usb_audio_host_poll(void)
{
    if (uac_test_mode) return;
    uac_wanted = sys_settings.usb_dac_mode > 0;
    if (uhost.state == UHOST_UAC_READY && !uac_wanted) {
        usb_audio_host_detach();
        return;
    }
    if (uhost.state != UHOST_UAC_READY || !uac_frame_bytes) return;

    uint32_t wr = uac_ring_wr;
    uint32_t rd = uac_ring_rd;
    uint32_t avail = wr - rd;

    /* For 44.1 kHz: alternate 44 and 45 samples per frame */
    uint32_t send_bytes = uac_frame_bytes;
    if (uhost.uac_sample_rate == 44100) {
        uint32_t bps = (uhost.uac_bit_depth + 7u) / 8u;
        uint32_t base_samples = 44;
        uac_frac_accum += 1;
        if (uac_frac_accum >= 10) {
            uac_frac_accum = 0;
            base_samples = 45;
        }
        send_bytes = base_samples * uhost.uac_channels * bps;
    }

    if (avail < send_bytes) return;

    uac_select();

    /* Linearize from ring into dma buffer */
    uint32_t i;
    for (i = 0; i < send_bytes; i++)
        uac_dma_tx[i] = uac_ring[(rd + i) & (UAC_RING_SIZE - 1)];

    usb_iso_out(USB_AUDIO_HOST_EP, uac_dma_tx, uhost.uac_max_packet,
                uhost.uac_ep_out, uac_dma_tx, send_bytes);
    uac_ring_rd = rd + send_bytes;
}

#endif /* CONFIG_USB_AUDIO_HOST */
