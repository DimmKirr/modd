#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Read debug logs from a running FM-1 over USB-MIDI SysEx.

Requires python-rtmidi:
    uv run --with python-rtmidi tools/fm1log.py
    uv run --with python-rtmidi tools/fm1log.py --follow
    uv run --with python-rtmidi tools/fm1log.py --hello
    uv run --with python-rtmidi tools/fm1log.py --wifi
    uv run --with python-rtmidi tools/fm1log.py --bytes 1024   # only the last 1 KB

A plain dump returns everything the device ring still holds (MODD_DBG_LOG_SIZE, 4 KB):
the device clamps any older read position to the start of the ring.

Uses the fm1-dock SysEx protocol (D_HELLO=0x01, D_LOG=0x20) via hbdeploy.py."""
import argparse
import datetime
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__file__))
import hbdeploy

D_WIFI = 0x25
D_WIFI_BSS = 0x26
D_PROBES = 0x27
D_WL0 = 0x29      # CONFIG_WIFI_DEBUG_WLCTL0_PROBE: run the WL_CTL0 release probe once
D_SWEEP = 0x28    # read consecutive words from a base (SoC, WL control, MAC, DMA windows)
D_USB = 0x2A      # USB host state dump (CONFIG_USB_HOST + CONFIG_DEBUG_DAP)
SWEEP_MAX = 60    # words per reply: 8-byte header + 60 words = 248 bytes

WIFI_STATES = [
    "OFF", "IDLE", "SCANNING", "AUTH", "ASSOC", "WPA", "CONNECTED"
]

WEATHER_STATES = [
    "IDLE", "GEO_FETCH", "GEO_WAIT", "WX_FETCH", "WX_WAIT", "DONE"
]


def print_hello(p):
    h = hbdeploy.hello(p)
    print(f"proto={h['proto']}  crash_pc={h['crash_pc']:#010x}  crash_mod={h['crash_module']}")
    print(f"worst={h['worst']}  overruns={h['overruns']}  log_w={h['log_w']}")
    print(f"free={h['free']}  largest={h['largest']}  max_file={h['max_file']}")
    return h


def _ip_str(addr):
    if addr == 0:
        return "-"
    return f"{addr & 0xFF}.{(addr >> 8) & 0xFF}.{(addr >> 16) & 0xFF}.{(addr >> 24) & 0xFF}"


def _mac_str(b):
    return ":".join(f"{x:02X}" for x in b)


def print_wifi(p):
    d = p.ask(D_WIFI)
    if not d or d[0] != 0:
        print("wifi: query failed (firmware may not have CONFIG_WIFI + CONFIG_DEBUG_DAP)")
        return

    sta = d[1]
    sdio_init = d[2]
    sdio_err = d[3]
    mac = d[4:10]
    ip = struct.unpack_from("<I", d, 10)[0]
    gw = struct.unpack_from("<I", d, 14)[0]
    dns = struct.unpack_from("<I", d, 18)[0]
    scan_n = d[22]
    bss_n = d[23]
    wx_state = d[24]
    wx_valid = d[25]
    wx_temp = struct.unpack_from("<i", d, 26)[0]
    wx_hum = d[30]
    wx_wmo = d[31]
    wx_wind = struct.unpack_from("<I", d, 32)[0]
    wx_ms = struct.unpack_from("<I", d, 36)[0]

    sta_name = WIFI_STATES[sta] if sta < len(WIFI_STATES) else f"?{sta}"
    wx_name = WEATHER_STATES[wx_state] if wx_state < len(WEATHER_STATES) else f"?{wx_state}"

    print(f"sta:     {sta_name}")
    print(f"mac:     {_mac_str(mac)}")
    print(f"sdio:    init={sdio_init}  errors={sdio_err}")
    print(f"ip:      {_ip_str(ip)}")
    print(f"gw:      {_ip_str(gw)}")
    print(f"dns:     {_ip_str(dns)}")
    print(f"scan:    {scan_n} results, {bss_n} BSS entries")
    print(f"weather: {wx_name}" + (f"  temp={wx_temp/10:.1f}C  hum={wx_hum}%  wmo={wx_wmo}  wind={wx_wind/10:.1f}km/h" if wx_valid else ""))
    if wx_valid:
        print(f"         updated {wx_ms}ms ago" if wx_ms else "         just updated")

    d2 = p.ask(D_WIFI_BSS)
    if d2 and d2[0] == 0 and len(d2) > 1:
        off = 1
        idx = 0
        print("--- BSS table ---")
        while off + 10 <= len(d2):
            bssid = d2[off:off+6]
            off += 6
            ch = d2[off]; off += 1
            rssi = d2[off] if d2[off] < 128 else d2[off] - 256; off += 1
            has_rsn = d2[off]; off += 1
            slen = d2[off]; off += 1
            if off + slen > len(d2):
                break
            ssid = bytes(d2[off:off+slen]).decode("latin-1", errors="replace")
            off += slen
            lock = "WPA" if has_rsn else "   "
            print(f"  [{idx}] {_mac_str(bssid)}  ch={ch:2d}  rssi={rssi:4d}  {lock}  {ssid}")
            idx += 1
        if idx == 0:
            print("  (empty)")


UHOST_STATES = ["DISCONNECTED", "ATTACHED", "ENUMERATED", "MSC_READY"]
OTG_MODES = ["DEVICE", "HOST"]
USB_CLASSES = {
    0x00: "Composite", 0x01: "Audio", 0x02: "CDC", 0x03: "HID",
    0x05: "Physical", 0x06: "Image", 0x07: "Printer", 0x08: "MSC",
    0x09: "Hub", 0x0A: "CDC-Data", 0x0B: "SmartCard", 0x0D: "ContentSec",
    0x0E: "Video", 0x0F: "Healthcare", 0xDC: "Diagnostic", 0xE0: "Wireless",
    0xEF: "Misc", 0xFE: "App-Specific", 0xFF: "Vendor-Specific",
}


def print_usb(p):
    d = p.ask(D_USB)
    if not d or d[0] != 0:
        print("usb: query failed (firmware may not have CONFIG_USB_HOST + CONFIG_DEBUG_DAP)")
        return
    if len(d) < 33:
        print(f"usb: short reply ({len(d)} bytes, expected 33)")
        return

    state = d[1]
    last_err = d[2] if d[2] < 128 else d[2] - 256
    ep_in = d[3]
    ep_out = d[4]
    iface = d[5]
    max_lun = d[6]
    max_packet = d[7] | (d[8] << 8)
    vid = d[9] | (d[10] << 8)
    pid = d[11] | (d[12] << 8)
    dev_class = d[13]
    capacity = struct.unpack_from("<I", d, 14)[0]
    sector_size = struct.unpack_from("<I", d, 18)[0]
    sie_faddr = d[22]
    sie_power = d[23]
    sie_devctl = d[24]
    sie_intrusb = d[25]
    usb_line = d[26]
    otg_mode = d[27]
    con0_hi = d[28] | (d[29] << 8)
    sie_csr0 = d[30]
    sie_txcsr1 = d[31]
    sie_rxcsr1 = d[32]

    state_name = UHOST_STATES[state] if state < len(UHOST_STATES) else ("ERROR" if state == 0xFF else f"?{state}")
    otg_name = OTG_MODES[otg_mode] if otg_mode < len(OTG_MODES) else f"?{otg_mode}"
    cls_name = USB_CLASSES.get(dev_class, f"0x{dev_class:02X}")

    vbus_bits = (sie_devctl >> 3) & 3
    vbus_names = {0: "below SessionEnd", 1: "above SessionEnd", 2: "above AValid", 3: "above VBusValid"}

    print(f"=== USB Host State ===")
    print(f"otg:       {otg_name}")
    print(f"state:     {state_name}" + (f"  (last_err={last_err})" if last_err else ""))
    print(f"device:    VID={vid:04X} PID={pid:04X}  class={cls_name}")
    print(f"msc:       iface={iface}  lun={max_lun}  maxpkt={max_packet}")
    print(f"endpoints: IN={ep_in}  OUT={ep_out}")
    print(f"capacity:  {capacity} sectors x {sector_size}B" + (f" ({capacity * sector_size // (1024*1024)} MB)" if capacity else ""))
    print(f"=== SIE Registers ===")
    print(f"FADDR:     {sie_faddr}")
    print(f"POWER:     0x{sie_power:02X}")
    print(f"DEVCTL:    0x{sie_devctl:02X}  FSDev={'Y' if sie_devctl & 0x40 else 'N'}  LSDev={'Y' if sie_devctl & 0x20 else 'N'}  VBUS={vbus_names.get(vbus_bits, '?')}")
    print(f"INTRUSB:   0x{sie_intrusb:02X}")
    print(f"CSR0:      0x{sie_csr0:02X}")
    print(f"TXCSR1:    0x{sie_txcsr1:02X}")
    print(f"RXCSR1:    0x{sie_rxcsr1:02X}")
    print(f"=== PHY ===")
    print(f"line:      {usb_line}  CON0[31:16]=0x{con0_hi:04X}")

    if len(d) < 51:
        return
    lun, csw_status, sense_key, asc, ascq, inq_pdt, inq_rmb, tur_tries = d[33:41]
    ctl_rt, ctl_rq, ctl_stage, ctl_r, ctl_csr0, ctl_faddr = d[41:47]
    ctl_r = ctl_r - 256 if ctl_r >= 128 else ctl_r
    ndev, nmsc, mounted, dead = d[47:51]
    ctl_stages = {0: "setup", 1: "data", 2: "status"}
    ctl_errs = {-1: "error (no answer)", -2: "stall", -3: "timeout"}

    print(f"=== SCSI (last LUN tried) ===")
    print(f"lun:       {lun}  TUR tries={tur_tries}  last CSW status={csw_status}"
          + {0: " (passed)", 1: " (failed)", 2: " (phase error)"}.get(csw_status, ""))
    print(f"inquiry:   pdt=0x{inq_pdt & 0x1F:02X}{' (direct-access)' if (inq_pdt & 0x1F) == 0 else ''}"
          f"  removable={'Y' if inq_rmb & 0x80 else 'N'}")
    print(f"sense:     key=0x{sense_key:02X} asc=0x{asc:02X} ascq=0x{ascq:02X}  {sense_text(sense_key, asc)}")
    print(f"=== last control transfer failure ===")
    if ctl_rt or ctl_rq or ctl_r:
        print(f"request:   bmRequestType=0x{ctl_rt:02X} bRequest=0x{ctl_rq:02X} ({usb_request_name(ctl_rt, ctl_rq)})")
        print(f"failed at: {ctl_stages.get(ctl_stage, ctl_stage)} stage, r={ctl_r} ({ctl_errs.get(ctl_r, '?')})"
              f"  CSR0=0x{ctl_csr0:02X} FADDR={ctl_faddr}")
    else:
        print("none")
    print(f"=== hub discovery ===")
    print(f"devices:   {ndev}  msc candidates: {nmsc}  mounted: "
          + ("none" if mounted == 0xFF else f"#{mounted}")
          + (f"  abandoned hub addr={dead}" if dead else ""))
    pos = 51
    for _ in range(ndev):
        if pos + 4 > len(d):
            break
        addr, hub, port, cls = d[pos:pos + 4]
        via = f"via hub {hub} port {port}" if hub else "root"
        print(f"  addr {addr:3}  {USB_CLASSES.get(cls, f'0x{cls:02X}'):10}  {via}")
        pos += 4
    for i in range(nmsc):
        if pos + 9 > len(d):
            break
        addr, hub, port, ep_in, ep_out = d[pos:pos + 5]
        vid = d[pos + 5] | (d[pos + 6] << 8)
        pid = d[pos + 7] | (d[pos + 8] << 8)
        mark = " <- mounted" if i == mounted else ""
        print(f"  msc #{i}: addr {addr} via hub {hub} port {port}  VID={vid:04X} PID={pid:04X}"
              f"  ep in={ep_in} out={ep_out}{mark}")
        pos += 9
    if pos + 6 <= len(d):
        addr, cfg, iface, alt, ep_in, ep_out = d[pos:pos + 6]
        if addr:
            print(f"usb-net:   addr {addr}  cfg {cfg} iface {iface} alt {alt}"
                  f"  ep in={ep_in & 0x0F} out={ep_out & 0x0F}")
        else:
            print("usb-net:   none")


SENSE_KEYS = {
    0x00: "no sense", 0x01: "recovered", 0x02: "not ready", 0x03: "medium error",
    0x04: "hardware error", 0x05: "illegal request", 0x06: "unit attention",
    0x07: "data protect", 0x0B: "aborted command", 0xFF: "REQUEST_SENSE failed",
}
SENSE_ASC = {
    (0x02, 0x04): "becoming ready / initialization required",
    (0x02, 0x3A): "medium not present (empty slot)",
    (0x06, 0x28): "medium changed",
    (0x06, 0x29): "power-on or reset occurred",
    (0x05, 0x20): "invalid command opcode",
    (0x05, 0x24): "invalid field in CDB",
    (0x05, 0x25): "LUN not supported",
}


def sense_text(key, asc):
    k = SENSE_KEYS.get(key, "?")
    return SENSE_ASC.get((key, asc), k)


def usb_request_name(rt, rq):
    if rt & 0x60 == 0x20:
        return {0xFE: "GET_MAX_LUN", 0xFF: "BOT reset"}.get(rq, "class request")

    std = {0x00: "GET_STATUS", 0x01: "CLEAR_FEATURE", 0x03: "SET_FEATURE",
           0x05: "SET_ADDRESS", 0x06: "GET_DESCRIPTOR", 0x09: "SET_CONFIGURATION",
           0x0B: "SET_INTERFACE"}
    name = std.get(rq, "?")
    if rt & 0x1F == 0x03:
        name += " (hub port)"
    elif rt & 0x1F == 0x02:
        name += " (endpoint)"
    return name


PHASE_NAMES = ["pre-clock", "post-clock", "post-reset"]

RT2800_NAMES = {
    # Legacy addresses (open bus on real HW, kept for reference)
    0x00200: "INT_SOURCE_CSR(legacy)",
    0x00204: "INT_MASK_CSR(legacy)",
    0x00208: "WPDMA_GLO_CFG(legacy)",
    0x0020C: "WPDMA_RST_IDX(legacy)",
    0x00210: "DELAY_INT_CFG(legacy)",
    0x00230: "TX_BASE_PTR0(legacy)",
    0x00234: "TX_MAX_CNT0(legacy)",
    0x00290: "RX_BASE_PTR(legacy)",
    0x00294: "RX_MAX_CNT(legacy)",
    0x00298: "RX_CRX_IDX(legacy)",
    0x0029C: "RX_DRX_IDX(legacy)",
    # DMA candidate at 0x40000 base (RT2800 offsets)
    0x40200: "INT_SOURCE_CSR?",
    0x40204: "INT_MASK_CSR?",
    0x40208: "WPDMA_GLO_CFG?",
    0x4020C: "WPDMA_RST_IDX?",
    0x40210: "DELAY_INT_CFG?",
    0x40230: "TX_BASE_PTR0?",
    0x40234: "TX_MAX_CNT0?",
    0x40238: "TX_CTX_IDX0?",
    0x4023C: "TX_DTX_IDX0?",
    0x40240: "TX_BASE_PTR1?",
    0x40244: "TX_MAX_CNT1?",
    0x40248: "TX_CTX_IDX1?",
    0x4024C: "TX_DTX_IDX1?",
    0x40260: "TX_BASE_PTR2?",
    0x40264: "TX_MAX_CNT2?",
    0x40268: "TX_CTX_IDX2?",
    0x4026C: "TX_DTX_IDX2?",
    0x40280: "TX_BASE_PTR4?",
    0x40284: "TX_MAX_CNT4?",
    0x40288: "TX_CTX_IDX4?",
    0x4028C: "TX_DTX_IDX4?",
    0x40290: "RX_BASE_PTR?",
    0x40294: "RX_MAX_CNT?",
    0x40298: "RX_CRX_IDX?",
    0x4029C: "RX_DRX_IDX?",
    # MAC registers (0x30000 base)
    0x31000: "CHIP_VERSION",
    0x31004: "MAC_SYS_CTRL",
    0x3101C: "BBP_CSR_CFG",
    0x30300: "MAC_ADDR_DW0",
    0x30304: "MAC_ADDR_DW1",
    0x31100: "XIFS_TIME_CFG",
    0x31104: "BKOFF_SLOT_CFG",
    0x31330: "TX_SW_CFG0",
    # GPIO (not WiFi)
    0x50000: "GPIO_PA_OUT",
    0x5000C: "GPIO_PA_DIE",
}


def print_probes(p):
    d = p.ask(D_PROBES)
    if not d or d[0] != 0:
        print("probes: query failed (firmware may not have CONFIG_WIFI_PROBES)")
        return

    count = d[1]
    print(f"=== WiFi Hardware Probe Results ({count} readings) ===")
    print(f"{'Phase':<12} {'Address':>8}  {'Value':>10}  {'RT2800 Name':<20}  Tag")
    print("-" * 72)

    off = 2
    for i in range(count):
        if off + 10 > len(d):
            break
        addr = (d[off] << 24) | (d[off+1] << 16) | (d[off+2] << 8) | d[off+3]
        off += 4
        val = (d[off] << 24) | (d[off+1] << 16) | (d[off+2] << 8) | d[off+3]
        off += 4
        phase = d[off]; off += 1
        rw = chr(d[off]); off += 1

        phase_name = PHASE_NAMES[phase] if phase < len(PHASE_NAMES) else f"?{phase}"
        rt_name = RT2800_NAMES.get(addr, "")
        tag = "[M:hw]"
        if addr >= 0x50000:
            tag = "[M:hw] ERRATA"

        print(f"  {phase_name:<10} 0x{addr:05X}  0x{val:08X}  {rt_name:<20}  {tag}")

    if count == 0:
        print("  (no results - probes may not have run yet)")

    print()
    print("Legend: pre-clock = before WiFi clock enable")
    print("        post-clock = clocks on, MAC still in reset")
    print("        post-reset = MAC reset released, registers accessible")
    print()
    print("Copy these results into SPECSHEET.md with [M:hw] tags.")


def wl0_request(p, mode=0):
    """D_WL0 modes: 0 release WL_CTL0 and scan once; 2 retry the SoC writes that refused on a
    held block, then scan (12k). Mode 1 (warm-reboot variant) was removed in 12p."""
    d = p.ask(D_WL0, struct.pack("<I", mode))
    return bool(d) and d[0] == 0


def sweep_read(p, base, count):
    """Read `count` words from `base` in reply-sized chunks. Returns {addr: value}."""
    regs = {}
    addr = base & ~3
    end = addr + 4 * count
    while addr < end:
        n = min(SWEEP_MAX, (end - addr) // 4)
        d = p.ask(D_SWEEP, struct.pack("<IH", addr, n))
        if not d or d[0] != 0:
            raise RuntimeError(f"sweep refused at {addr:#x}: outside the allowed windows, "
                               "or MAC/DMA space before the bus gate")
        got = struct.unpack_from("<I", d, 4)[0]
        for i, v in enumerate(struct.unpack_from(f"<{n}I", d, 8)):
            regs[got + 4 * i] = v
        addr += 4 * n
    return regs


def sweep_text(regs):
    """`0xADDR = 0xVALUE` lines, the fm1-base /status and /sweep format, for wifi_sweep_diff.py."""
    return "".join(f"0x{a:08x} = 0x{v:08x}\n" for a, v in sorted(regs.items()))


def _hostts():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def _read_from(p, pos):
    """Read log from byte position `pos` forward. Returns (text, new_pos)."""
    out = bytearray()
    frm = pos
    while True:
        d = p.ask(hbdeploy.LOG, struct.pack("<I", frm))
        log_w, at = struct.unpack_from("<II", d, 4)
        chunk = d[12:]
        if not chunk:
            return out.decode("latin-1"), max(frm, log_w)
        out += chunk
        frm = at + len(chunk)
        if frm >= log_w:
            return out.decode("latin-1"), frm


def _emit(text, host_ts):
    for line in text.splitlines(keepends=True):
        if host_ts:
            sys.stdout.write(f"[{_hostts()}] {line}")
        else:
            sys.stdout.write(line)
    sys.stdout.flush()


def dump_log(p, host_ts=True, tail=0):
    """Print what the ring holds (all of it, or the last `tail` bytes); return the read position."""
    h = hbdeploy.hello(p)
    pos = max(0, h["log_w"] - tail) if tail > 0 else 0
    text, pos = _read_from(p, pos)
    if text:
        _emit(text, host_ts)
    return pos


def follow_log(p, pos, interval=0.2, host_ts=True, stop=None, timeout=None):
    """Poll for new log text from `pos`. Returns 'stop' once `stop` appears in new text,
    'timeout' after `timeout` seconds, 'lost' when the device stops answering (a halted
    unit); with neither limit it follows until interrupted."""
    t0 = time.monotonic()
    carry = ""
    while True:
        time.sleep(interval)
        try:
            text, pos = _read_from(p, pos)
        except SystemExit:
            _emit("[fm1log] device stopped answering\n", host_ts)
            return "lost"
        if text:
            _emit(text, host_ts)
            if stop and stop in carry + text:
                return "stop"
            carry = text[-len(stop):] if stop else ""
        if timeout is not None and time.monotonic() - t0 >= timeout:
            _emit("[fm1log] timeout\n", host_ts)
            return "timeout"


def read_log(p, follow=False, interval=0.2, host_ts=True, tail=0, timeout=None):
    """Print the device log. `tail` 0 reads everything the ring still holds;
    N > 0 reads only the last N bytes. With `follow`, keep polling, for
    `timeout` seconds when given."""
    pos = dump_log(p, host_ts=host_ts, tail=tail)
    if follow:
        follow_log(p, pos, interval=interval, host_ts=host_ts, timeout=timeout)


def build_parser():
    ap = argparse.ArgumentParser(description="Read FM-1 debug logs over USB-MIDI")
    ap.add_argument("--port", help="MIDI port name substring (default: auto-detect)")
    ap.add_argument("--hello", action="store_true", help="Print device status and exit")
    ap.add_argument("--usb", action="store_true", help="Print USB host state and exit")
    ap.add_argument("--wifi", action="store_true", help="Print WiFi subsystem state and exit")
    ap.add_argument("--probes", action="store_true", help="Print WiFi hardware probe results and exit")
    ap.add_argument("--follow", "-f", action="store_true", help="Poll for new log entries continuously")
    ap.add_argument("--for", dest="for_seconds", type=float, metavar="SECONDS",
                    help="With --follow: stop after this many seconds")
    ap.add_argument("--interval", type=float, default=0.2, help="Poll interval in seconds (default: 0.2)")
    ap.add_argument("--no-ts", action="store_true", help="Disable host-side timestamps")
    ap.add_argument("--wl0", action="store_true",
                    help="Run the WL_CTL0 release probe (CONFIG_WIFI_DEBUG_WLCTL0_PROBE) and follow until its scan ends")
    ap.add_argument("--retry", action="store_true",
                    help="With --wl0: retry the writes refused on a held block (12k) instead of the release probe")
    ap.add_argument("--sweep", nargs=2, type=lambda s: int(s, 0), metavar=("BASE", "COUNT"),
                    help="Dump COUNT words from BASE as `addr = value` lines (task debug:sweep)")
    ap.add_argument("--timeout", type=float, default=30,
                    help="--wl0: seconds to follow before giving up (default: 30)")
    ap.add_argument("--bytes", type=int, default=0,
                    help="Read only the last N bytes of the device ring (default: all it holds, 4 KB)")
    return ap


def main():
    args = build_parser().parse_args()

    def open_port():
        return hbdeploy.Port(name=args.port)

    p = open_port()

    if args.hello:
        print_hello(p)
        return

    if args.usb:
        print_usb(p)
        return

    if args.wifi:
        print_wifi(p)
        return

    if args.probes:
        print_probes(p)
        return

    if args.sweep:
        sys.stdout.write(sweep_text(sweep_read(p, args.sweep[0], args.sweep[1])))
        return

    if args.wl0:
        print_hello(p)
        print("---")
        pos = dump_log(p, host_ts=not args.no_ts, tail=args.bytes)
        if not wl0_request(p, mode=2 if args.retry else 0):
            print("wl0: request refused (firmware built without CONFIG_WIFI_DEBUG_WLCTL0_PROBE?)")
            return
        print("wl0: " + ("retry of the refused writes" if args.retry else "probe")
              + " requested; following until its scan finishes")
        why = follow_log(p, pos, interval=args.interval, host_ts=not args.no_ts,
                         stop="wifi: scan done n=", timeout=args.timeout)
        print(f"wl0: {why}")
        return

    if args.follow:
        h = print_hello(p)
        print("---")

    read_log(p, follow=args.follow, interval=args.interval, host_ts=not args.no_ts,
             tail=args.bytes, timeout=args.for_seconds)


if __name__ == "__main__":
    main()
