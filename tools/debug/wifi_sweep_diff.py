#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Diff two register sweeps: modd (`task debug:sweep`, .scratch/debug/sweep.log) against the
fm1-base oracle (`curl http://<ip>:8080/sweep?base=30000&n=2048`). Both sides use the same
`0xADDR = 0xVALUE` lines, so any text with such lines parses (the /status MMIO section too).

Usage: wifi_sweep_diff.py <modd.txt> <vendor.txt> [--all]
Prints differing and one-sided rows (all rows with --all). Exit code 1 if any differ.
"""
import re
import sys
from pathlib import Path

LINE = re.compile(r'0x([0-9A-Fa-f]{8})\s*=\s*0x([0-9A-Fa-f]{8})')

# RT2800 register names (public rt2800.h offsets + 0x30000) and the JieLi SoC WiFi block.
# Rows without a name are the interesting ones: nobody has looked at them.
NAMES = {
    0x00200: 'INT_SOURCE_CSR(legacy)',
    0x00204: 'INT_MASK_CSR(legacy)',
    0x00208: 'WPDMA_GLO_CFG(legacy)',
    0x0020C: 'WPDMA_RST_IDX(legacy)',
    0x00210: 'DELAY_INT_CFG(legacy)',
    0x00230: 'TX_BASE_PTR0(legacy)',
    0x00234: 'TX_MAX_CNT0(legacy)',
    0x00290: 'RX_BASE_PTR(legacy)',
    0x00294: 'RX_MAX_CNT(legacy)',
    0x00298: 'RX_CRX_IDX(legacy)',
    0x0029C: 'RX_DRX_IDX(legacy)',
    0x40200: 'INT_SOURCE_CSR?',
    0x40204: 'INT_MASK_CSR?',
    0x40208: 'WPDMA_GLO_CFG?',
    0x4020C: 'WPDMA_RST_IDX?',
    0x40210: 'DELAY_INT_CFG?',
    0x40230: 'TX_BASE_PTR0?',
    0x40234: 'TX_MAX_CNT0?',
    0x40238: 'TX_CTX_IDX0?',
    0x4023C: 'TX_DTX_IDX0?',
    0x40240: 'TX_BASE_PTR1?',
    0x40244: 'TX_MAX_CNT1?',
    0x40248: 'TX_CTX_IDX1?',
    0x4024C: 'TX_DTX_IDX1?',
    0x40260: 'TX_BASE_PTR2?',
    0x40264: 'TX_MAX_CNT2?',
    0x40268: 'TX_CTX_IDX2?',
    0x4026C: 'TX_DTX_IDX2?',
    0x40280: 'TX_BASE_PTR4?',
    0x40284: 'TX_MAX_CNT4?',
    0x40288: 'TX_CTX_IDX4?',
    0x4028C: 'TX_DTX_IDX4?',
    0x40290: 'RX_BASE_PTR?',
    0x40294: 'RX_MAX_CNT?',
    0x40298: 'RX_CRX_IDX?',
    0x4029C: 'RX_DRX_IDX?',
    0x31000: 'CHIP_VERSION',
    0x31004: 'MAC_SYS_CTRL',
    0x3101C: 'BBP_CSR_CFG',
    0x30300: 'MAC_ADDR_DW0',
    0x30304: 'MAC_ADDR_DW1',
    0x31100: 'XIFS_TIME_CFG',
    0x31104: 'BKOFF_SLOT_CFG',
    0x31330: 'TX_SW_CFG0',
    0x50000: 'GPIO_PA_OUT',
    0x5000C: 'GPIO_PA_DIE',
    0x10008: 'SYS_CLK1', 0x10010: 'SYS_CLK2',
    0x14000: 'WL_CTL0', 0x14008: 'WL_CTL8', 0x1400C: 'WL_ID', 0x14028: 'RFSPI_CTL', 0x1402C: 'RFSPI_DATA',
    0x14040: 'WL_CLK0', 0x14044: 'WL_CLK1', 0x14048: 'WL_CLK2', 0x1404C: 'WL_CLK3', 0x14050: 'WL_CLK4',
    0x14054: 'WL_CFG54', 0x14058: 'WL_CFG58', 0x1405C: 'WL_RST', 0x14060: 'WL_CFG60',
    0x14080: 'WL_CFG80', 0x14084: 'WL_CFG84', 0x14088: 'WL_CFG88', 0x14090: 'WL_CFG90',
    0x30308: 'MAC_MODE(0x308)', 0x3030C: 'PHY_MAC_RST(0x30C)', 0x30324: 'MAC_RX_PREP', 0x30328: 'MAC_IRQ_EN',
    0x30400: 'PBF_SYS_CTRL', 0x30408: 'PBF_CFG', 0x3040C: 'PBF_MAX_PCNT', 0x30438: 'TXRXQ_PCNT',
    0x30F00: 'WDMA_CFG0', 0x30F04: 'WDMA_CTL', 0x30F10: 'WDMA_TX_ADR', 0x30F14: 'WDMA_TX_LEN',
    0x30F18: 'WDMA_RX_ADR', 0x30F1C: 'WDMA_RX_LEN',
    0x31008: 'MAC_ADDR_DW0', 0x3100C: 'MAC_ADDR_DW1', 0x31010: 'MAC_BSSID_DW0', 0x31014: 'MAC_BSSID_DW1',
    0x31018: 'MAX_LEN_CFG', 0x31114: 'BCN_TIME_CFG', 0x31130: 'CH_IDLE_STA', 0x31134: 'CH_BUSY_STA',
    0x31138: 'CH_BUSY_STA_SEC', 0x31200: 'MAC_STATUS_CFG', 0x31204: 'PWR_PIN_CFG', 0x31208: 'AUTOWAKEUP_CFG',
    0x31300: 'TX_PIN_CFG', 0x31304: 'TX_BAND_CFG', 0x31308: 'TX_SW_CFG0', 0x3130C: 'TX_SW_CFG1',
    0x31310: 'TX_SW_CFG2', 0x31314: 'TXOP_THRES_CFG', 0x31318: 'TXOP_CTRL_CFG', 0x31344: 'TX_RTS_CFG',
    0x31348: 'TX_TIMEOUT_CFG', 0x3134C: 'TX_RTY_CFG', 0x31350: 'TX_LINK_CFG', 0x31364: 'CCK_PROT_CFG',
    0x31368: 'OFDM_PROT_CFG', 0x3136C: 'MM20_PROT_CFG', 0x31370: 'MM40_PROT_CFG', 0x31374: 'GF20_PROT_CFG',
    0x31378: 'GF40_PROT_CFG', 0x3137C: 'EXP_CTS_TIME', 0x31380: 'EXP_ACK_TIME',
    0x31400: 'RX_FILTER_CFG', 0x31404: 'AUTO_RSP_CFG', 0x31408: 'LEGACY_BASIC_RATE', 0x3140C: 'HT_BASIC_RATE',
    0x31700: 'RX_STA_CNT0', 0x31704: 'RX_STA_CNT1', 0x31708: 'RX_STA_CNT2', 0x3170C: 'TX_STA_CNT0',
    0x31710: 'TX_STA_CNT1', 0x31714: 'TX_STA_CNT2', 0x31718: 'TX_STA_FIFO',
}


def parse(text):
    return {int(a, 16): int(v, 16) for a, v in LINE.findall(text)}


def diff(core, vendor):
    """(addr, core value or None, vendor value or None) for every row that differs or is one-sided."""
    rows = []
    for addr in sorted(set(core) | set(vendor)):
        va, vb = core.get(addr), vendor.get(addr)
        if va != vb:
            rows.append((addr, va, vb))
    return rows


def table(rows, names=NAMES):
    out = [f"{'address':<10} {'modd':>10} {'vendor':>10}  {'diff':<14} name"]
    for addr, va, vb in rows:
        sa = f"0x{va:08x}" if va is not None else "-"
        sb = f"0x{vb:08x}" if vb is not None else "-"
        x = f"xor=0x{(va ^ vb):08x}" if va is not None and vb is not None else "one-sided"
        out.append(f"0x{addr:08x} {sa:>10} {sb:>10}  {x:<14} {names.get(addr, '')}")
    return "\n".join(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    core = parse(Path(sys.argv[1]).read_text(errors='replace'))
    vendor = parse(Path(sys.argv[2]).read_text(errors='replace'))
    rows = diff(core, vendor)
    if '--all' in sys.argv:
        rows = [(a, core.get(a), vendor.get(a)) for a in sorted(set(core) | set(vendor))]
    print(table(rows))
    both = len(set(core) & set(vendor))
    print(f"\n{len(core)} modd rows, {len(vendor)} vendor rows, {both} on both sides, "
          f"{len(diff(core, vendor))} differ or one-sided")
    sys.exit(1 if diff(core, vendor) else 0)


if __name__ == '__main__':
    main()
